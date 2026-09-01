from __future__ import annotations

from dataclasses import asdict, dataclass, field
from enum import IntEnum
from typing import Any

import numpy as np


class MeasurementStage(IntEnum):
    IDLE = 0
    NECK = 1
    CROWN = 2
    ENDCONE = 3
    BODY = 4

    @classmethod
    def from_value(cls, value: int | float | str) -> "MeasurementStage":
        try:
            numeric = int(float(value))
        except (TypeError, ValueError):
            return cls.IDLE
        if numeric == 0:
            return cls.IDLE
        if numeric == 1:
            return cls.NECK
        if numeric == 2:
            return cls.CROWN
        if numeric == 3:
            return cls.ENDCONE
        if numeric == 4:
            return cls.BODY
        return cls.IDLE


@dataclass
class StereoFramePair:
    camera1: np.ndarray
    camera2: np.ndarray
    timestamp_s: float
    source: str
    origin1_xy: tuple[int, int] = (0, 0)
    origin2_xy: tuple[int, int] = (0, 0)


@dataclass
class MeasurementValues:
    diameter_mm: float | None = None

    def complete(self) -> bool:
        values = asdict(self).values()
        return all(value is not None and np.isfinite(value) for value in values)


@dataclass
class MeasurementState:
    values: MeasurementValues = field(default_factory=MeasurementValues)
    filtered_light: list[float] = field(default_factory=lambda: [0.0, 0.0])
    neck_centers_px: list[list[float]] | None = None
    neck_x_spans: list[list[int]] | None = None
    crown_boundary_points_px: list[list[float]] | None = None
    body_centers_px: list[list[float]] | None = None
    body_boundary_points_px: list[list[float]] | None = None
    mm_per_pixel: float | None = None
    valid_neck: bool = False

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "MeasurementState":
        values_data = data.get("values", {})
        allowed_values = MeasurementValues.__dataclass_fields__
        values = MeasurementValues(**{key: value for key, value in values_data.items() if key in allowed_values})
        allowed = cls.__dataclass_fields__
        kwargs = {key: value for key, value in data.items() if key in allowed and key != "values"}
        return cls(values=values, **kwargs)


@dataclass
class MeasurementResult:
    valid: bool
    stage: MeasurementStage
    values: MeasurementValues
    diagnostics: dict[str, Any]
    preview1: np.ndarray
    preview2: np.ndarray
    overlay1: list[dict[str, Any]]
    overlay2: list[dict[str, Any]]
    timestamp_s: float
    message: str = ""
