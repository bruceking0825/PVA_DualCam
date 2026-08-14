import os
import threading
import time
from PySide6.QtWidgets import QFileDialog
from PySide6.QtGui import QImage, QPixmap
import cv2
import numpy as np
from .Ui_PageCamera import Ui_PageCamera
from .app_config import config_manager
from .base_page import BasePage
import gxipy as gx
from .signals import signals
from widgets import CustomGraphicsView
from PySide6.QtCore import Signal
from .camera_state import CameraStateStore
from .camera_manager import CameraManager
from .camera_device import CameraDevice, CameraRole, STEREO_CAMERA_ROLES
from .app_settings import Settings
from config_models import RoiSettings
from measurement_core.models import MeasurementStage
from vision.ui.editor.node_scene import NodeScene
from vision.ui.editor.node_view import NodeView

from vision.services.pipeline_service import PipelineService
from vision.ui.panels.toolbox_panel import ToolboxPanel

class PageCamera(BasePage):
    ui: Ui_PageCamera
    UI_CLASS = Ui_PageCamera
    frame_captured = Signal(object)

    ROLE_PARAM_ATTRS = {
        CameraRole.CAM1: ("initial_exposure_camera1", "gain_camera1"),
        CameraRole.CAM2: ("initial_exposure_camera2", "gain_camera2"),
    }
    ROLE_STATE_KEYS = {
        CameraRole.CAM1: "camera1",
        CameraRole.CAM2: "camera2",
    }
    AUTO_EXPOSURE_ROI_ATTRS = {
        CameraRole.CAM1: "auto_exposure_roi_camera1",
        CameraRole.CAM2: "auto_exposure_roi_camera2",
    }
    def _init_state(self) -> None:
        self.image_on = "images/images/ledLow.png"
        self.image_off = "images/images/ledHigh.png" 
        self.last_open_dir = os.getcwd()
        self.orgImg = None
        self.settings = config_manager.camera
        self.measurement_settings = config_manager.measurement.measurement

        self.camera_manager: CameraManager | None = None
        self.stream_owner: str | None = None
        self.current_camera: CameraDevice | None = None
        self.auto_exposure_last_adjust_s = {role: 0.0 for role in STEREO_CAMERA_ROLES}
        self.online_exposure_us: dict[CameraRole, float] = {}
        self.camera_state_lock = threading.Lock()
        self.online_stage = MeasurementStage.IDLE

    def _setup_ui(self) -> None:
        self.scene = NodeScene()
        self.view = NodeView(self.scene)
        self.service = PipelineService(self.scene.graph)
        self.scene.selectionChanged.connect(self._on_pipeline_selection_changed)
        self.toolbox = ToolboxPanel()
        self.ui.QVBtoolBox.addWidget(self.toolbox)
        self.ui.QHBView.addWidget(self.view)
        self.ui.splitter.setSizes([500, 500])
        self.ui.splitter_3.setSizes([600, 400])
        self.ui.splitter_2.setSizes([500, 500])
        self.ui.orgGraphicsView.set_view_id(1)
        self.ui.transGraphicsView.set_view_id(2)

        # Initialize graphics view info labels.
        self.ui.lblOrgInfo.setText(f"View 1 - Pos: (0, 0) | Value: 0")
        self.ui.lblTransformedInfo.setText(f"View 2 - Pos: (0, 0) | Value: 0")

    def _on_ready(self) -> None:
        self.refresh_camera_list()

    def refresh_camera_list(self) -> None:
        if self.camera_manager:
            self._close_all_cameras("refresh camera list")
            self.camera_manager = None
        self.camera_manager = CameraManager()
        cameras = self.camera_manager.enumerate()

        self.ui.combCameraList.clear()
        connected_roles = []
        for role in STEREO_CAMERA_ROLES:
            if self.camera_manager.get_by_role(role):
                connected_roles.append(role)
                self.ui.combCameraList.addItem(role.name, role)

        self.ui.combCameraList.setCurrentIndex(-1)
        self._set_manual_controls_enabled(False)

        if connected_roles:
            self.ui.combCameraList.setCurrentIndex(0)
            self._select_camera(0)

        connected_names = ", ".join(role.name for role in connected_roles) or "None"
        signals.status.emit("Camera", "OK", "info", f"{len(cameras)} camera found: {connected_names}")

        missing = [role.name for role in STEREO_CAMERA_ROLES if role not in connected_roles]
        if missing:
            signals.status.emit("Camera", "OK", "info", f"Camera not connected for debug: {', '.join(missing)}")
        
    def _bind_signals(self) -> None:
        self.frame_captured.connect(self._display_captured_frame)

        self.ui.orgGraphicsView.update_info_signal.connect(self._update_view_info)
        self.ui.orgGraphicsView.image_loaded_signal.connect(lambda img: setattr(self, 'orgImg', img))
        self.ui.transGraphicsView.update_info_signal.connect(self._update_view_info)


        signals.status.connect(self._on_status)
        config_manager.changed.connect(self._on_config_changed)
        signals.online_camera_start_requested.connect(self.start_online_cameras)
        signals.online_camera_stop_requested.connect(self.stop_online_cameras)
        signals.online_camera_trigger_requested.connect(self.trigger_online_cameras)
        signals.online_stage_changed.connect(self._on_online_stage_changed)
        signals.app_close.connect(self.close)

    def _bind_events(self) -> None:
        # 闁搞儱澧芥晶鏍ㄧ箙椤愩垹甯犻柡鍕⒔閵?
        self.ui.btnOrgFullFill.clicked.connect(lambda: self.ui.orgGraphicsView.full_fill())
        self.ui.btnTransformedFullFill.clicked.connect(lambda: self.ui.transGraphicsView.full_fill())
        self.ui.btnOrgOpen.clicked.connect(self.open_image)

        self.ui.btnCamON.toggled.connect(self.on_open_camera)

        self.ui.btnStartSnap.toggled.connect(self.on_start_snap)

        self.ui.btnSoftTrigger.clicked.connect(self.on_soft_trigger)
        self.ui.combCameraList.currentIndexChanged.connect(self._select_camera)
        self.ui.btnRefresh.clicked.connect(self.refresh_camera_list)
        self.ui.combTrigMode.currentIndexChanged.connect(lambda mode: self.on_trigger_mode_changed(self.ui.combTrigMode.currentData()))
        self.ui.combTrigSource.currentIndexChanged.connect(lambda: self.on_trigger_source(self.ui.combTrigSource.currentData()))
        self.ui.combTrigEdge.currentIndexChanged.connect(lambda: self.on_trigger_falling_edge(self.ui.combTrigEdge.currentData()))
        self.ui.edtExposure.returnPressed.connect(self.on_set_exposure)
        self.ui.edtGain.returnPressed.connect(self.on_set_gain)
        self.ui.edtWidth.returnPressed.connect(self.on_set_width)
        self.ui.edtHeight.returnPressed.connect(self.on_set_height)
        self.ui.edtOffsetX.returnPressed.connect(self.on_set_offsetX)
        self.ui.edtOffsetY.returnPressed.connect(self.on_set_offsetY)

        self.ui.btnRun.clicked.connect(self._run_pipeline)
        self.ui.btnConfig.clicked.connect(self.service.configure)
        self.ui.btnSave.clicked.connect(self._save_pipeline)
        self.ui.btnLoad.clicked.connect(self._load_pipeline)
                        

    def _on_online_stage_changed(self, stage_value: int) -> None:
        self.online_stage = MeasurementStage.from_value(stage_value)

    def open_file_dialog(self):
        file_name, _ = QFileDialog.getOpenFileName(
            self,
            "Select image file",
            self.last_open_dir,
            "Image files (*.png *.jpg *.jpeg *.bmp *.gif);"
        )
        if file_name:
            self.last_open_dir = os.path.dirname(file_name)
            return file_name
        return ""
                    
    def open_image(self):
        image_path = self.open_file_dialog()
        if not image_path:
            self.orgImg = None
            return
        self.orgImg = cv2.imread(image_path)
        if self.orgImg is None:
            self.ui.orgGraphicsView.set_text("Cannot load image")
            return
        self.ui.orgGraphicsView.load_image(image_path)

    def apply_camera_params(self, cam: CameraDevice, roi=None, exposure_us: float | None = None):
        """Apply role-specific exposure, gain, and ROI to the camera."""
        param_attrs = self.ROLE_PARAM_ATTRS.get(cam.role)
        if param_attrs:
            exposure_attr, gain_attr = param_attrs
            exposure = (
                float(exposure_us)
                if exposure_us is not None
                else float(getattr(self.settings, exposure_attr))
            )
            cam.set_exposure(exposure)
            cam.set_gain(float(getattr(self.settings, gain_attr)))
        self._apply_camera_roi(cam, roi)

    def _apply_camera_roi(self, cam: CameraDevice, roi=None) -> None:
        roi = roi or self.settings.offline_crop_roi
        cam.set_offsetX(0)
        cam.set_offsetY(0)
        cam.set_width(int(roi.width))
        cam.set_height(int(roi.height))
        cam.set_offsetX(int(roi.x))
        cam.set_offsetY(int(roi.y))

    def _on_config_changed(self, group: str, key: str, value: object) -> None:
        if group == "Measurement" and key.startswith("auto_exposure_roi_"):
            self.auto_exposure_last_adjust_s = {role: 0.0 for role in STEREO_CAMERA_ROLES}
            return
        if group != "Camera" or self.camera_manager is None:
            return
        role_methods = {
            "initial_exposure_camera1": (CameraRole.CAM1, "set_exposure"),
            "gain_camera1": (CameraRole.CAM1, "set_gain"),
            "initial_exposure_camera2": (CameraRole.CAM2, "set_exposure"),
            "gain_camera2": (CameraRole.CAM2, "set_gain"),
        }
        targets: list[tuple[CameraDevice, str]] = []
        if key in role_methods:
            # Online 使用记忆曝光，修改初始值只影响下次回退及手动/标定模式。
            if key.startswith("initial_exposure_") and self.stream_owner == "online":
                return
            role, method_name = role_methods[key]
            camera = self.camera_manager.get_by_role(role)
            if camera is not None:
                targets.append((camera, method_name))
        elif key in {"offline_crop_roi", "online_crop_roi"}:
            active_key = (
                "online_crop_roi"
                if self.stream_owner == "online"
                else "offline_crop_roi"
            )
            if key != active_key:
                return
            for camera in self.camera_manager.get_all():
                if not camera.is_open:
                    continue
                try:
                    self._apply_camera_roi(camera, getattr(self.settings, active_key))
                except Exception as exc:
                    signals.status.emit("Camera", "NG", "error", f"Apply {key} failed: {exc}")
            return
        elif key.startswith("auto_exposure_"):
            self.auto_exposure_last_adjust_s = {role: 0.0 for role in STEREO_CAMERA_ROLES}
            return

        for camera, method_name in targets:
            if not camera.is_open:
                continue
            try:
                getattr(camera, method_name)(value)
            except Exception as exc:
                signals.status.emit("Camera", "NG", "error", f"Apply {key} failed: {exc}")

    def open_current_camera_if_needed(self) -> bool:
        """Ensure the selected camera is open before manual capture."""
        cam = self.current_camera
        if not cam:
            signals.status.emit("Camera", "NG", "error", "No camera selected")
            return False
        if not cam.is_open:
            cam.open()
            self.apply_camera_params(cam)
        return True

    def start_online_cameras(self) -> None:
        self._start_stereo_cameras("online")

    def _camera_state_store(self) -> CameraStateStore:
        return CameraStateStore(config_manager.path.parent / "camera_state.json")

    def _initial_online_exposures(self) -> dict[CameraRole, float]:
        lower, upper = sorted(
            (
                float(self.settings.auto_exposure_min_us),
                float(self.settings.auto_exposure_max_us),
            )
        )
        exposures: dict[CameraRole, float] = {}
        for role, (exposure_attr, _gain_attr) in self.ROLE_PARAM_ATTRS.items():
            exposures[role] = float(
                np.clip(float(getattr(self.settings, exposure_attr)), lower, upper)
            )
        return exposures

    def _load_online_exposures(self) -> dict[CameraRole, float]:
        defaults = self._initial_online_exposures()
        keyed_defaults = {
            self.ROLE_STATE_KEYS[role]: exposure
            for role, exposure in defaults.items()
        }
        store = self._camera_state_store()
        try:
            loaded = store.load_exposures(
                keyed_defaults,
                self.settings.auto_exposure_min_us,
                self.settings.auto_exposure_max_us,
            )
        except ValueError as exc:
            signals.status.emit(
                "Camera",
                "NG",
                "warning",
                f"Camera exposure state invalid, using initial values: {exc}",
            )
            self.online_exposure_us = defaults
            return defaults.copy()

        self.online_exposure_us = {
            role: loaded[self.ROLE_STATE_KEYS[role]]
            for role in STEREO_CAMERA_ROLES
        }
        return self.online_exposure_us.copy()

    def _remember_online_exposure(self, role: CameraRole, exposure_us: float) -> None:
        # 两台相机的 SDK 回调可能并发执行，状态更新和原子写入必须串行。
        with self.camera_state_lock:
            self.online_exposure_us[role] = float(exposure_us)
            self._persist_online_exposures_locked()

    def _save_open_online_exposures(self) -> None:
        if not self.camera_manager:
            return
        with self.camera_state_lock:
            for role in STEREO_CAMERA_ROLES:
                cam = self.camera_manager.get_by_role(role)
                if cam is None or not cam.is_open:
                    continue
                self.online_exposure_us[role] = self._camera_exposure_for_display(cam)
            self._persist_online_exposures_locked()

    def _persist_online_exposures_locked(self) -> None:
        values = {
            self.ROLE_STATE_KEYS[role]: self.online_exposure_us[role]
            for role in STEREO_CAMERA_ROLES
            if role in self.online_exposure_us
        }
        if not values:
            return
        try:
            self._camera_state_store().save_exposures(values)
        except (OSError, TypeError, ValueError) as exc:
            signals.status.emit(
                "Camera",
                "NG",
                "warning",
                f"Save camera exposure state failed: {exc}",
            )

    def _start_stereo_cameras(self, owner: str) -> None:
        """Open both cameras for one named page and reject competing owners."""
        if self.stream_owner is not None:
            if self.stream_owner == owner:
                self._emit_stereo_started(owner)
            else:
                self._emit_stereo_failed(owner, f"Cameras are in use by {self.stream_owner}")
            return
        try:
            if not self.camera_manager:
                self.refresh_camera_list()

            cameras = {
                role: self.camera_manager.get_by_role(role)
                for role in STEREO_CAMERA_ROLES
            }
            missing = [role.value for role, cam in cameras.items() if cam is None]
            if missing:
                raise RuntimeError(f"Missing camera user id: {', '.join(missing)}")

            capture_roi = (
                self.settings.online_crop_roi
                if owner == "online"
                else self.settings.offline_crop_roi
            )
            online_exposures = self._load_online_exposures() if owner == "online" else {}
            for cam in cameras.values():
                if cam.is_streaming:
                    cam.stop_stream()
                if not cam.is_open:
                    cam.open()
                self.apply_camera_params(
                    cam,
                    capture_roi,
                    exposure_us=online_exposures.get(cam.role),
                )
                if owner == "online":
                    cam.set_trigger_source(0)
                    cam.set_trigger_mode(1)
                else:
                    cam.set_trigger_mode(0)
                cam.start_stream(lambda raw_image, cam=cam: self.on_captured(cam, raw_image))

            self.stream_owner = owner
            self._set_stereo_camera_ui(True)
            signals.status.emit("Camera", "OK", "info", f"{owner} cameras started: CAM1, CAM2")
            self._emit_stereo_started(owner)
        except Exception as exc:
            if self.camera_manager:
                self._close_all_cameras(f"{owner} camera start cleanup")
            self.stream_owner = None
            self._set_stereo_camera_ui(False)
            signals.status.emit("Camera", "NG", "error", f"{owner} camera start failed: {exc}")
            self._emit_stereo_failed(owner, str(exc))

    def stop_online_cameras(self) -> None:
        self._stop_stereo_cameras("online")

    def trigger_online_cameras(self) -> None:
        if self.stream_owner != "online" or not self.camera_manager:
            return
        for role in STEREO_CAMERA_ROLES:
            try:
                cam = self.camera_manager.get_by_role(role)
                if cam is None or not cam.is_streaming:
                    raise RuntimeError(f"{role.name} is not ready for software trigger")
                cam.soft_trigger()
            except Exception as exc:
                # 双目测量缺少任一画面都无效，通知 Home 停止 Online 并切回 Offline。
                message = f"{role.name} software trigger failed: {exc}"
                signals.status.emit("Camera", "NG", "error", message)
                signals.online_camera_failed.emit(message)
                return

    def _close_all_cameras(self, context: str) -> list[str]:
        if not self.camera_manager:
            return []
        errors = self.camera_manager.close_all()
        if errors:
            signals.status.emit(
                "Camera",
                "NG",
                "warning",
                f"{context}: {'; '.join(errors)}",
            )
        return errors

    def _stop_stereo_cameras(self, owner: str) -> None:
        if self.stream_owner != owner:
            return
        if owner == "online":
            self._save_open_online_exposures()
        if self.camera_manager:
            close_errors = self._close_all_cameras(f"{owner} camera stop cleanup")
        else:
            close_errors = []
        self.stream_owner = None
        self._set_stereo_camera_ui(False)
        if not close_errors:
            signals.status.emit("Camera", "OK", "info", f"{owner} cameras stopped")
        signals.online_camera_stopped.emit()

    @staticmethod
    def _emit_stereo_started(owner: str) -> None:
        signals.online_camera_started.emit()

    @staticmethod
    def _emit_stereo_failed(owner: str, message: str) -> None:
        signals.online_camera_failed.emit(message)

    def _set_stereo_camera_ui(self, active: bool) -> None:
        self.ui.btnOrgOpen.setEnabled(not active)
        self.ui.combCameraList.setEnabled(not active)
        self.ui.btnRefresh.setEnabled(not active)
        self.ui.btnCamON.setEnabled(not active)
        self._set_manual_controls_enabled(False)
        if self.current_camera:
            self._refresh_camera_ui(self.current_camera)

    def on_open_camera(self):
        if self.stream_owner is not None:
            signals.status.emit("Camera", "NG", "error", f"Cameras are in use by {self.stream_owner}")
            return
        # self.current_camera = self.camera_manager.get_by_role(CameraRole.IN)
        cam = self.current_camera
        if not cam:
            signals.status.emit("Camera", "NG", "error", "No camera selected")
            return

        try:
            if not cam.is_open:
                cam.open()
                self.apply_camera_params(cam)
            else:
                cam.close()
        except Exception as exc:
            signals.status.emit("Camera", "NG", "error", f"{cam.role.name} open/close failed: {exc}")
            self.ui.btnCamON.blockSignals(True)
            self.ui.btnCamON.setChecked(cam.is_open)
            self.ui.btnCamON.blockSignals(False)
            return

        self._refresh_camera_ui(cam)

    def on_start_snap(self):
        if self.stream_owner is not None:
            signals.status.emit("Camera", "NG", "error", f"Cameras are in use by {self.stream_owner}")
            return
        cam = self.current_camera
        if not cam:
            signals.status.emit("Camera", "NG", "error", "No camera selected")
            return

        try:
            if not self.open_current_camera_if_needed():
                return
            if not cam.is_streaming:
                cam.start_stream(lambda raw_image, cam=cam: self.on_captured(cam, raw_image))
            else:
                cam.stop_stream()
        except Exception as exc:
            signals.status.emit("Camera", "NG", "error", f"{cam.role.name} stream failed: {exc}")

        self._refresh_camera_ui(cam)
        return

    def on_captured(self, cam: CameraDevice, raw_image):
        # Camera callbacks run on a worker thread; do not touch Qt widgets here.
        if raw_image.get_status() == gx.GxFrameStatusList.INCOMPLETE:
            print("Image capture incomplete")
            signals.status.emit("Camera", "NG", "error", "Image capture incomplete")
            return

        image = self._raw_frame_to_array(raw_image)
        if image is None:
            signals.status.emit("Camera", "NG", "error", "Image capture empty")
            return

        # Copy the SDK buffer before sending it across threads.
        frame = np.asarray(image).copy()
        try:
            self._adjust_auto_exposure(cam, frame)
        except Exception as exc:
            signals.status.emit("Camera", "NG", "error", f"{cam.role.name} auto exposure failed: {exc}")
        signals.camera_exposure_changed.emit(cam.role.value, self._camera_exposure_for_display(cam))
        if self.stream_owner is None:
            self.frame_captured.emit(frame)
        signals.camera_frame_captured.emit(cam.role.value, frame)

    @staticmethod
    def _camera_exposure_for_display(cam: CameraDevice) -> float:
        try:
            exposure = cam.get_exposure()
        except Exception:
            exposure = None
        if exposure is None:
            exposure = cam.exposure
        return float(exposure)

    def _adjust_auto_exposure(self, cam: CameraDevice, frame: np.ndarray) -> None:
        if self.stream_owner != "online":
            return
        if self.stream_owner == "online" and self.online_stage not in {
            MeasurementStage.IDLE,
            MeasurementStage.NECK,
        }:
            return
        if not self.settings.auto_exposure_enabled:
            return
        roi = self._auto_exposure_roi(cam.role)
        if roi is None:
            return

        now = time.monotonic()
        interval_s = max(float(self.settings.auto_exposure_interval_ms), 50.0) / 1000.0
        if now - self.auto_exposure_last_adjust_s.get(cam.role, 0.0) < interval_s:
            return

        mean_gray = self._roi_gray_mean(frame, roi)
        if mean_gray is None:
            return

        target = max(float(self.settings.auto_exposure_target), 1.0)
        error = target - mean_gray
        if abs(error) <= float(self.settings.auto_exposure_deadband):
            self.auto_exposure_last_adjust_s[cam.role] = now
            return

        current_exposure = cam.get_exposure()
        if current_exposure is None:
            current_exposure = cam.exposure
        current_exposure = float(current_exposure)
        gain = max(float(self.settings.auto_exposure_gain), 0.0)
        next_exposure = current_exposure * (1.0 + gain * error / target)

        lower = float(self.settings.auto_exposure_min_us)
        upper = float(self.settings.auto_exposure_max_us)
        if upper < lower:
            lower, upper = upper, lower
        next_exposure = float(np.clip(next_exposure, lower, upper))
        if abs(next_exposure - current_exposure) < 1.0:
            self.auto_exposure_last_adjust_s[cam.role] = now
            return

        cam.set_exposure(next_exposure)
        if self.stream_owner == "online":
            self._remember_online_exposure(cam.role, next_exposure)
        self.auto_exposure_last_adjust_s[cam.role] = now

    @staticmethod
    def _roi_gray_mean(frame: np.ndarray, roi: RoiSettings) -> float | None:
        if frame is None or frame.size == 0:
            return None
        height, width = frame.shape[:2]
        x0 = int(np.clip(roi.x, 0, width))
        y0 = int(np.clip(roi.y, 0, height))
        x1 = int(np.clip(roi.x + roi.width, 0, width))
        y1 = int(np.clip(roi.y + roi.height, 0, height))
        if x1 <= x0 or y1 <= y0:
            return None
        patch = frame[y0:y1, x0:x1]
        if patch.ndim == 3:
            if patch.shape[2] == 4:
                patch = cv2.cvtColor(patch, cv2.COLOR_BGRA2GRAY)
            else:
                patch = cv2.cvtColor(patch, cv2.COLOR_BGR2GRAY)
        return float(np.mean(patch))

    def _auto_exposure_roi(self, role: CameraRole) -> RoiSettings | None:
        if self.stream_owner == "online":
            roi_attr = self.AUTO_EXPOSURE_ROI_ATTRS.get(role)
            return getattr(self.measurement_settings, roi_attr) if roi_attr else None
        return None

    @staticmethod
    def _raw_frame_to_array(raw_image):
        """Convert SDK raw frames to 8-bit arrays for display and processing."""
        numpy_image = raw_image.get_numpy_array()
        if numpy_image is None:
            return None
        if numpy_image.dtype == np.uint16:
            max_value = float(numpy_image.max())
            alpha = 255.0 / max(max_value, 1.0)
            return cv2.convertScaleAbs(numpy_image, alpha=alpha)
        return numpy_image

    def _display_captured_frame(self, img):
        self.orgImg = img
        self._show_image(self.ui.orgGraphicsView, img)
        self._run_pipeline_automatically()

    def _on_status(self, device_type: str, status: str, _msg_type: str, _message: str):
        if status == "NG":
            pixmap = QPixmap(self.image_off)
        else:
            pixmap = QPixmap(self.image_on)
        if device_type=="Camera":
            self.ui.lblStatus.setPixmap(pixmap)

    def _select_camera(self, index):
        if index < 0 or not self.camera_manager:
            self.current_camera = None
            self._set_manual_controls_enabled(False)
            return
        role = self.ui.combCameraList.itemData(index)
        cam = self.camera_manager.get_by_role(role)
        if not cam:
            return
        self.current_camera = cam
        self._refresh_camera_ui(cam)

    def _set_manual_controls_enabled(self, enabled: bool) -> None:
        for control in (
            self.ui.btnStartSnap,
            self.ui.combTrigMode,
            self.ui.combTrigSource,
            self.ui.btnSoftTrigger,
            self.ui.combTrigEdge,
            self.ui.edtExposure,
            self.ui.edtGain,
            self.ui.edtWidth,
            self.ui.edtHeight,
            self.ui.edtOffsetX,
            self.ui.edtOffsetY,
        ):
            control.setEnabled(enabled)
                    
    def _refresh_camera_ui(self, cam: CameraDevice) -> None:
        is_open = bool(cam and cam.is_open)
        is_streaming = bool(cam and cam.is_streaming)
        self.ui.btnCamON.blockSignals(True)
        self.ui.btnCamON.setChecked(is_open)
        self.ui.btnCamON.blockSignals(False)
        self.ui.btnStartSnap.blockSignals(True)
        self.ui.btnStartSnap.setChecked(is_streaming)
        self.ui.btnStartSnap.blockSignals(False)

        self._set_manual_controls_enabled(cam.is_open and self.stream_owner is None)
        self.ui.btnCamON.setStyleSheet(
            Settings.BUTTON_ON if cam.is_open else Settings.BUTTON_OFF
        )
        self.ui.btnStartSnap.setStyleSheet(
            Settings.BUTTON_ON if cam.is_streaming else Settings.BUTTON_OFF
        )

        if cam.is_open:
            current_mode = cam.get_trigger_mode()
            self._sync_combo(
                self.ui.combTrigMode,
                cam.get_trigger_mode_range(),
                current_mode,
            )
            self._sync_combo(
                self.ui.combTrigSource,
                cam.get_trigger_source_range(),
                cam.get_trigger_source(),
            )
            self._sync_combo(
                self.ui.combTrigEdge,
                cam.get_trigger_edge_range(),
                cam.get_trigger_edge(),
            )
            self.ui.btnSoftTrigger.setEnabled(
                bool(current_mode and current_mode[1] == "On" and self.stream_owner is None)
            )

            exposure_time = cam.get_exposure()
            self.ui.lblExposure.setText(f"Exposure({exposure_time} us)")
            self.ui.edtExposure.setText(str(exposure_time))

            gain = cam.get_gain()
            self.ui.lblGain.setText(f"Gain({gain})")
            self.ui.edtGain.setText(str(gain))

            width = cam.get_width()
            self.ui.lblWidth.setText(f"Width({width})")
            self.ui.edtWidth.setText(str(width))
            height = cam.get_height()
            self.ui.lblHeight.setText(f"Height({height})")
            self.ui.edtHeight.setText(str(height))
            offsetX = cam.get_offsetX()
            self.ui.lblOffsetX.setText(f"OffsetX({offsetX})")
            self.ui.edtOffsetX.setText(str(offsetX))
            offsetY = cam.get_offsetY()
            self.ui.lblOffsetY.setText(f"OffsetY({offsetY})")     
            self.ui.edtOffsetY.setText(str(offsetY))

    @staticmethod
    def _sync_combo(combo, options, current) -> None:
        combo.blockSignals(True)
        combo.clear()
        for label, value in (options or {}).items():
            combo.addItem(label, value)
        current_value = current[0] if current else None
        index = combo.findData(current_value)
        if index >= 0:
            combo.setCurrentIndex(index)
        combo.blockSignals(False)

    def on_soft_trigger(self):
        if not self.current_camera:
            signals.status.emit("Camera", "NG", "error", "No camera selected")
            return
        try:
            self.current_camera.soft_trigger()
        except Exception as exc:
            signals.status.emit("Camera", "NG", "error", f"{self.current_camera.role.name} soft trigger failed: {exc}")

    def on_trigger_mode_changed(self, mode):
        if not self.current_camera or mode is None:
            return
        self.current_camera.set_trigger_mode(mode)
        if self.ui.combTrigMode.currentText() == "Off":
            self.ui.btnSoftTrigger.setEnabled(False)

        elif self.ui.combTrigMode.currentText() == "On" and self.stream_owner is None:
            self.ui.btnSoftTrigger.setEnabled(True)

    def on_set_exposure(self):
        if not self.current_camera:
            return
        self.current_camera.set_exposure(float(self.ui.edtExposure.text()))
        self.ui.lblExposure.clear()
        exposure_time = self.current_camera.get_exposure()
        self.ui.lblExposure.setText(f"Exposure({exposure_time} us)")

    def on_set_gain(self):
        if not self.current_camera:
            return
        self.current_camera.set_gain(float(self.ui.edtGain.text()))
        self.ui.lblGain.clear()
        gain = self.current_camera.get_gain()
        self.ui.lblGain.setText(f"Gain({gain})")

    def on_set_width(self):
        if not self.current_camera:
            return
        self.current_camera.set_width(int(self.ui.edtWidth.text()))
        self.ui.lblWidth.clear()
        width = self.current_camera.get_width()
        self.ui.lblWidth.setText(f"Width({width})")

    def on_set_height(self):
        if not self.current_camera:
            return
        self.current_camera.set_height(int(self.ui.edtHeight.text()))
        self.ui.lblHeight.clear()
        height = self.current_camera.get_height()
        self.ui.lblHeight.setText(f"Height({height})")

    def on_set_offsetX(self):
        if not self.current_camera:
            return
        self.current_camera.set_offsetX(int(self.ui.edtOffsetX.text()))
        self.ui.lblOffsetX.clear()
        offsetX = self.current_camera.get_offsetX()
        self.ui.lblOffsetX.setText(f"OffsetX({offsetX})")

    def on_set_offsetY(self):
        if not self.current_camera:
            return
        self.current_camera.set_offsetY(int(self.ui.edtOffsetY.text()))
        self.ui.lblOffsetY.clear()
        offsetY = self.current_camera.get_offsetY()
        self.ui.lblOffsetY.setText(f"OffsetY({offsetY})")     

    def on_trigger_source(self, source):
        if not self.current_camera or source is None:
            return
        self.current_camera.set_trigger_source(source)

    def on_trigger_falling_edge(self, trigger_edge):
        if not self.current_camera or trigger_edge is None:
            return
        self.current_camera.set_trigger_edge(trigger_edge)

    def _show_image(self, view: CustomGraphicsView, data, fit: bool = False) -> None:

        if isinstance(data, np.ndarray):
            pixmap = self._cv_to_pixmap(data)
            view.show_pixmap(pixmap)
            if fit:
                view.fit_in_view()
        elif isinstance(data, str):
            view.set_text(data)
            if fit:
                view.full_fill()
        else:
            view.set_text("Cannot load data")
            if fit:
                view.full_fill()
  
    def _update_view_info(self, view_id, x, y, gray):
        if view_id == 1:
            self.ui.lblOrgInfo.setText(f"View 1 - Pos: ({x}, {y}) | Value: {gray}")
        elif view_id == 2:
            self.ui.lblTransformedInfo.setText(f"View 2 - Pos: ({x}, {y}) | Value: {gray}")       

    @staticmethod
    def _cv_to_pixmap(cv_img: np.ndarray):
        if cv_img is None:
            return QPixmap()
        # Ensure contiguous image data.
        if not cv_img.flags['C_CONTIGUOUS']:
            cv_img = np.ascontiguousarray(cv_img)

        height, width = cv_img.shape[:2]

        if len(cv_img.shape) == 2:
            # Grayscale image.
            qimg = QImage(cv_img.data, width, height, width, QImage.Format_Grayscale8)
        else:
            # BGR to RGB.
            rgb_image = cv2.cvtColor(cv_img, cv2.COLOR_BGR2RGB)
            bytes_per_line = 3 * width
            qimg = QImage(rgb_image.data, width, height, bytes_per_line, QImage.Format_RGB888)

        return QPixmap.fromImage(qimg)

    def _run_pipeline(self):
        result = self.service.run(self.orgImg)
        self._show_image(self.ui.transGraphicsView, result, True)

    def _run_pipeline_automatically(self):
        result = self.service.run(self.orgImg)
        self._show_image(self.ui.transGraphicsView, result)

    def _on_pipeline_selection_changed(self):
        items = self.scene.selectedItems()

        if not items:
            return

        item = items[0]

        # Only handle NodeItem.
        if hasattr(item, "node"):
            node = item.node
            self._show_image(self.ui.transGraphicsView, node.output, True)

    def _save_pipeline(self):
        self.service.save_graph(self.scene, "graph.json")

    def _load_pipeline(self):
        self.service.load_graph(self.scene, "graph.json")


    def closeEvent(self, event):
        if self.stream_owner == "online":
            self._save_open_online_exposures()
        if self.camera_manager:
            self._close_all_cameras("application close cleanup")
        event.accept()
