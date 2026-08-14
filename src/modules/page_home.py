from __future__ import annotations

import time
import re
from dataclasses import replace
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np
from PySide6.QtCore import QTimer
from PySide6.QtGui import QBrush, QColor, QFont, QImage, QPixmap
from PySide6.QtWidgets import QButtonGroup, QHeaderView, QMessageBox, QTreeWidgetItem

from measurement_core import (
    MeasurementEngine,
    MeasurementResult,
    MeasurementStage,
    StateStore,
    prepare_stereo_pair,
)
from .Ui_PageHome import Ui_PageHome
from .app_config import config_manager, is_setting_group
from .base_page import BasePage
from .camera_device import CameraRole, STEREO_CAMERA_ROLES
from .home_diagnostics import (
    diagnostic_group as _diagnostic_group,
    diagnostic_label_and_unit as _diagnostic_label_and_unit,
    format_diagnostic_value as _format_diagnostic_value,
)
from .measurement_workers import MeasurementWorker, OpcUaWorker
from .signals import signals


class PageHome(BasePage):
    ui: Ui_PageHome
    UI_CLASS = Ui_PageHome

    def _init_state(self) -> None:
        self.view_info = {1: (0, 0, 0), 2: (0, 0, 0)}
        self.camera_light = {1: None, 2: None}
        self.camera_exposure_us = {1: None, 2: None}
        self.roi_mean_values = {1: None, 2: None}
        self.latest_result_overlays: dict[int, list[dict]] = {1: [], 2: []}
        self.latest_preview_shapes: dict[int, tuple[int, ...] | None] = {1: None, 2: None}
        self.config = config_manager.measurement
        self.active_runtime_mode: str | None = None
        self.active_restart_signature = None
        self.running = False
        self.current_stage = MeasurementStage.IDLE
        self.offline_mode = "neck"
        self.offline_image_paths: list[Path] = []
        self.offline_image_index = -1
        self.process_diagnostic_signature: (
            tuple[MeasurementStage, tuple[tuple[str, str], ...]] | None
        ) = None
        self.process_diagnostic_items: dict[str, QTreeWidgetItem] = {}
        self.measurement_worker: MeasurementWorker | None = None
        self.plc_worker: OpcUaWorker | None = None
        self.camera_connected = {1: False, 2: False}
        self.plc_connected = False
        self.offline_timer = QTimer(self)
        self.online_trigger_timer = QTimer(self)
        self.refresh_timer = QTimer(self)
        self.refresh_timer.setSingleShot(True)
        self.latest_online_frames: dict[CameraRole, tuple[float, np.ndarray] | None] = {
            role: None for role in STEREO_CAMERA_ROLES
        }

    def _setup_ui(self) -> None:
        self.ui.viewSplitter.setSizes([520, 520])
        self.ui.mainSplitter.setSizes([960, 320])
        self.ui.runtimeButtonLayout.setStretch(0, 3)
        self.ui.runtimeButtonLayout.setStretch(1, 1)
        self.ui.cam1GraphicsView.set_view_id(1)
        self.ui.cam2GraphicsView.set_view_id(2)
        self.ui.cam1GraphicsView.set_text("Camera 1")
        self.ui.cam2GraphicsView.set_text("Camera 2")
        self.ui.treeProcess.setIndentation(0)
        self.ui.treeProcess.setRootIsDecorated(False)
        process_header = self.ui.treeProcess.header()
        process_header.setMinimumSectionSize(44)
        process_header.setStretchLastSection(False)
        process_header.setSectionResizeMode(0, QHeaderView.ResizeMode.Interactive)
        process_header.setSectionResizeMode(1, QHeaderView.ResizeMode.Interactive)
        process_header.setSectionResizeMode(2, QHeaderView.ResizeMode.Interactive)
        process_header.resizeSection(0, 135)
        process_header.resizeSection(1, 90)
        process_header.resizeSection(2, 52)
        self.led_on = QPixmap(":/images/images/images/ledLow.png")
        self.led_off = QPixmap(":/images/images/images/ledHigh.png")
        self.offline_stage_group = QButtonGroup(self)
        self.offline_stage_group.setExclusive(True)
        for button in self._offline_stage_buttons().values():
            self.offline_stage_group.addButton(button)
        self.ui.btnOnline.setChecked(False)
        self.ui.btnOnline.setText("Offline")
        self._sync_offline_stage_buttons()
        self._reload_offline_image_directory()
        self._refresh_offline_controls()
        self._refresh_offline_image_controls()
        self._refresh_connection_status()
        self._set_status("Ready", ok=True)

    def _bind_events(self) -> None:
        self.ui.cam1GraphicsView.update_info_signal.connect(self._update_view_info)
        self.ui.cam2GraphicsView.update_info_signal.connect(self._update_view_info)
        self.offline_timer.timeout.connect(self._submit_offline_frame)
        self.online_trigger_timer.timeout.connect(self._trigger_online_capture)
        self.refresh_timer.timeout.connect(self._apply_updated_params)
        self.ui.btnOnline.clicked.connect(self._toggle_runtime_mode)
        self.ui.btnStart.clicked.connect(self._toggle_runtime_running)
        for mode, button in self._offline_stage_buttons().items():
            button.clicked.connect(lambda _checked=False, selected=mode: self._select_offline_mode(selected))
        self.ui.btnFirstImage.clicked.connect(lambda: self._set_offline_image_index(0))
        self.ui.btnPreviousImage.clicked.connect(lambda: self._step_offline_image(-1))
        self.ui.btnNextImage.clicked.connect(lambda: self._step_offline_image(1))
        self.ui.btnLastImage.clicked.connect(
            lambda: self._set_offline_image_index(len(self.offline_image_paths) - 1)
        )

    def _bind_signals(self) -> None:
        signals.camera_frame_captured.connect(self._on_camera_frame)
        signals.camera_exposure_changed.connect(self._on_camera_exposure_changed)
        signals.online_camera_started.connect(self._on_online_camera_started)
        signals.online_camera_stopped.connect(self._on_online_camera_stopped)
        signals.online_camera_failed.connect(self._on_online_camera_failed)
        config_manager.changed.connect(self._on_config_changed)
        config_manager.batch_changed.connect(self._apply_updated_params)
        signals.app_close.connect(self.stop_runtime)

    def start_runtime(self) -> bool:
        if self.running:
            return True
        try:
            mode = self._runtime_mode()
            plc_test_mode = mode == "online" and self.config.runtime.disable_camera_for_plc_test
            if not plc_test_mode:
                store = StateStore(self.config.runtime.state_file)
                try:
                    state = store.load()
                except Exception as exc:
                    state = None
                    self._log(f"State snapshot ignored: {exc}")
                engine = MeasurementEngine(self.config, state)
                self.measurement_worker = MeasurementWorker(engine, store, self)
                self.measurement_worker.result_ready.connect(self._on_measurement_result)
                self.measurement_worker.failed.connect(lambda message: self._warn_status(f"Measurement error: {message}"))
                self.measurement_worker.start()

            self.running = True
            self.active_runtime_mode = mode
            self.active_restart_signature = self._restart_signature()
            if mode == "online":
                self.current_stage = MeasurementStage.IDLE
                signals.online_stage_changed.emit(int(self.current_stage))
                self._start_plc_worker()
                if self.config.runtime.disable_camera_for_plc_test:
                    self._log("PLC test mode: camera and measurement worker skipped")
                else:
                    signals.online_camera_start_requested.emit()
                    if not self.running:
                        return False
            elif mode == "offline":
                self.ui.lblFrameDelta.setText("Frame delta: --")
                if self.config.runtime.connect_plc_in_offline:
                    self._start_plc_worker()
                self._reload_offline_image_directory(preserve_current=True)
                self.current_stage = _stage_from_offline_mode(self.offline_mode)
                self.offline_timer.start(self.config.runtime.loop_interval_ms)
                QTimer.singleShot(0, self._submit_offline_frame)

            self._refresh_offline_controls()
            self._log(f"Runtime started: {mode}")
            return True
        except Exception as exc:
            self.running = False
            self.active_runtime_mode = None
            self.active_restart_signature = None
            self._stop_workers()
            self._set_status(f"Start failed: {exc}", ok=False)
            self._log(f"Start failed: {exc}")
            return False

    def stop_runtime(self) -> None:
        was_running = self.running
        self.running = False
        self.offline_timer.stop()
        self.online_trigger_timer.stop()
        if self.active_runtime_mode == "online" and not self.config.runtime.disable_camera_for_plc_test:
            signals.online_camera_stop_requested.emit()
        self._stop_workers()
        self.latest_online_frames = {role: None for role in STEREO_CAMERA_ROLES}
        self.active_runtime_mode = None
        self.active_restart_signature = None
        self.camera_connected = {1: False, 2: False}
        self.plc_connected = False
        self._refresh_connection_status()
        self._refresh_offline_controls()
        if was_running:
            self._log("Runtime stopped")

    def _toggle_runtime_running(self) -> None:
        if self.running:
            self.stop_runtime()
        else:
            self.start_runtime()

    def _stop_workers(self) -> None:
        if self.plc_worker is not None:
            self.plc_worker.stop()
            self.plc_worker.wait(2500)
            self.plc_worker = None
        if self.measurement_worker is not None:
            worker = self.measurement_worker
            worker.stop()
            worker.wait()
            try:
                worker.state_store.save(worker.engine.state)
            except Exception as exc:
                self._log(f"Final state save failed: {exc}")
            self.measurement_worker = None

    def _start_plc_worker(self) -> None:
        if self.plc_worker is not None:
            return
        self.plc_worker = OpcUaWorker(self)
        self.plc_worker.controls_changed.connect(self._on_plc_controls)
        self.plc_worker.connection_changed.connect(self._on_plc_connection)
        self.plc_worker.start()
        self._log("PLC worker started")

    def _submit_offline_frame(self) -> None:
        if not self.running or self.active_runtime_mode != "offline" or self.measurement_worker is None:
            return
        self._reload_offline_image_directory(preserve_current=True)
        path = self._current_offline_image_path()
        if path is None:
            self._warn_status(f"No offline images found: {self.config.runtime.offline_image_dir}")
            return
        image = _read_image(path)
        if image is None:
            self._warn_status(f"Cannot read offline image: {path}")
            return
        if image.shape[1] % 2:
            self._warn_status("Offline composite image width must be even")
            return
        middle = image.shape[1] // 2
        try:
            pair = prepare_stereo_pair(
                image[:, :middle],
                image[:, middle:],
                time.time(),
                "offline",
                self._online_crop_origin(),
            )
        except Exception as exc:
            self._warn_status(f"Offline image error: {exc}")
            return
        self.measurement_worker.submit(pair, self.current_stage)

    def _trigger_online_capture(self) -> None:
        if (
            not self.running
            or self.active_runtime_mode != "online"
            or self.measurement_worker is None
            or not all(self.camera_connected.values())
        ):
            return
        self.latest_online_frames = {role: None for role in STEREO_CAMERA_ROLES}
        signals.online_camera_trigger_requested.emit()

    def _on_camera_frame(self, role_value: str, image: object) -> None:
        if not self.running or self.active_runtime_mode != "online" or self.measurement_worker is None:
            return
        role = CameraRole.from_user_id(role_value)
        if role not in STEREO_CAMERA_ROLES:
            self._warn_status(f"Online frame ignored: unknown camera id {role_value}")
            return
        if role == CameraRole.CAM1:
            self.camera_connected[1] = True
        elif role == CameraRole.CAM2:
            self.camera_connected[2] = True
        self._refresh_connection_status()
        self.latest_online_frames[role] = (time.monotonic(), np.asarray(image))
        first = self.latest_online_frames[CameraRole.CAM1]
        second = self.latest_online_frames[CameraRole.CAM2]
        if first is None or second is None:
            return
        delta_ms = abs(first[0] - second[0]) * 1000.0
        self.ui.lblFrameDelta.setText(f"Frame delta: {delta_ms:.1f} ms")
        limit_ms = float(self.config.runtime.stereo_pair_max_delta_ms)
        if delta_ms > limit_ms:
            older = CameraRole.CAM1 if first[0] < second[0] else CameraRole.CAM2
            self.latest_online_frames[older] = None
            self._warn_status(
                "Online stereo pair dropped: "
                f"camera frame delta {delta_ms:.1f} ms > {limit_ms:.1f} ms"
            )
            return
        self.latest_online_frames = {role: None for role in STEREO_CAMERA_ROLES}
        try:
            pair = prepare_stereo_pair(
                first[1],
                second[1],
                time.time(),
                "online",
                self._online_crop_origin(),
            )
        except Exception as exc:
            self._warn_status(f"Online frame error: {exc}")
            return
        self.measurement_worker.submit(pair, self._measurement_stage_for_capture())

    def _on_measurement_result(self, result: MeasurementResult) -> None:
        self.ui.cam1GraphicsView.show_pixmap(
            _cv_to_pixmap(result.preview1), preserve_view=True
        )
        self.ui.cam2GraphicsView.show_pixmap(
            _cv_to_pixmap(result.preview2), preserve_view=True
        )
        self.latest_result_overlays[1] = list(result.overlay1)
        self.latest_result_overlays[2] = list(result.overlay2)
        self.latest_preview_shapes[1] = result.preview1.shape
        self.latest_preview_shapes[2] = result.preview2.shape
        self.roi_mean_values[1] = self._roi_mean_value(1, result.preview1)
        self.roi_mean_values[2] = self._roi_mean_value(2, result.preview2)
        self.ui.cam1GraphicsView.update_contours(
            self._view_overlay_elements(1, result.preview1.shape, self.latest_result_overlays[1])
        )
        self.ui.cam2GraphicsView.update_contours(
            self._view_overlay_elements(2, result.preview2.shape, self.latest_result_overlays[2])
        )
        values = result.values
        self.ui.lblDiameter.setText(_format_value(values.diameter_mm))
        self.camera_light[1] = float(result.diagnostics.get("light_camera1", 0))
        self.camera_light[2] = float(result.diagnostics.get("light_camera2", 0))
        self._refresh_view_info(1)
        self._refresh_view_info(2)
        self.ui.lblCycle.setText(f"Cycle: {result.diagnostics.get('cycle_ms', 0):.1f} ms")
        self.ui.lblLastFrame.setText(datetime.fromtimestamp(result.timestamp_s).strftime("Last frame: %H:%M:%S"))
        # if self.active_runtime_mode != "online":
        #     self.current_stage = result.stage
        #     self._sync_offline_stage_buttons()
        self._update_process_diagnostics(result.diagnostics, result.stage)
        self._set_status(result.message, ok=result.valid)
        if not result.valid:
            self._log(result.message)
        elif self.plc_worker is not None and values.complete():
            self.plc_worker.queue_values(values)

    def _on_plc_controls(self, stage_value: int, shoulder_transition: bool) -> None:
        self.current_stage = _stage_from_plc_controls(
            stage_value,
            shoulder_transition,
        )
        signals.online_stage_changed.emit(int(self.current_stage))
        self._sync_offline_stage_buttons()
        self._refresh_online_trigger_interval()
        self._log(f"PLC stage={stage_value}, shoulder_transition={int(shoulder_transition)}")

    def _offline_stage_buttons(self) -> dict[str, object]:
        return {
            "idle": self.ui.btnStageIdle,
            "neck": self.ui.btnStageNeck,
            "crown": self.ui.btnStageCrown,
            "body": self.ui.btnStageBody,
            "endcone": self.ui.btnStageEndcone,
        }

    def _select_offline_mode(self, mode: str) -> None:
        if mode not in self._offline_stage_buttons() or self._runtime_mode() != "offline":
            return
        self.offline_mode = mode
        self.current_stage = _stage_from_offline_mode(mode)
        self._sync_offline_stage_buttons()
        self._log(f"Offline stage selected: {mode.title()}")
        if self.running:
            QTimer.singleShot(0, self._submit_offline_frame)

    def _sync_offline_stage_buttons(self) -> None:
        mode = _mode_from_stage(self.current_stage)
        # if self._runtime_mode() == "offline":
        #     mode = self.offline_mode
        button = self._offline_stage_buttons().get(mode)
        if button is not None:
            button.setChecked(True)

    def _reload_offline_image_directory(self, preserve_current: bool = False) -> None:
        previous_path = self._current_offline_image_path() if preserve_current else None
        paths = _list_image_files(Path(self.config.runtime.offline_image_dir))
        self.offline_image_paths = paths
        if previous_path in paths:
            self.offline_image_index = paths.index(previous_path)
        else:
            self.offline_image_index = 0 if paths else -1
        self._refresh_offline_image_controls()

    def _step_offline_image(self, offset: int) -> None:
        if not self.offline_image_paths:
            return
        target = int(np.clip(self.offline_image_index + offset, 0, len(self.offline_image_paths) - 1))
        self._set_offline_image_index(target)

    def _set_offline_image_index(self, target: int) -> None:
        if not self.offline_image_paths:
            return
        target = int(np.clip(target, 0, len(self.offline_image_paths) - 1))
        if target == self.offline_image_index:
            return
        self.offline_image_index = target
        self._refresh_offline_image_controls()
        path = self._current_offline_image_path()
        if path is not None:
            self._log(f"Offline image: {path.name}")
        if self.running and self.active_runtime_mode == "offline":
            QTimer.singleShot(0, self._submit_offline_frame)

    def _current_offline_image_path(self) -> Path | None:
        if 0 <= self.offline_image_index < len(self.offline_image_paths):
            return self.offline_image_paths[self.offline_image_index]
        return None

    def _refresh_offline_image_controls(self) -> None:
        count = len(self.offline_image_paths)
        index = self.offline_image_index
        path = self._current_offline_image_path()
        self.ui.lblOfflineImage.setText(path.name if path is not None else "No image")
        self.ui.lblOfflineImage.setToolTip(str(path) if path is not None else "")
        self.ui.lblImageIndex.setText(f"{index + 1} / {count}" if count else "0 / 0")
        is_offline = self._runtime_mode() == "offline"
        allow_sequence = is_offline
        self.ui.btnFirstImage.setEnabled(allow_sequence and index > 0)
        self.ui.btnPreviousImage.setEnabled(allow_sequence and index > 0)
        self.ui.btnNextImage.setEnabled(allow_sequence and 0 <= index < count - 1)
        self.ui.btnLastImage.setEnabled(allow_sequence and 0 <= index < count - 1)

    def _refresh_offline_controls(self) -> None:
        is_offline = self._runtime_mode() == "offline"
        is_online = not is_offline
        self.ui.btnOnline.blockSignals(True)
        self.ui.btnOnline.setChecked(is_online)
        self.ui.btnOnline.setText("Online" if is_online else "Offline")
        self.ui.btnOnline.blockSignals(False)
        self.ui.btnStart.setEnabled(is_offline)
        self._refresh_runtime_button()
        controls = [
            self.ui.labelOfflineMode,
            self.ui.labelOfflineImages,
            self.ui.lblOfflineImage,
            self.ui.lblImageIndex,
            self.ui.btnFirstImage,
            self.ui.btnPreviousImage,
            self.ui.btnNextImage,
            self.ui.btnLastImage,
            *self._offline_stage_buttons().values(),
        ]
        for control in controls:
            control.setVisible(True)
        for button in self._offline_stage_buttons().values():
            button.setEnabled(is_offline)
        self._sync_offline_stage_buttons()
        self._refresh_offline_image_controls()

    def _refresh_runtime_button(self) -> None:
        self.ui.btnStart.setText("Stop" if self.running else "Run")
        self.ui.btnStart.setProperty("runtimeState", "running" if self.running else "stopped")
        self.ui.btnStart.style().unpolish(self.ui.btnStart)
        self.ui.btnStart.style().polish(self.ui.btnStart)

    def _update_view_info(self, view_id: int, x: int, y: int, gray: int) -> None:
        if view_id not in self.view_info:
            return
        self.view_info[view_id] = (x, y, gray)
        self._refresh_view_info(view_id)

    def _refresh_view_info(self, view_id: int) -> None:
        x, y, gray = self.view_info[view_id]
        light = self.camera_light[view_id]
        light_text = "--" if light is None else f"{light:.1f}"
        if self.active_runtime_mode == "online":
            exposure = self.camera_exposure_us[view_id]
            exposure_text = "--" if exposure is None else f"{exposure / 1000.0:.2f}"
            roi_mean = self.roi_mean_values[view_id]
            roi_mean_text = "--" if roi_mean is None else f"{roi_mean:.1f}"
        else:
            exposure_text = "--"
            roi_mean_text = "--"
        label = self.ui.lblCam1Info if view_id == 1 else self.ui.lblCam2Info
        label.setText(
            f"Pos ({x},{y}) | G {gray} | Light {light_text} | Exp {exposure_text} ms | Mean {roi_mean_text}"
        )

    def _on_camera_exposure_changed(self, role_value: str, exposure_us: float) -> None:
        role = CameraRole.from_user_id(role_value)
        if role == CameraRole.CAM1:
            view_id = 1
        elif role == CameraRole.CAM2:
            view_id = 2
        else:
            return
        self.camera_exposure_us[view_id] = float(exposure_us)
        self._refresh_view_info(view_id)

    def _update_process_diagnostics(
        self,
        diagnostics: dict[str, object],
        stage: MeasurementStage,
    ) -> None:
        rows = [
            (
                _diagnostic_group(key),
                key,
                *_diagnostic_label_and_unit(key, stage),
                value,
            )
            for key, value in diagnostics.items()
        ]
        signature = (
            stage,
            tuple((group, key) for group, key, _label, _unit, _value in rows),
        )
        if signature != self.process_diagnostic_signature:
            self.ui.treeProcess.clear()
            self.process_diagnostic_items.clear()
            group_items: dict[str, QTreeWidgetItem] = {}
            for group, key, label, unit, _value in rows:
                group_item = group_items.get(group)
                if group_item is None:
                    group_item = QTreeWidgetItem(self.ui.treeProcess, [group])
                    group_item.setExpanded(True)
                    self._style_process_group_item(group_item)
                    group_items[group] = group_item
                item = QTreeWidgetItem(group_item, [label, "NA", unit])
                item.setToolTip(0, key)
                self.process_diagnostic_items[key] = item
            self.process_diagnostic_signature = signature

        for _group, key, _label, _unit, value in rows:
            formatted = _format_diagnostic_value(value)
            self.process_diagnostic_items[key].setText(1, formatted)
            self.process_diagnostic_items[key].setToolTip(1, formatted)

    def _style_process_group_item(self, item: QTreeWidgetItem) -> None:
        item.setFirstColumnSpanned(True)
        font = QFont()
        font.setBold(True)
        background = QBrush(QColor(31, 36, 44))
        foreground = QBrush(QColor(230, 234, 241))
        for column in range(self.ui.treeProcess.columnCount()):
            item.setFont(column, font)
            item.setBackground(column, background)
            item.setForeground(column, foreground)

    def _on_plc_connection(self, connected: bool, message: str) -> None:
        self.plc_connected = connected
        self._refresh_connection_status()
        self._set_status(message, ok=connected)
        if not connected:
            self._log(f"PLC disconnected: {message}")

    def _on_online_camera_started(self) -> None:
        self.camera_connected = {1: True, 2: True}
        self._refresh_connection_status()
        self._refresh_online_trigger_interval()
        self._trigger_online_capture()
        self._log("Online cameras started")

    def _on_online_camera_stopped(self) -> None:
        self.online_trigger_timer.stop()
        self.camera_connected = {1: False, 2: False}
        self._refresh_connection_status()
        self._log("Online cameras stopped")

    def _on_online_camera_failed(self, message: str) -> None:
        self.online_trigger_timer.stop()
        self.camera_connected = {1: False, 2: False}
        self._refresh_connection_status()
        self._log(f"Online camera failed: {message}")
        self.stop_runtime()
        # 相机掉线属于 Online 条件失效，直接回到 Offline，不弹出模式切换确认框。
        self.ui.btnOnline.blockSignals(True)
        self.ui.btnOnline.setChecked(False)
        self.ui.btnOnline.blockSignals(False)
        self._refresh_offline_controls()
        self._set_status(f"Online camera failed: {message}", ok=False)

    def _toggle_runtime_mode(self, checked: bool) -> None:
        target_mode = "online" if checked else "offline"
        previous_mode = self.active_runtime_mode if self.running else ("offline" if checked else "online")
        question = f"Switch to {target_mode.title()} mode?"
        reply = QMessageBox.question(
            self,
            "Confirm Mode Change",
            question,
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            self.ui.btnOnline.blockSignals(True)
            self.ui.btnOnline.setChecked(previous_mode == "online")
            self.ui.btnOnline.blockSignals(False)
            self._refresh_offline_controls()
            return
        self.stop_runtime()
        if target_mode == "online":
            if not self.start_runtime():
                self.ui.btnOnline.blockSignals(True)
                self.ui.btnOnline.setChecked(False)
                self.ui.btnOnline.blockSignals(False)
        self._refresh_offline_controls()

    def _refresh_connection_status(self) -> None:
        self._set_led(self.ui.lblCamera1Status, self.camera_connected[1])
        self._set_led(self.ui.lblCamera2Status, self.camera_connected[2])
        self._set_led(self.ui.lblPlcStatus, self.plc_connected)

    def _set_led(self, label, connected: bool) -> None:
        label.setPixmap(self.led_on if connected else self.led_off)

    def _on_config_changed(self, group: str, key: str, _value: object) -> None:
        if is_setting_group(group):
            if self.measurement_worker is not None:
                self.measurement_worker.engine.config = self.config
            if group == "Measurement" and key.startswith("auto_exposure_roi_"):
                self._refresh_auto_exposure_roi_overlays()
            return
        if group == "Camera" and key == "auto_exposure_enabled":
            self._refresh_auto_exposure_roi_overlays()
            return
        if group == "Runtime" or (group == "Camera" and key == "online_crop_roi"):
            self.refresh_timer.start(300)

    def _apply_updated_params(self) -> None:
        self.refresh_timer.stop()
        self._reload_offline_image_directory(preserve_current=True)
        self._refresh_offline_controls()
        if self.measurement_worker is not None:
            self.measurement_worker.engine.config = self.config

        restart_required = (
            self.running
            and self.active_restart_signature is not None
            and self.active_restart_signature != self._restart_signature()
        )
        if restart_required:
            self.stop_runtime()
            QTimer.singleShot(0, self.start_runtime)
        elif not self.running:
            QTimer.singleShot(0, self.start_runtime)

    def _restart_signature(self):
        return (
            replace(self.config.runtime),
            self._online_crop_roi(),
        )

    def _runtime_mode(self) -> str:
        return "online" if self.ui.btnOnline.isChecked() else "offline"

    def _measurement_stage_for_capture(self) -> MeasurementStage:
        if self.current_stage == MeasurementStage.IDLE:
            return MeasurementStage.NECK
        return self.current_stage

    def _refresh_online_trigger_interval(self) -> None:
        if not self.running or self.active_runtime_mode != "online":
            return
        interval = self._online_sample_interval_ms()
        if self.online_trigger_timer.interval() != interval or not self.online_trigger_timer.isActive():
            self.online_trigger_timer.start(interval)

    def _online_sample_interval_ms(self) -> int:
        settings = self.config.runtime
        return {
            MeasurementStage.IDLE: settings.idle_sample_interval_ms,
            MeasurementStage.NECK: settings.neck_sample_interval_ms,
            MeasurementStage.CROWN: settings.crown_sample_interval_ms,
            MeasurementStage.BODY: settings.body_sample_interval_ms,
            MeasurementStage.ENDCONE: settings.endcone_sample_interval_ms,
        }.get(self.current_stage, settings.idle_sample_interval_ms)

    @staticmethod
    def _online_crop_roi() -> tuple[int, int, int, int]:
        roi = config_manager.camera.online_crop_roi
        return roi.x, roi.y, roi.width, roi.height

    @classmethod
    def _online_crop_origin(cls) -> tuple[int, int]:
        x, y, _width, _height = cls._online_crop_roi()
        return x, y

    def _view_overlay_elements(
        self,
        view_id: int,
        image_shape: tuple[int, ...],
        base_elements: list[dict] | None = None,
    ) -> list[dict]:
        elements = list(base_elements or [])
        elements.extend(self._auto_exposure_roi_overlay_elements(view_id, image_shape))
        return elements

    def _auto_exposure_roi_overlay_elements(self, view_id: int, image_shape: tuple[int, ...]) -> list[dict]:
        if self.active_runtime_mode != "online" or not config_manager.camera.auto_exposure_enabled:
            return []
        rect = self._auto_exposure_roi_rect(view_id, image_shape)
        if rect is None:
            return []
        x, y, width, height = rect
        return [{
            "type": "rectangle",
            "data": (float(x), float(y), float(width), float(height), None),
            "color": "#0000FF",
            "width": 2,
        }]

    def _auto_exposure_roi_rect(self, view_id: int, image_shape: tuple[int, ...]) -> tuple[int, int, int, int] | None:
        roi = (
            self.config.measurement.auto_exposure_roi_camera1
            if view_id == 1
            else self.config.measurement.auto_exposure_roi_camera2
        )
        image_h, image_w = image_shape[:2]
        if image_h <= 0 or image_w <= 0 or roi.width <= 0 or roi.height <= 0:
            return None
        x0 = max(0, min(int(roi.x), image_w - 1))
        y0 = max(0, min(int(roi.y), image_h - 1))
        x1 = max(x0 + 1, min(x0 + int(roi.width), image_w))
        y1 = max(y0 + 1, min(y0 + int(roi.height), image_h))
        return x0, y0, x1 - x0, y1 - y0

    def _roi_mean_value(self, view_id: int, image: np.ndarray) -> float | None:
        if image is None:
            return None
        rect = self._auto_exposure_roi_rect(view_id, image.shape)
        if rect is None:
            return None
        x, y, width, height = rect
        roi = image[y : y + height, x : x + width]
        if roi.size == 0:
            return None
        if roi.ndim == 3:
            if roi.shape[2] == 4:
                roi = cv2.cvtColor(roi, cv2.COLOR_BGRA2GRAY)
            else:
                roi = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        return float(np.mean(roi))

    def _refresh_auto_exposure_roi_overlays(self) -> None:
        for view_id, view in ((1, self.ui.cam1GraphicsView), (2, self.ui.cam2GraphicsView)):
            image = view.get_image()
            if image is None:
                continue
            image_shape = self.latest_preview_shapes.get(view_id) or (image.height(), image.width())
            view.update_contours(
                self._view_overlay_elements(view_id, image_shape, self.latest_result_overlays.get(view_id))
            )

    def _log(self, text: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.ui.txtLog.appendPlainText(f"{stamp} {text}")
        self._set_status(text, ok=not _looks_like_error(text))

    def _warn_status(self, text: str) -> None:
        if self.ui.lblStatus.text() != text:
            stamp = datetime.now().strftime("%H:%M:%S")
            self.ui.txtLog.appendPlainText(f"{stamp} {text}")
        self._set_status(text, ok=False)

    def _set_status(self, text: str, ok: bool = True) -> None:
        self.ui.lblStatus.setText(text)
        self.ui.lblStatus.setToolTip(text)
        color = "rgb(42, 170, 80)" if ok else "rgb(220, 70, 70)"
        self.ui.lblStatus.setStyleSheet(
            f"background-color: {color}; color: black; border-radius: 4px; padding: 4px 6px;"
        )


def _cv_to_pixmap(image: np.ndarray) -> QPixmap:
    array = np.ascontiguousarray(image)
    height, width = array.shape[:2]
    if array.ndim == 2:
        qimage = QImage(array.data, width, height, width, QImage.Format.Format_Grayscale8)
    else:
        rgb = cv2.cvtColor(array, cv2.COLOR_BGR2RGB)
        qimage = QImage(rgb.data, width, height, width * 3, QImage.Format.Format_RGB888)
    return QPixmap.fromImage(qimage.copy())


def _format_value(value: float | None) -> str:
    return "--" if value is None or not np.isfinite(value) else f"{value:.3f}"


_SUPPORTED_IMAGE_SUFFIXES = {".bmp", ".png", ".jpg", ".jpeg", ".tif", ".tiff"}


def _stage_from_offline_mode(mode: str) -> MeasurementStage:
    return {
        "idle": MeasurementStage.IDLE,
        "neck": MeasurementStage.NECK,
        "crown": MeasurementStage.CROWN,
        "body": MeasurementStage.BODY,
        "endcone": MeasurementStage.ENDCONE,
    }.get(mode, MeasurementStage.IDLE)


def _mode_from_stage(stage: MeasurementStage) -> str:
    return {
        MeasurementStage.IDLE: "idle",
        MeasurementStage.NECK: "neck",
        MeasurementStage.CROWN: "crown",
        MeasurementStage.BODY: "body",
        MeasurementStage.ENDCONE: "endcone",
    }.get(stage, "idle")


def _looks_like_error(text: str) -> bool:
    normalized = text.lower()
    return any(
        token in normalized
        for token in (
            "failed",
            "error",
            "cannot",
            "missing",
            "invalid",
            "unsupported",
            "disconnected",
            "not found",
            "not enough",
            "outside",
            "too large",
            "inconsistent",
        )
    )


def _stage_from_plc_controls(
    stage_value: int,
    shoulder_transition: bool,
) -> MeasurementStage:
    stage = MeasurementStage.from_value(stage_value)
    if stage == MeasurementStage.CROWN and shoulder_transition:
        return MeasurementStage.BODY
    return stage


def _natural_sort_key(path: Path) -> tuple:
    return tuple(
        (0, int(part)) if part.isdigit() else (1, part.casefold())
        for part in re.split(r"(\d+)", path.name)
    )


def _image_sort_key(path: Path) -> tuple:
    timestamp = re.search(r"(?<!\d)(\d{8}_\d{6})(?!\d)", path.stem)
    if timestamp is not None:
        return 0, timestamp.group(1), path.name.casefold()
    return 1, _natural_sort_key(path)


def _list_image_files(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        (
            path
            for path in directory.iterdir()
            if path.is_file() and path.suffix.casefold() in _SUPPORTED_IMAGE_SUFFIXES
        ),
        key=_image_sort_key,
    )


def _read_image(path: Path) -> np.ndarray | None:
    try:
        encoded = np.fromfile(path, dtype=np.uint8)
    except OSError:
        return None
    if encoded.size == 0:
        return None
    return cv2.imdecode(encoded, cv2.IMREAD_UNCHANGED)
