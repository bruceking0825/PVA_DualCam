from __future__ import annotations

import threading
from dataclasses import replace

from PySide6.QtCore import QThread, Signal

from measurement_core import (
    MeasurementEngine,
    MeasurementValues,
    MeasurementStage,
    StateStore,
    StereoFramePair,
)
from measurement_core.plc import OpcUaGateway, PlcSettings


class MeasurementWorker(QThread):
    """仅保留最新一组双目帧，避免算法积压拖慢界面。"""

    result_ready = Signal(object)
    failed = Signal(str)

    def __init__(self, engine: MeasurementEngine, state_store: StateStore, parent=None):
        super().__init__(parent)
        self.engine = engine
        self.state_store = state_store
        self._condition = threading.Condition()
        self._pending: tuple[StereoFramePair, MeasurementStage] | None = None
        self._stopping = False

    def submit(self, pair: StereoFramePair, stage: MeasurementStage) -> None:
        with self._condition:
            self._pending = pair, stage
            self._condition.notify()

    def run(self) -> None:
        while True:
            with self._condition:
                while self._pending is None and not self._stopping:
                    self._condition.wait()
                if self._stopping:
                    return
                pair, stage = self._pending
                self._pending = None
            try:
                result = self.engine.process(pair, stage)
                if result.valid:
                    self.state_store.save(self.engine.state)
                result.values = replace(result.values)
                self.result_ready.emit(result)
            except Exception as exc:
                self.failed.emit(str(exc))

    def stop(self) -> None:
        with self._condition:
            self._stopping = True
            self._pending = None
            self._condition.notify_all()


class OpcUaWorker(QThread):
    controls_changed = Signal(int, bool)
    connection_changed = Signal(bool, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.settings = PlcSettings()
        self._stop_event = threading.Event()
        self._output_lock = threading.Lock()
        self._pending_values: MeasurementValues | None = None

    def queue_values(self, values: MeasurementValues) -> None:
        with self._output_lock:
            self._pending_values = replace(values)

    def run(self) -> None:
        last_controls = None
        while not self._stop_event.is_set():
            gateway = OpcUaGateway(self.settings)
            try:
                gateway.connect()
                self.connection_changed.emit(True, "PLC connected")
                while not self._stop_event.wait(0.1):
                    controls = gateway.read_controls()
                    current = controls.stage_value, controls.shoulder_transition
                    if current != last_controls:
                        last_controls = current
                        self.controls_changed.emit(*current)
                    with self._output_lock:
                        values, self._pending_values = self._pending_values, None
                    if values is not None:
                        gateway.write_values(values)
            except Exception as exc:
                self.connection_changed.emit(False, str(exc))
            finally:
                gateway.disconnect()
            self._stop_event.wait(max(self.settings.reconnect_ms, 100) / 1000.0)

    def stop(self) -> None:
        self._stop_event.set()
