from PySide6.QtCore import QObject, Signal


class AppSignals(QObject):
    status = Signal(str, str, str, str)
    online_camera_start_requested = Signal()
    online_camera_stop_requested = Signal()
    online_camera_trigger_requested = Signal()
    online_stage_changed = Signal(int)
    online_camera_started = Signal()
    online_camera_stopped = Signal()
    online_camera_failed = Signal(str)
    camera_frame_captured = Signal(str, object)
    camera_exposure_changed = Signal(str, float)
    app_close = Signal()

signals = AppSignals()
