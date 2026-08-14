from __future__ import annotations

import json
import math
import os
from pathlib import Path
from typing import Mapping


CAMERA_STATE_VERSION = 1


class CameraStateStore:
    """保存程序自动调节得到的相机运行状态。"""

    def __init__(self, path: Path) -> None:
        self.path = Path(path)

    def load_exposures(
        self,
        defaults: Mapping[str, float],
        minimum_us: float,
        maximum_us: float,
    ) -> dict[str, float]:
        lower, upper = sorted((float(minimum_us), float(maximum_us)))
        fallback = {
            key: _clamp_exposure(value, lower, upper)
            for key, value in defaults.items()
        }
        if not self.path.exists():
            return fallback

        try:
            payload = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError(f"Cannot read camera state: {exc}") from exc

        values = payload.get("exposure_us") if isinstance(payload, dict) else None
        if not isinstance(values, dict):
            raise ValueError("Camera state does not contain exposure_us")

        result = fallback.copy()
        for key in result:
            if key not in values:
                continue
            try:
                result[key] = _clamp_exposure(values[key], lower, upper)
            except (TypeError, ValueError):
                continue
        return result

    def save_exposures(self, exposures: Mapping[str, float]) -> None:
        values = {
            str(key): float(value)
            for key, value in exposures.items()
            if math.isfinite(float(value))
        }
        payload = {
            "version": CAMERA_STATE_VERSION,
            "exposure_us": values,
        }
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temp_path = self.path.with_suffix(self.path.suffix + ".tmp")
        temp_path.write_text(
            json.dumps(payload, indent=2, sort_keys=True),
            encoding="utf-8",
        )
        os.replace(temp_path, self.path)


def _clamp_exposure(value: object, lower: float, upper: float) -> float:
    exposure = float(value)
    if not math.isfinite(exposure):
        raise ValueError("Exposure must be finite")
    return min(max(exposure, lower), upper)
