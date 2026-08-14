from __future__ import annotations

import re

import numpy as np

from measurement_core import MeasurementStage


_LABEL_PHRASES = (
    ("Column Maximum Maximum", "Col Peak Max"),
    ("Column Maximum P90", "Col Peak P90"),
    ("Column Strengths Maximum", "Col Strength Max"),
    ("Column Strengths P90", "Col Strength P90"),
    ("Left Side Candidate Count", "Left Cand Count"),
    ("Right Side Candidate Count", "Right Cand Count"),
    ("Search Bottom Ratio", "Search Y1 Ratio"),
    ("Search Top Ratio", "Search Y0 Ratio"),
    ("Search Start Y", "Search Y0"),
    ("Search Stop Y", "Search Y1"),
)

_LABEL_WORDS = {
    "Boundaries": "Bounds",
    "Boundary": "Bound",
    "Brightness": "Bright",
    "Candidate": "Cand",
    "Candidates": "Cands",
    "Column": "Col",
    "Columns": "Cols",
    "Height": "H",
    "Horizontal": "Horiz",
    "Image": "Img",
    "Maximum": "Max",
    "Minimum": "Min",
    "Point": "Pt",
    "Points": "Pts",
    "Previous": "Prev",
    "Residual": "Resid",
    "Strengths": "Strength",
    "Threshold": "Thresh",
    "Tracking": "Track",
    "Vertical": "Vert",
}


def diagnostic_group(key: str) -> str:
    normalized = str(key).lower()
    if "camera1" in normalized:
        return "Camera 1"
    if "camera2" in normalized:
        return "Camera 2"
    if any(token in normalized for token in ("pair_offset", "raw_")):
        return "Stereo"
    if any(token in normalized for token in ("cycle_", "source", "tracking_active")):
        return "Runtime"
    return "Algorithm"


def diagnostic_label_and_unit(
    key: str,
    stage: MeasurementStage | None = None,
) -> tuple[str, str]:
    normalized = str(key)
    unit = ""
    for suffix, candidate_unit in (
        ("_mm", "mm"),
        ("_px", "px"),
        ("_ms", "ms"),
        ("_deg", "deg"),
    ):
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
            unit = candidate_unit
            break
    label = re.sub(r"\s+Camera[12]\b", "", normalized.replace("_", " ").title())
    label = _shorten_label(label)
    if stage == MeasurementStage.CROWN and label.startswith("Crown "):
        label = label.removeprefix("Crown ")
    elif stage == MeasurementStage.BODY and label.startswith("Body "):
        label = label.removeprefix("Body ")
    return label, unit


def _shorten_label(label: str) -> str:
    for source, replacement in _LABEL_PHRASES:
        label = label.replace(source, replacement)
    return " ".join(_LABEL_WORDS.get(word, word) for word in label.split())


def format_diagnostic_value(value: object) -> str:
    if value is None:
        return "NA"
    if isinstance(value, (bool, np.bool_)):
        return "Yes" if bool(value) else "No"
    if isinstance(value, (int, np.integer)):
        return str(int(value))
    if isinstance(value, (float, np.floating)):
        return "NA" if not np.isfinite(value) else f"{float(value):.3f}"
    if isinstance(value, (list, tuple, np.ndarray)):
        array = np.asarray(value)
        if np.issubdtype(array.dtype, np.number):
            flattened = array.reshape(-1)
            values = [f"{float(item):.3f}" for item in flattened[:8]]
            if flattened.size > 8:
                values.append("...")
            return "[" + ", ".join(values) + "]"
    return str(value)
