from __future__ import annotations

import configparser
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from config_models import CameraSettings, MeasurementConfig

from .utils import IniEntry

class ConfigManager(QObject):
    changed = Signal(str, str, object)
    batch_changed = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.path = default_config_path()
        self.camera = CameraSettings()
        self.measurement = MeasurementConfig()

    def load(self, path: str | Path | None = None, emit_changes: bool = True) -> None:
        self.path = Path(path) if path is not None else default_config_path()
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str
        if not self.path.exists():
            raise FileNotFoundError(f"Configuration file not found: {self.path}")
        parser.read(self.path, encoding="utf-8")
        for group in parser.sections():
            for key, value in parser.items(group):
                self.set_entry(IniEntry(group, key, _clean(value)), emit=emit_changes)
        if emit_changes:
            self.batch_changed.emit()

    def set_entry(self, entry: IniEntry, emit: bool = True) -> bool:
        group, key = entry.group, entry.key
        raw_value = _clean(entry.value)
        changed = False

        if group == "Camera":
            changed = self._set_camera(key, raw_value)
        elif group == "Runtime":
            changed = self._set_runtime(key, raw_value)
        elif is_setting_group(group):
            changed = self._set_scalar(self.measurement.target_for_group(group), key, raw_value)

        if changed and emit:
            self.changed.emit(group, key, self.value(group, key))
        return changed

    def value(self, group: str, key: str):
        if group == "Camera":
            if key in {
                "offline_crop_roi",
                "online_crop_roi",
            }:
                roi = getattr(self.camera, key)
                return roi.x, roi.y, roi.width, roi.height
            return getattr(self.camera, key, None)
        if group == "Runtime":
            return getattr(self.measurement.runtime, key, None)
        if is_setting_group(group):
            target = self.measurement.target_for_group(group)
            value = getattr(target, key, None) if target is not None else None
            if key in ROI_KEYS and value is not None:
                return value.x, value.y, value.width, value.height
            return value
        return None

    def _set_camera(self, key: str, raw_value: str) -> bool:
        roi_keys = {
            "offline_crop_roi",
            "online_crop_roi",
        }
        if key not in roi_keys:
            if key == "auto_exposure_interval_ms":
                raw_value = str(max(50, int(float(raw_value))))
            if key in {"auto_exposure_min_us", "auto_exposure_max_us"}:
                raw_value = str(max(1.0, float(raw_value)))
            if key == "auto_exposure_gain":
                raw_value = str(max(0.0, float(raw_value)))
            if key == "auto_exposure_deadband":
                raw_value = str(max(0.0, float(raw_value)))
            if key == "auto_exposure_target":
                raw_value = str(float(_clip_float(float(raw_value), 1.0, 254.0)))
            return self._set_scalar(self.camera, key, raw_value)
        return self._set_roi(self.camera, key, raw_value)

    def _set_runtime(self, key: str, raw_value: str) -> bool:
        if key == "loop_interval_ms":
            raw_value = str(max(50, int(float(raw_value))))
        if key == "stereo_pair_max_delta_ms":
            raw_value = str(max(1, int(float(raw_value))))
        if key in {
            "idle_sample_interval_ms",
            "neck_sample_interval_ms",
            "crown_sample_interval_ms",
            "body_sample_interval_ms",
            "endcone_sample_interval_ms",
        }:
            raw_value = str(max(50, int(float(raw_value))))
        return self._set_scalar(self.measurement.runtime, key, raw_value)

    def _set_scalar(self, target, key: str, raw_value: str) -> bool:
        if not hasattr(target, key):
            return False
        if key in ROI_KEYS:
            return self._set_roi(target, key, raw_value)
        current = getattr(target, key)
        converted = self._convert(raw_value, current)
        return self._assign(target, key, converted)

    def _set_roi(self, target, key: str, raw_value: str) -> bool:
        roi = getattr(target, key)
        updated = self._parse_roi(raw_value)
        current = (roi.x, roi.y, roi.width, roi.height)
        if updated == current:
            return False
        roi.x, roi.y, roi.width, roi.height = updated
        return True

    @staticmethod
    def _assign(target, key: str, value) -> bool:
        if getattr(target, key) == value:
            return False
        setattr(target, key, value)
        return True

    def _convert(self, raw_value: str, current):
        if isinstance(current, bool):
            normalized = raw_value.lower()
            if normalized in {"1", "true", "yes", "on"}:
                return True
            if normalized in {"0", "false", "no", "off"}:
                return False
            raise ValueError(f"invalid boolean value: {raw_value}")
        if isinstance(current, Path):
            path = Path(raw_value)
            return path if path.is_absolute() else (self.path.parent / path).resolve()
        if isinstance(current, int):
            return int(float(raw_value))
        if isinstance(current, float):
            return float(raw_value)
        if isinstance(current, str):
            return raw_value
        raise TypeError(f"unsupported configuration type: {type(current).__name__}")

    @staticmethod
    def _parse_roi(raw_value: str) -> tuple[int, int, int, int]:
        values = [part.strip() for part in raw_value.split(",")]
        if len(values) != 4:
            raise ValueError("ROI must contain x,y,width,height")
        x, y, width, height = (int(float(value)) for value in values)
        if width < 1 or height < 1:
            raise ValueError("ROI width and height must be at least 1")
        return x, y, width, height

def default_config_path() -> Path:
    direct = Path("cnf.ini")
    return direct if direct.exists() else Path("src/cnf.ini")


def _clean(value) -> str:
    text = str(value).strip()
    if text.startswith("[") and text.endswith("]"):
        return text[1:-1].strip()
    return text


def _clip_float(value: float, lower: float, upper: float) -> float:
    return min(max(value, lower), upper)


SETTING_GROUPS = {
    "Measurement",
    "Neck",
    "Crown",
    "Body",
    "Endcone",
}

ROI_KEYS = {
    "auto_exposure_roi_camera1",
    "auto_exposure_roi_camera2",
    "crown_roi_camera1",
    "crown_roi_camera2",
    "body_roi_camera1",
    "body_roi_camera2",
}


def is_setting_group(group: str) -> bool:
    return group in SETTING_GROUPS


config_manager = ConfigManager()
