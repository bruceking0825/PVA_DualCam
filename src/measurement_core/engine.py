from __future__ import annotations

from dataclasses import dataclass
from time import perf_counter
from typing import Any, Generic, TypeVar

import cv2
import numpy as np
from scipy.signal import find_peaks

from config_models import MeasurementConfig
from .models import MeasurementResult, MeasurementState, MeasurementValues, MeasurementStage, StereoFramePair


T = TypeVar("T")


@dataclass
class DetectionResult(Generic[T]):
    value: T | None
    diagnostics: dict[str, Any]
    error: str | None = None

    @classmethod
    def success(cls, value: T, diagnostics: dict[str, Any]) -> "DetectionResult[T]":
        return cls(value=value, diagnostics=diagnostics)

    @classmethod
    def failure(cls, error: str, diagnostics: dict[str, Any]) -> "DetectionResult[T]":
        return cls(value=None, diagnostics=diagnostics, error=error)


@dataclass
class EllipseDetection:
    ellipse: tuple[tuple[float, float], tuple[float, float], float]
    contour: np.ndarray
    center: np.ndarray
    lower: np.ndarray
    area: float
    contour_closed: bool = True


@dataclass
class MeniscusArcDetection:
    edge_points: np.ndarray
    curve_points: np.ndarray
    boundary_point: np.ndarray
    coefficients: np.ndarray
    seed_y: float
    fit_error_px: float
    coverage_ratio: float
    fit_strength_mean: float
    column_strengths_mean: float | None = None
    column_strengths_maximum: float | None = None


def _paired_detection_failure(
    label: str,
    result1: DetectionResult[Any],
    result2: DetectionResult[Any],
) -> str:
    details: list[str] = []
    if result1.value is None:
        details.append(f"Camera 1: {result1.error or 'unknown reason'}")
    if result2.value is None:
        details.append(f"Camera 2: {result2.error or 'unknown reason'}")
    return f"{label} not found: {'; '.join(details)}"


_NECK_CAMERA_DIAGNOSTICS = (
    "neck_gradient_p98",
    "neck_gradient_max",
    "neck_ellipse_vertex_px",
    "neck_major_axis_px",
)

_CROWN_CAMERA_DIAGNOSTICS = (
    "crown_bottom_margin_px",
    "crown_tracking_half_height_px",
    "crown_column_strengths_mean",
    "crown_column_strengths_maximum",
    "crown_minimum_strength",
    "crown_kept_column_count",
    "crown_seed_y_px",
    "crown_edge_point_count",
    "crown_residual_limit_px",
    "crown_robust_inlier_count",
    "crown_sagitta_px",
    "crown_center_px",
    "crown_boundary_px",
    "crown_edge_seed_y_px",
    "crown_edge_coverage",
    "crown_edge_fit_error_px",
    "crown_fit_strengths_mean",
)

_BODY_CAMERA_DIAGNOSTICS = (
    "body_search_start_y_px",
    "body_search_stop_y_px",
    "body_bottom_margin_px",
    "body_tracking_half_height_px",
    "body_brightness_offset",
    "body_threshold_crossing_count",
    "body_column_maximum_p90",
    "body_column_maximum_maximum",
    "body_residual_limit_px",
    "body_robust_inlier_count",
    "body_coverage_ratio",
    "body_sagitta_px",
    "body_center_px",
    "body_boundary_px",
    "body_edge_seed_y_px",
    "body_edge_coverage",
    "body_edge_fit_error_px",
    "body_fit_strength_mean",
)


def _camera_diagnostic_key(name: str, camera: int) -> str:
    for suffix in ("_mm", "_px", "_ms", "_deg"):
        if name.endswith(suffix):
            return f"{name[:-len(suffix)]}_camera{camera}{suffix}"
    return f"{name}_camera{camera}"


def _diagnostic_template(stage: MeasurementStage) -> dict[str, Any]:
    camera_fields = {
        MeasurementStage.NECK: _NECK_CAMERA_DIAGNOSTICS,
        MeasurementStage.CROWN: _CROWN_CAMERA_DIAGNOSTICS,
        MeasurementStage.BODY: _BODY_CAMERA_DIAGNOSTICS,
    }.get(stage, ())
    keys: list[str] = []
    for camera in (1, 2):
        keys.append(f"light_camera{camera}")
        keys.extend(_camera_diagnostic_key(name, camera) for name in camera_fields)

    if stage == MeasurementStage.NECK:
        keys.extend(
            (
                "raw_diameter_mm",
                "source",
                "cycle_ms",
                "neck_pixels_per_mm",
            )
        )
    elif stage == MeasurementStage.CROWN:
        keys.extend(
            (
                "source",
                "cycle_ms",
                "crown_edge_previous_tracking_active",
                "crown_edge_model",
            )
        )
    elif stage == MeasurementStage.BODY:
        keys.extend(
            (
                "source",
                "cycle_ms",
                "body_edge_previous_tracking_active",
                "body_edge_model",
            )
        )
    elif stage == MeasurementStage.ENDCONE:
        keys.extend(("boundary_y_px", "raw_diameter_mm", "source", "cycle_ms"))
    else:
        keys.extend(("source", "cycle_ms"))
    return dict.fromkeys(keys)


def _merge_camera_diagnostics(
    diagnostics: dict[str, Any],
    prefix: str,
    camera: int,
    camera_diagnostics: dict[str, Any],
) -> None:
    for key, value in camera_diagnostics.items():
        diagnostics[_camera_diagnostic_key(f"{prefix}_{key}", camera)] = value


def prepare_stereo_pair(
    camera1: np.ndarray,
    camera2: np.ndarray,
    timestamp_s: float,
    source: str,
    origin_xy: tuple[int, int],
) -> StereoFramePair:
    image1 = _to_gray_uint8(camera1)
    image2 = _to_gray_uint8(camera2)
    origin = int(origin_xy[0]), int(origin_xy[1])
    return StereoFramePair(image1, image2, timestamp_s, source, origin, origin)


class MeasurementEngine:
    def __init__(self, config: MeasurementConfig, state: MeasurementState | None = None):
        self.config = config
        self.state = state or MeasurementState()

    def process(
        self,
        pair: StereoFramePair,
        stage: MeasurementStage,
    ) -> MeasurementResult:
        processing_stage = MeasurementStage.NECK if stage == MeasurementStage.IDLE else stage
        started = perf_counter()
        gray1 = _to_gray_uint8(pair.camera1)
        gray2 = _to_gray_uint8(pair.camera2)
        preview1 = cv2.cvtColor(gray1, cv2.COLOR_GRAY2BGR)
        preview2 = cv2.cvtColor(gray2, cv2.COLOR_GRAY2BGR)
        overlay1: list[dict[str, Any]] = []
        overlay2: list[dict[str, Any]] = []
        light = [float(gray1.max()), float(gray2.max())]
        self.state.filtered_light = [
            _ema(old, raw, self.config.measurement.light_alpha)
            for old, raw in zip(self.state.filtered_light, light)
        ]
        diagnostics = _diagnostic_template(processing_stage)
        diagnostics.update(
            light_camera1=self.state.filtered_light[0],
            light_camera2=self.state.filtered_light[1],
            source=pair.source,
        )

        if not self._brightness_valid():
            low = float(self.config.measurement.brightness_min)
            high = float(self.config.measurement.brightness_max)
            message = (
                "Brightness is outside limits: "
                f"Camera 1={self.state.filtered_light[0]:.3f}, "
                f"Camera 2={self.state.filtered_light[1]:.3f}, "
                f"allowed=[{low:.3f}, {high:.3f}]"
            )
            return self._result(False, stage, pair, preview1, preview2, overlay1, overlay2, diagnostics, message, started)

        try:
            if processing_stage == MeasurementStage.NECK:
                valid, message = self._process_neck(pair, gray1, gray2, overlay1, overlay2, diagnostics)
            elif processing_stage == MeasurementStage.CROWN:
                valid, message = self._process_crown(
                    pair,
                    gray1,
                    gray2,
                    overlay1,
                    overlay2,
                    diagnostics,
                )
            elif processing_stage == MeasurementStage.BODY:
                valid, message = self._process_body(
                    pair,
                    gray1,
                    gray2,
                    overlay1,
                    overlay2,
                    diagnostics,
                )
            elif processing_stage == MeasurementStage.ENDCONE:
                valid, message = self._process_endcone(pair, gray1, gray2, overlay1, overlay2, diagnostics)
            else:
                valid, message = False, f"Unsupported stage: {stage}"
        except Exception as exc:
            valid, message = False, f"{type(exc).__name__}: {exc}"
        return self._result(valid, stage, pair, preview1, preview2, overlay1, overlay2, diagnostics, message, started)

    def _process_neck(
        self,
        pair: StereoFramePair,
        gray1: np.ndarray,
        gray2: np.ndarray,
        overlay1: list[dict[str, Any]],
        overlay2: list[dict[str, Any]],
        diagnostics: dict[str, Any],
    ) -> tuple[bool, str]:
        settings = self.config
        neck_result1 = _find_ellipse(
            gray1,
            settings.neck.neck_gradient_threshold_cam1,
            settings.neck.min_contour_area_px,
            vertical_roi=_vertical_roi_from_ratio(
                gray1.shape[0],
                settings.neck.neck_start_search_ratio,
                settings.neck.neck_stop_search_ratio,
            ),
            outer_arc_only=True,
        )
        neck_result2 = _find_ellipse(
            gray2,
            settings.neck.neck_gradient_threshold_cam2,
            settings.neck.min_contour_area_px,
            vertical_roi=_vertical_roi_from_ratio(
                gray2.shape[0],
                settings.neck.neck_start_search_ratio,
                settings.neck.neck_stop_search_ratio,
            ),
            outer_arc_only=True,
        )
        _merge_camera_diagnostics(diagnostics, "neck", 1, neck_result1.diagnostics)
        _merge_camera_diagnostics(diagnostics, "neck", 2, neck_result2.diagnostics)
        if neck_result1.value is None or neck_result2.value is None:
            return False, _paired_detection_failure(
                "Neck meniscus",
                neck_result1,
                neck_result2,
            )
        neck1 = neck_result1.value
        neck2 = neck_result2.value
        if min(len(neck1.contour), len(neck2.contour)) < settings.neck.neck_min_edge_points:
            return (
                False,
                "Not enough neck edge points: "
                f"Camera 1={len(neck1.contour)}, Camera 2={len(neck2.contour)}, "
                f"minimum={int(settings.neck.neck_min_edge_points)}",
            )
        overlay1.extend(_detection_elements(neck1, "#00FF00"))
        overlay2.extend(_detection_elements(neck2, "#00FF00"))
        diagnostics.update(
            neck_ellipse_vertex_camera1_px=neck1.lower.tolist(),
            neck_ellipse_vertex_camera2_px=neck2.lower.tolist(),
        )

        pixels_per_mm = float(settings.neck.neck_pixels_per_mm)
        if not np.isfinite(pixels_per_mm) or pixels_per_mm <= 0.0:
            return False, f"Neck pixels-per-mm must be positive: {pixels_per_mm}"
        major_axis1_px = float(max(neck1.ellipse[1]))
        major_axis2_px = float(max(neck2.ellipse[1]))
        raw_diameter = major_axis2_px / pixels_per_mm
        diagnostics.update(
            neck_major_axis_camera1_px=major_axis1_px,
            neck_major_axis_camera2_px=major_axis2_px,
            neck_pixels_per_mm=pixels_per_mm,
            raw_diameter_mm=raw_diameter,
        )
        if not (settings.measurement.diameter_min_mm < raw_diameter < settings.measurement.diameter_max_mm):
            return (
                False,
                "Neck result is outside physical limits: "
                f"diameter={raw_diameter:.3f} mm "
                f"({settings.measurement.diameter_min_mm:.3f}, {settings.measurement.diameter_max_mm:.3f})",
            )

        values = self.state.values
        values.diameter_mm = _ema_optional(values.diameter_mm, raw_diameter, settings.neck.neck_diameter_alpha)

        scale = 1.0 / pixels_per_mm
        self.state.mm_per_pixel = _ema_optional(self.state.mm_per_pixel, scale, settings.measurement.mm_per_pixel_alpha)
        self.state.neck_centers_px = [neck1.center.tolist(), neck2.center.tolist()]
        self.state.neck_x_spans = [
            _ellipse_x_span(neck1, gray1.shape[1]),
            _ellipse_x_span(neck2, gray2.shape[1]),
        ]
        self.state.crown_boundary_points_px = None
        self.state.body_centers_px = None
        self.state.body_boundary_points_px = None
        self.state.valid_neck = True
        return True, "Neck measurement updated"

    def _process_crown(
        self,
        pair: StereoFramePair,
        gray1: np.ndarray,
        gray2: np.ndarray,
        overlay1: list[dict[str, Any]],
        overlay2: list[dict[str, Any]],
        diagnostics: dict[str, Any],
    ) -> tuple[bool, str]:
        settings = self.config
        roi1, center1 = _manual_meniscus_roi(settings.crown.crown_roi_camera1, gray1.shape)
        roi2, center2 = _manual_meniscus_roi(settings.crown.crown_roi_camera2, gray2.shape)
        centers = [center1, center2]
        overlay1.append(_marker_element("cross", centers[0], 3.0, "#00FF00", 2))
        overlay2.append(_marker_element("cross", centers[1], 3.0, "#00FF00", 2))
        previous = self.state.crown_boundary_points_px
        use_previous_boundary = (
            bool(settings.crown.crown_edge_use_previous_boundary_y)
            and previous is not None
        )
        diagnostics["crown_edge_previous_tracking_active"] = use_previous_boundary
        crown_result1 = _find_meniscus_arc(
            gray1,
            roi1,
            centers[0],
            previous[0][1] if use_previous_boundary else None,
            settings,
        )
        crown_result2 = _find_meniscus_arc(
            gray2,
            roi2,
            centers[1],
            previous[1][1] if use_previous_boundary else None,
            settings,
        )
        _merge_camera_diagnostics(
            diagnostics, "crown", 1, crown_result1.diagnostics
        )
        _merge_camera_diagnostics(
            diagnostics, "crown", 2, crown_result2.diagnostics
        )
        if crown_result1.value is None or crown_result2.value is None:
            return False, _paired_detection_failure(
                "Crown meniscus curve",
                crown_result1,
                crown_result2,
            )
        crown1 = crown_result1.value
        crown2 = crown_result2.value
        if min(len(crown1.edge_points), len(crown2.edge_points)) < settings.crown.crown_min_edge_points:
            return (
                False,
                "Not enough Crown edge points: "
                f"Camera 1={len(crown1.edge_points)}, Camera 2={len(crown2.edge_points)}, "
                f"minimum={int(settings.crown.crown_min_edge_points)}",
            )
        overlay1.extend(_meniscus_arc_elements(crown1))
        overlay2.extend(_meniscus_arc_elements(crown2))

        diagnostics.update(
            crown_edge_model="maximum_negative_gradient_quadratic",
            crown_center_camera1_px=centers[0].tolist(),
            crown_center_camera2_px=centers[1].tolist(),
            crown_boundary_camera1_px=crown1.boundary_point.tolist(),
            crown_boundary_camera2_px=crown2.boundary_point.tolist(),
            crown_edge_seed_y_camera1_px=crown1.seed_y,
            crown_edge_seed_y_camera2_px=crown2.seed_y,
            crown_edge_coverage_camera1=crown1.coverage_ratio,
            crown_edge_coverage_camera2=crown2.coverage_ratio,
            crown_edge_fit_error_camera1_px=crown1.fit_error_px,
            crown_edge_fit_error_camera2_px=crown2.fit_error_px,
            crown_fit_strengths_mean_camera1=crown1.fit_strength_mean,
            crown_fit_strengths_mean_camera2=crown2.fit_strength_mean,
            crown_column_strengths_mean_camera1=crown1.column_strengths_mean,
            crown_column_strengths_mean_camera2=crown2.column_strengths_mean,
            crown_column_strengths_maximum_camera1=crown1.column_strengths_maximum,
            crown_column_strengths_maximum_camera2=crown2.column_strengths_maximum,
        )

        self.state.crown_boundary_points_px = [
            crown1.boundary_point.tolist(),
            crown2.boundary_point.tolist(),
        ]
        return True, "Crown meniscus lower vertices updated"

    def _process_body(
        self,
        pair: StereoFramePair,
        gray1: np.ndarray,
        gray2: np.ndarray,
        overlay1: list[dict[str, Any]],
        overlay2: list[dict[str, Any]],
        diagnostics: dict[str, Any],
    ) -> tuple[bool, str]:
        settings = self.config
        roi1, center1 = _manual_meniscus_roi(settings.body.body_roi_camera1, gray1.shape)
        roi2, center2 = _manual_meniscus_roi(settings.body.body_roi_camera2, gray2.shape)
        centers = [center1, center2]
        overlay1.append(_marker_element("cross", centers[0], 3.0, "#00FF00", 2))
        overlay2.append(_marker_element("cross", centers[1], 3.0, "#00FF00", 2))

        previous = self.state.body_boundary_points_px
        use_previous_boundary = (
            bool(settings.body.body_edge_use_previous_boundary_y)
            and previous is not None
        )
        diagnostics["body_edge_previous_tracking_active"] = use_previous_boundary
        body_result1 = _find_body_brightness_curve(
            gray1,
            roi1,
            centers[0],
            previous[0][1] if use_previous_boundary else None,
            settings,
            settings.body.body_brightness_offset_cam1,
        )
        body_result2 = _find_body_brightness_curve(
            gray2,
            roi2,
            centers[1],
            previous[1][1] if use_previous_boundary else None,
            settings,
            settings.body.body_brightness_offset_cam2,
        )
        _merge_camera_diagnostics(diagnostics, "body", 1, body_result1.diagnostics)
        _merge_camera_diagnostics(diagnostics, "body", 2, body_result2.diagnostics)
        if body_result1.value is None or body_result2.value is None:
            return False, _paired_detection_failure(
                "Body meniscus curve",
                body_result1,
                body_result2,
            )
        body1 = body_result1.value
        body2 = body_result2.value
        if min(len(body1.edge_points), len(body2.edge_points)) < settings.body.body_min_edge_points:
            return (
                False,
                "Not enough Body edge points: "
                f"Camera 1={len(body1.edge_points)}, Camera 2={len(body2.edge_points)}, "
                f"minimum={int(settings.body.body_min_edge_points)}",
            )
        overlay1.extend(_meniscus_arc_elements(body1))
        overlay2.extend(_meniscus_arc_elements(body2))

        diagnostics.update(
            body_edge_model="maximum_brightness_quadratic",
            body_center_camera1_px=centers[0].tolist(),
            body_center_camera2_px=centers[1].tolist(),
            body_boundary_camera1_px=body1.boundary_point.tolist(),
            body_boundary_camera2_px=body2.boundary_point.tolist(),
            body_edge_seed_y_camera1_px=body1.seed_y,
            body_edge_seed_y_camera2_px=body2.seed_y,
            body_edge_coverage_camera1=body1.coverage_ratio,
            body_edge_coverage_camera2=body2.coverage_ratio,
            body_edge_fit_error_camera1_px=body1.fit_error_px,
            body_edge_fit_error_camera2_px=body2.fit_error_px,
            body_fit_strength_mean_camera1=body1.fit_strength_mean,
            body_fit_strength_mean_camera2=body2.fit_strength_mean,
        )

        self.state.body_centers_px = [centers[0].tolist(), centers[1].tolist()]
        self.state.body_boundary_points_px = [
            body1.boundary_point.tolist(),
            body2.boundary_point.tolist(),
        ]
        return True, "Body meniscus lower vertices updated"

    def _process_endcone(
        self,
        pair: StereoFramePair,
        gray1: np.ndarray,
        gray2: np.ndarray,
        overlay1: list[dict[str, Any]],
        overlay2: list[dict[str, Any]],
        diagnostics: dict[str, Any],
    ) -> tuple[bool, str]:
        if not self.state.valid_neck or not self.state.body_centers_px or not self.state.mm_per_pixel:
            missing = []
            if not self.state.valid_neck:
                missing.append("valid_neck")
            if not self.state.body_centers_px:
                missing.append("body_centers_px")
            if not self.state.mm_per_pixel:
                missing.append("mm_per_pixel")
            return False, f"Endcone mode requires valid neck and body state: missing {', '.join(missing)}"
        center2 = np.asarray(self.state.body_centers_px[1], dtype=float)
        span = self.state.neck_x_spans[1] if self.state.neck_x_spans else [0, gray2.shape[1] - 1]
        x0 = max(0, int(span[0]))
        x1 = min(gray2.shape[1], int(span[1]) + 1)
        y0 = max(1, min(int(center2[1]), gray2.shape[0] - 2))
        if x1 - x0 < 3 or y0 >= gray2.shape[0] - 2:
            return (
                False,
                f"Endcone search area is invalid: x=[{x0}, {x1}), y_start={y0}, "
                f"image={gray2.shape[1]}x{gray2.shape[0]}",
            )
        profile = gray2[y0:, x0:x1].astype(float).mean(axis=1)
        gradient = np.diff(profile)
        if gradient.size == 0:
            return False, "Endcone boundary not found: vertical brightness profile is empty"
        local_index = int(np.argmax(np.abs(gradient)))
        boundary_y = float(y0 + local_index + self.config.endcone.endcone_boundary_offset_px)
        raw_diameter = abs(boundary_y - center2[1]) * float(self.state.mm_per_pixel)
        diagnostics.update(boundary_y_px=boundary_y, raw_diameter_mm=raw_diameter)
        if not (self.config.measurement.diameter_min_mm < raw_diameter < self.config.measurement.diameter_max_mm):
            return (
                False,
                "Endcone diameter is outside physical limits: "
                f"{raw_diameter:.3f} mm not in "
                f"({self.config.measurement.diameter_min_mm:.3f}, "
                f"{self.config.measurement.diameter_max_mm:.3f})",
            )
        overlay2.extend([
            _line_element((x0, boundary_y), (x1 - 1, boundary_y), "#FF0000", 4),
            _marker_element("cross", center2, 3.0, "#FF00FF", 2),
        ])
        values = self.state.values
        values.diameter_mm = _ema_optional(values.diameter_mm, raw_diameter, self.config.endcone.endcone_diameter_alpha)
        self._update_hr()
        return True, "Endcone measurement updated"

    def _brightness_valid(self) -> bool:
        low = self.config.measurement.brightness_min
        high = self.config.measurement.brightness_max
        return all(low <= value <= high for value in self.state.filtered_light)

    def _update_hr(self) -> None:
        # 保留 Endcone 的原调用点，当前不再生成派生高度值。
        return

    def _result(
        self,
        valid: bool,
        stage: MeasurementStage,
        pair: StereoFramePair,
        preview1: np.ndarray,
        preview2: np.ndarray,
        overlay1: list[dict[str, Any]],
        overlay2: list[dict[str, Any]],
        diagnostics: dict[str, Any],
        message: str,
        started: float,
    ) -> MeasurementResult:
        diagnostics["cycle_ms"] = (perf_counter() - started) * 1000.0
        return MeasurementResult(
            valid=valid,
            stage=stage,
            values=self.state.values,
            diagnostics=diagnostics,
            preview1=preview1,
            preview2=preview2,
            overlay1=overlay1,
            overlay2=overlay2,
            timestamp_s=pair.timestamp_s,
            message=message,
        )


def _to_gray_uint8(image: np.ndarray) -> np.ndarray:
    array = np.asarray(image)
    if array.ndim == 3:
        code = cv2.COLOR_BGRA2GRAY if array.shape[2] == 4 else cv2.COLOR_BGR2GRAY
        array = cv2.cvtColor(array, code)
    if array.dtype != np.uint8:
        minimum = float(np.min(array))
        maximum = float(np.max(array))
        if maximum <= minimum:
            return np.zeros(array.shape, dtype=np.uint8)
        array = cv2.convertScaleAbs(array, alpha=255.0 / (maximum - minimum), beta=-minimum * 255.0 / (maximum - minimum))
    return np.ascontiguousarray(array)


def _ellipse_x_span(detection: EllipseDetection, image_width: int) -> list[int]:
    half_width = float(max(detection.ellipse[1])) * 0.5
    left = int(np.clip(round(detection.center[0] - half_width), 0, image_width - 1))
    right = int(np.clip(round(detection.center[0] + half_width), left, image_width - 1))
    return [left, right]


def _manual_meniscus_roi(roi, image_shape: tuple[int, ...]) -> tuple[dict[str, Any], np.ndarray]:
    """将 cnf 矩形 ROI 转为二维 meniscus 检测器使用的边界。"""
    height, width = image_shape[:2]
    x0 = int(np.clip(roi.x, 0, max(width - 1, 0)))
    y0 = int(np.clip(roi.y, 0, max(height - 1, 0)))
    x1 = int(np.clip(roi.x + roi.width - 1, x0, max(width - 1, 0)))
    y1 = int(np.clip(roi.y + roi.height - 1, y0, max(height - 1, 0)))
    if x1 - x0 < 2 or y1 - y0 < 2:
        raise ValueError(f"manual meniscus ROI is too small: ({x0}, {y0})-({x1}, {y1})")
    roi_state = {
        "left_boundary": [float(x0), float(y1)],
        "right_boundary": [float(x1), float(y1)],
        "bottom_curve": [[float(x0), float(y1)], [float(x1), float(y1)]],
    }
    return roi_state, np.array([(x0 + x1) * 0.5, float(y0)], dtype=float)


def _vertical_roi_from_ratio(
    height: int,
    start_ratio: float,
    stop_ratio: float,
) -> tuple[int, int]:
    start = int(round(height * float(start_ratio)))
    stop = int(round(height * float(stop_ratio)))
    return _clamp_vertical_roi(start, stop, height)


def _clamp_vertical_roi(start: int, stop: int, height: int) -> tuple[int, int]:
    y0 = int(np.clip(start, 0, max(height - 1, 0)))
    y1 = int(np.clip(stop, y0 + 1, height))
    return y0, y1


def _find_meniscus_arc(
    gray: np.ndarray,
    roi_state: dict[str, Any],
    expected_center: np.ndarray,
    previous_boundary_y: float | None,
    settings: Any,
) -> DetectionResult[MeniscusArcDetection]:
    data: dict[str, Any] = {}
    height, width = gray.shape[:2]
    try:
        left = np.asarray(roi_state.get("left_boundary"), dtype=float)
        right = np.asarray(roi_state.get("right_boundary"), dtype=float)
        bottom_curve = np.asarray(roi_state.get("bottom_curve"), dtype=float).reshape(-1, 2)
    except (TypeError, ValueError):
        return DetectionResult.failure("invalid Crown ROI data", data)
    if left.size != 2 or right.size != 2 or bottom_curve.shape[0] < 2:
        return DetectionResult.failure(
            "Crown ROI requires two side boundaries and at least two bottom points",
            data,
        )

    left_x, right_x = sorted((float(left[0]), float(right[0])))
    margin = max(0, int(round(float(settings.crown.crown_edge_horizontal_margin_px))))
    x0 = max(1, int(np.ceil(left_x)) + margin)
    x1 = min(width - 2, int(np.floor(right_x)) - margin)

    if x1 - x0 + 1 < int(settings.crown.crown_min_edge_points):
        return DetectionResult.failure(
            f"Crown horizontal search width is too small ({x1 - x0 + 1} columns)",
            data,
        )
    x_values = np.arange(x0, x1 + 1, dtype=int)

    order = np.argsort(bottom_curve[:, 0])
    bottom_sorted = bottom_curve[order]
    unique_x, unique_indices = np.unique(bottom_sorted[:, 0], return_index=True)
    unique_y = bottom_sorted[unique_indices, 1]
    if unique_x.size < 2:
        return DetectionResult.failure(
            "Crown bottom boundary has fewer than two unique x values",
            data,
        )
    bottom_y = np.interp(x_values, unique_x, unique_y)
    bottom_margin = max(0.0, float(settings.crown.crown_edge_bottom_margin_px))
    bottom_y = np.minimum(bottom_y - bottom_margin, height - 2.0)

    search_start = max(1, int(np.floor(float(expected_center[1]))))
    search_stop = min(height - 1, int(np.ceil(float(np.max(bottom_y)))))
    tracking_half_height = max(
        8,
        int(round(float(settings.crown.crown_edge_search_half_height_px))),
    )
    if previous_boundary_y is not None:
        search_start = max(search_start, int(np.floor(previous_boundary_y)) - tracking_half_height)
        search_stop = min(search_stop, int(np.ceil(previous_boundary_y)) + tracking_half_height + 1)
    data.update(
        bottom_margin_px=bottom_margin,
        tracking_half_height_px=tracking_half_height,
    )
    if search_stop - search_start < 3:
        return DetectionResult.failure(
            f"Crown vertical search range is too small ({search_start}:{search_stop})",
            data,
        )

    blurred = cv2.GaussianBlur(gray, (7, 7), 1.5)
    gradient_y = cv2.Sobel(blurred, cv2.CV_32F, 0, 1, ksize=3)
    negative_gradient = np.maximum(-gradient_y, 0.0)
    horizontal_size = max(3, int(round((x1 - x0 + 1) * 0.01)))
    negative_gradient = cv2.blur(negative_gradient, (horizontal_size, 1))

    rows = np.arange(search_start, search_stop, dtype=float)[:, None]
    valid_area = rows < bottom_y[None, :]
    score_area = negative_gradient[search_start:search_stop, x_values]
    column_strengths = np.max(np.where(valid_area, score_area, 0.0), axis=0)
    column_strengths_mean = float(np.mean(column_strengths))
    column_strengths_maximum = float(np.max(column_strengths))
    minimum_strength = column_strengths_maximum * max(
        float(settings.crown.crown_edge_column_max_factor),
        0.0,
    )
    column_keep = column_strengths >= max(minimum_strength, np.finfo(float).eps)
    data.update(
        column_strengths_mean=column_strengths_mean,
        column_strengths_maximum=column_strengths_maximum,
        minimum_strength=minimum_strength,
        kept_column_count=int(np.count_nonzero(column_keep)),
    )
    if np.count_nonzero(column_keep) < int(settings.crown.crown_min_edge_points):
        return DetectionResult.failure(
            "Crown gradient columns below minimum "
            f"({np.count_nonzero(column_keep)} < {int(settings.crown.crown_min_edge_points)}, "
            f"threshold={minimum_strength:.3f})",
            data,
        )

    # Use only columns where the meniscus is present so empty columns do not dilute row scores.
    x_values = x_values[column_keep]
    bottom_y = bottom_y[column_keep]
    valid_area = valid_area[:, column_keep]
    score_area = score_area[:, column_keep]
    valid_counts = np.maximum(valid_area.sum(axis=1), 1)
    row_scores = np.where(valid_area, score_area, 0.0).sum(axis=1) / valid_counts
    seed_y = search_start + _last_peak_index(row_scores)
    data["seed_y_px"] = seed_y

    candidate_y: list[int] = []
    strengths: list[float] = []
    local_start = max(search_start, seed_y - tracking_half_height)
    for column, maximum_y in zip(x_values, bottom_y):
        local_stop = min(search_stop, int(np.floor(maximum_y)) + 1, seed_y + tracking_half_height + 1)
        if local_stop <= local_start:
            candidate_y.append(local_start)
            strengths.append(0.0)
            continue
        column_score = negative_gradient[local_start:local_stop, column]
        local_index = int(np.argmax(column_score))
        candidate_y.append(local_start + local_index)
        strengths.append(float(column_score[local_index]))

    candidate_y_array = np.asarray(candidate_y, dtype=float)
    strengths_array = np.asarray(strengths, dtype=float)
    keep = strengths_array >= max(minimum_strength, np.finfo(float).eps)
    fit_x = x_values[keep].astype(float)
    fit_y = candidate_y_array[keep]
    fit_strengths = strengths_array[keep]
    data["edge_point_count"] = int(fit_x.size)
    if fit_x.size < int(settings.crown.crown_min_edge_points):
        return DetectionResult.failure(
            f"Crown local edge points below minimum ({fit_x.size} < {int(settings.crown.crown_min_edge_points)})",
            data,
        )

    residual_limit = max(1.0, float(settings.crown.crown_edge_fit_residual_px))
    fit_x, fit_y, fit_strengths, coefficients = _robust_meniscus_curve(
        fit_x,
        fit_y,
        fit_strengths,
        residual_limit,
    )
    data.update(
        residual_limit_px=residual_limit,
        robust_inlier_count=int(fit_x.size),
    )
    if fit_x.size < int(settings.crown.crown_min_edge_points):
        return DetectionResult.failure(
            f"Crown robust-fit inliers below minimum ({fit_x.size} < {int(settings.crown.crown_min_edge_points)})",
            data,
        )
    selected_width = max(float(x_values[-1] - x_values[0] + 1), 1.0)
    # Crown coverage is kept as diagnostics only, not as a validity gate.
    coverage = float((fit_x.max() - fit_x.min() + 1.0) / selected_width)
    data["coverage_ratio"] = coverage

    middle_x = float(expected_center[0])
    if not float(fit_x.min()) <= middle_x <= float(fit_x.max()):
        return DetectionResult.failure(
            f"Crown projected center x={middle_x:.2f} is outside fitted range "
            f"[{fit_x.min():.2f}, {fit_x.max():.2f}]",
            data,
        )
    sagitta = float(
        np.polyval(coefficients, middle_x)
        - 0.5 * (np.polyval(coefficients, fit_x.min()) + np.polyval(coefficients, fit_x.max()))
    )
    data["sagitta_px"] = sagitta
    if sagitta < 0.0:
        return DetectionResult.failure(
            f"Crown fitted curve bends in the wrong direction "
            f"(sagitta={sagitta:.3f}, limit={residual_limit:.3f})",
            data,
        )
    boundary = np.array([middle_x, np.polyval(coefficients, middle_x)], dtype=float)
    curve_x = np.linspace(
        fit_x.min(),
        fit_x.max(),
        max(2, int(round(fit_x.max() - fit_x.min())) + 1),
    )
    curve = np.column_stack([curve_x, np.polyval(coefficients, curve_x)])
    residual = fit_y - np.polyval(coefficients, fit_x)
    return DetectionResult.success(
        MeniscusArcDetection(
            edge_points=np.column_stack([fit_x, fit_y]),
            curve_points=curve,
            boundary_point=boundary,
            coefficients=coefficients,
            seed_y=float(seed_y),
            fit_error_px=float(np.sqrt(np.mean(residual * residual))),
            coverage_ratio=coverage,
            fit_strength_mean=float(np.mean(fit_strengths)),
            column_strengths_mean=column_strengths_mean,
            column_strengths_maximum=column_strengths_maximum,
        ),
        data,
    )


def _last_peak_index(values: np.ndarray) -> int:
    """Return the last significant local peak index, or the global maximum."""

    scores = np.asarray(values, dtype=float).reshape(-1)
    if scores.size == 0:
        return 0
    maximum = float(np.max(scores))
    minimum_peak_height = maximum * 0.5
    peak_indices, _properties = find_peaks(scores, height=minimum_peak_height)
    if peak_indices.size:
        return int(peak_indices[-1])
    return int(np.argmax(scores))


def _find_body_brightness_curve(
    gray: np.ndarray,
    roi_state: dict[str, Any],
    expected_center: np.ndarray,
    previous_boundary_y: float | None,
    settings: Any,
    brightness_offset: float | None = None,
) -> DetectionResult[MeniscusArcDetection]:
    """Extract the lower body brightness boundary from bottom to top."""
    data: dict[str, Any] = {}
    height, width = gray.shape[:2]
    try:
        left = np.asarray(roi_state.get("left_boundary"), dtype=float)
        right = np.asarray(roi_state.get("right_boundary"), dtype=float)
        bottom_curve = np.asarray(roi_state.get("bottom_curve"), dtype=float).reshape(-1, 2)
    except (TypeError, ValueError):
        return DetectionResult.failure("invalid Body ROI data", data)
    if left.size != 2 or right.size != 2 or bottom_curve.shape[0] < 2:
        return DetectionResult.failure(
            "Body ROI requires two side boundaries and at least two bottom points",
            data,
        )

    left_x, right_x = sorted((float(left[0]), float(right[0])))
    margin = max(0, int(round(float(settings.body.body_edge_horizontal_margin_px))))
    x0 = max(1, int(np.ceil(left_x)) + margin)
    x1 = min(width - 2, int(np.floor(right_x)) - margin)

    if x1 - x0 + 1 < int(settings.body.body_min_edge_points):
        return DetectionResult.failure(
            f"Body horizontal search width is too small ({x1 - x0 + 1} columns)",
            data,
        )
    x_values = np.arange(x0, x1 + 1, dtype=int)

    order = np.argsort(bottom_curve[:, 0])
    bottom_sorted = bottom_curve[order]
    unique_x, unique_indices = np.unique(bottom_sorted[:, 0], return_index=True)
    unique_y = bottom_sorted[unique_indices, 1]
    if unique_x.size < 2:
        return DetectionResult.failure(
            "Body bottom boundary has fewer than two unique x values",
            data,
        )
    bottom_y = np.interp(x_values, unique_x, unique_y)
    bottom_margin = max(0.0, float(settings.body.body_edge_bottom_margin_px))
    bottom_y = np.minimum(bottom_y - bottom_margin, height - 2.0)

    roi_y0, roi_y1 = _vertical_roi_from_ratio(
        height,
        settings.body.body_start_search_ratio,
        settings.body.body_stop_search_ratio,
    )
    data.update(search_start_y_px=roi_y0, search_stop_y_px=roi_y1)
    search_start = max(roi_y0, 1, int(np.floor(float(expected_center[1]))))
    search_stop = min(roi_y1, height - 1, int(np.ceil(float(np.max(bottom_y)))))
    tracking_half_height = max(
        8,
        int(round(float(settings.body.body_edge_search_half_height_px))),
    )
    if previous_boundary_y is not None:
        search_start = max(search_start, int(np.floor(previous_boundary_y)) - tracking_half_height)
        search_stop = min(search_stop, int(np.ceil(previous_boundary_y)) + tracking_half_height + 1)
    data.update(
        bottom_margin_px=bottom_margin,
        tracking_half_height_px=tracking_half_height,
    )
    if search_stop - search_start < 3:
        return DetectionResult.failure(
            f"Body vertical search range is too small ({search_start}:{search_stop})",
            data,
        )

    # Keep each column maximum first, then separate obvious parallel bright bands.
    # Body ROI limits both the candidate range and the expensive image preprocessing.
    roi = gray[roi_y0:roi_y1, :]
    blurred = cv2.GaussianBlur(roi, (7, 7), 1.5)
    horizontal_size = max(3, int(round((x1 - x0 + 1) * 0.05)))
    brightness = cv2.blur(blurred, (horizontal_size, 1))
    column_values = brightness[
        search_start - roi_y0 : search_stop - roi_y0,
        x_values,
    ]
    search_rows = np.arange(search_start, search_stop, dtype=float)[:, None]
    valid_area = search_rows <= np.floor(bottom_y)[None, :]
    offset = (
        float(settings.body.body_brightness_offset_cam1)
        if brightness_offset is None
        else float(brightness_offset)
    )
    data["brightness_offset"] = offset
    candidate_y_array, strengths_array = _bottom_up_brightness_threshold_crossings(
        column_values,
        valid_area,
        offset,
    )
    candidate_y_array += search_start
    keep = np.isfinite(candidate_y_array) & (strengths_array > np.finfo(float).eps)
    fit_x = x_values[keep].astype(float)
    fit_y = candidate_y_array[keep]
    fit_strengths = strengths_array[keep]
    usable_strengths = strengths_array[strengths_array > np.finfo(float).eps]
    data.update(
        threshold_crossing_count=int(fit_x.size),
        column_maximum_p90=(
            float(np.percentile(usable_strengths, 90)) if usable_strengths.size else 0.0
        ),
        column_maximum_maximum=(
            float(np.max(usable_strengths)) if usable_strengths.size else 0.0
        ),
    )
    if fit_x.size < int(settings.body.body_min_edge_points):
        return DetectionResult.failure(
            f"Body threshold crossings below minimum ({fit_x.size} < {int(settings.body.body_min_edge_points)})",
            data,
        )

    residual_limit = max(1.0, float(settings.body.body_edge_fit_residual_px))
    fit_x, fit_y, fit_strengths, coefficients = _robust_meniscus_curve(
        fit_x,
        fit_y,
        fit_strengths,
        residual_limit,
    )
    data.update(
        residual_limit_px=residual_limit,
        robust_inlier_count=int(fit_x.size),
    )
    if fit_x.size < int(settings.body.body_min_edge_points):
        return DetectionResult.failure(
            f"Body robust-fit inliers below minimum ({fit_x.size} < {int(settings.body.body_min_edge_points)})",
            data,
        )
    coverage = float((fit_x.max() - fit_x.min() + 1.0) / max(x1 - x0 + 1, 1))
    data["coverage_ratio"] = coverage
    if coverage < float(settings.body.body_edge_min_coverage_ratio):
        return DetectionResult.failure(
            f"Body edge coverage is too low "
            f"({coverage:.3f} < {float(settings.body.body_edge_min_coverage_ratio):.3f})",
            data,
        )
    middle_x = float(expected_center[0])
    if not float(fit_x.min()) <= middle_x <= float(fit_x.max()):
        return DetectionResult.failure(
            f"Body projected center x={middle_x:.2f} is outside fitted range "
            f"[{fit_x.min():.2f}, {fit_x.max():.2f}]",
            data,
        )
    sagitta = float(
        np.polyval(coefficients, middle_x)
        - 0.5 * (np.polyval(coefficients, fit_x.min()) + np.polyval(coefficients, fit_x.max()))
    )
    data["sagitta_px"] = sagitta
    if sagitta < 0.0:
        return DetectionResult.failure(
            f"Body fitted curve bends in the wrong direction "
            f"(sagitta={sagitta:.3f}, limit={residual_limit:.3f})",
            data,
        )
    boundary = np.array([middle_x, np.polyval(coefficients, middle_x)], dtype=float)
    curve_x = np.linspace(
        fit_x.min(),
        fit_x.max(),
        max(2, int(round(fit_x.max() - fit_x.min())) + 1),
    )
    residual = fit_y - np.polyval(coefficients, fit_x)
    return DetectionResult.success(
        MeniscusArcDetection(
            edge_points=np.column_stack([fit_x, fit_y]),
            curve_points=np.column_stack([curve_x, np.polyval(coefficients, curve_x)]),
            boundary_point=boundary,
            coefficients=coefficients,
            seed_y=float(np.median(fit_y)),
            fit_error_px=float(np.sqrt(np.mean(residual * residual))),
            coverage_ratio=coverage,
            fit_strength_mean=float(np.mean(fit_strengths)),
        ),
        data,
    )


def _bottom_up_brightness_threshold_crossings(
    profiles: np.ndarray,
    valid_area: np.ndarray,
    offset: float,
    minimum_run: int = 3,
) -> tuple[np.ndarray, np.ndarray]:
    """Vectorized extraction of bottom-up max-offset threshold crossings."""
    values = np.asarray(profiles, dtype=float)
    valid = np.asarray(valid_area, dtype=bool)
    if values.ndim != 2 or valid.shape != values.shape:
        raise ValueError("profiles and valid_area must be equally shaped 2-D arrays")
    height, column_count = values.shape
    crossings = np.full(column_count, np.nan, dtype=float)
    maxima = np.zeros(column_count, dtype=float)
    if height < minimum_run + 1 or column_count == 0:
        return crossings, maxima

    finite_columns = np.all(np.isfinite(values) | ~valid, axis=0)
    usable_columns = valid.any(axis=0) & finite_columns
    masked_values = np.where(valid, values, -np.inf)
    maxima[usable_columns] = np.max(masked_values[:, usable_columns], axis=0)
    thresholds = maxima - max(float(offset), 0.0)
    above = valid & (values >= thresholds[None, :])

    # run[j] means minimum_run consecutive pixels from j are inside the bright region.
    run_length = height - minimum_run + 1
    run = np.ones((run_length, column_count), dtype=bool)
    for shift in range(minimum_run):
        run &= above[shift : shift + run_length]

    # Row j maps to inside=j+minimum_run-1; the next pixel must stay in ROI and drop below threshold.
    core = run[:-1] & valid[minimum_run:] & ~above[minimum_run:]
    core[:, ~usable_columns] = False
    has_crossing = core.any(axis=0)
    if not np.any(has_crossing):
        return crossings, maxima

    reverse_offset = np.argmax(core[::-1, has_crossing], axis=0)
    inside = height - 2 - reverse_offset
    columns = np.flatnonzero(has_crossing)
    inside_values = values[inside, columns]
    outside_values = values[inside + 1, columns]
    denominator = inside_values - outside_values
    fractions = np.divide(
        inside_values - thresholds[columns],
        denominator,
        out=np.zeros_like(inside_values),
        where=np.abs(denominator) >= 1e-12,
    )
    crossings[columns] = inside + np.clip(fractions, 0.0, 1.0)
    return crossings, maxima


def _robust_meniscus_curve(
    x_values: np.ndarray,
    y_values: np.ndarray,
    strengths: np.ndarray,
    residual_limit: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    x = x_values.astype(float)
    y = y_values.astype(float)
    weights = strengths.astype(float)
    coefficients = np.polyfit(x, y, 2, w=np.sqrt(np.maximum(weights, 1e-6)))
    for _iteration in range(5):
        residual = y - np.polyval(coefficients, x)
        median = float(np.median(residual))
        mad = float(np.median(np.abs(residual - median)))
        limit = max(residual_limit, 3.0 * 1.4826 * mad)
        keep = np.abs(residual - median) <= limit
        if int(np.count_nonzero(keep)) < 5 or bool(np.all(keep)):
            break
        x = x[keep]
        y = y[keep]
        weights = weights[keep]
        coefficients = np.polyfit(x, y, 2, w=np.sqrt(np.maximum(weights, 1e-6)))
    return x, y, weights, coefficients


def _find_ellipse(
    gray: np.ndarray,
    gradient_threshold: float,
    min_area: float,
    minimum_y: float | None = None,
    expected_x: float | None = None,
    prefer_small: bool = False,
    outer_arc_only: bool = False,
    vertical_roi: tuple[int, int] | None = None,
) -> DetectionResult[EllipseDetection]:
    """Detect and fit a neck ellipse from the grayscale gradient contour."""

    data: dict[str, Any] = {}
    original_height = gray.shape[0]
    if vertical_roi is None:
        roi_y0, roi_y1 = 0, original_height
    else:
        roi_y0, roi_y1 = _clamp_vertical_roi(vertical_roi[0], vertical_roi[1], original_height)
    roi = gray[roi_y0:roi_y1, :]
    data.update(search_start_y_px=roi_y0, search_stop_y_px=roi_y1)
    blurred = cv2.GaussianBlur(roi, (5, 5), 0)
    gradient_x = cv2.Sobel(blurred, cv2.CV_32F, 1, 0, ksize=3)
    gradient_y = cv2.Sobel(blurred, cv2.CV_32F, 0, 1, ksize=3)
    gradient_magnitude = cv2.magnitude(gradient_x, gradient_y)
    # Diagnostics only need a trend reference; full-image percentile is expensive on large frames.
    # Sampling keeps 1/64 of pixels, enough for diagnostics without slowing the neck loop.
    gradient_p98 = float(np.percentile(gradient_magnitude[::8, ::8], 98))
    gradient_max = float(np.max(gradient_magnitude))
    binary = np.where(
        gradient_magnitude >= max(float(gradient_threshold), 0.0),
        255,
        0,
    ).astype(np.uint8)
    # Close small gaps on gradient edges so one meniscus arc is not split into short contours.
    binary = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8), iterations=1)
    contours, _hierarchy = cv2.findContours(binary, cv2.RETR_LIST, cv2.CHAIN_APPROX_NONE)
    data.update(
        gradient_p98=gradient_p98,
        gradient_max=gradient_max,
    )
    candidates: list[tuple[float, EllipseDetection]] = []
    image_area = float(roi.shape[0] * roi.shape[1])
    for contour in contours:
        if len(contour) < 5:
            continue
        area = abs(float(cv2.contourArea(contour)))
        if area < min_area or area > image_area * 0.8:
            continue
        fit_contour, contour_closed = (
            _extract_outer_convex_arc(contour) if outer_arc_only else (contour, True)
        )
        if len(fit_contour) < 5:
            continue
        ellipse = _fit_axis_aligned_ellipse(fit_contour)
        if ellipse is None:
            continue
        center = np.asarray(ellipse[0], dtype=float)
        axes = np.asarray(ellipse[1], dtype=float)
        if min(axes) < 4 or max(axes) > max(roi.shape) * 1.5:
            continue
        center_full = center + np.array([0.0, float(roi_y0)], dtype=float)
        if minimum_y is not None and center_full[1] < minimum_y:
            continue
        lower = _ellipse_lower_point(ellipse)
        lower_full = lower + np.array([0.0, float(roi_y0)], dtype=float)
        fit_contour_full = fit_contour.astype(float) + np.array([[[0.0, float(roi_y0)]]])
        ellipse_full = (
            (float(center_full[0]), float(center_full[1])),
            ellipse[1],
            ellipse[2],
        )
        perimeter = max(float(cv2.arcLength(contour, True)), 1e-6)
        circularity = 4.0 * np.pi * area / (perimeter * perimeter)
        x_penalty = 0.0 if expected_x is None else abs(center_full[0] - expected_x)
        size_score = -np.sqrt(area) if prefer_small else np.sqrt(area)
        score = size_score + circularity * 100.0 - x_penalty * 0.1
        candidates.append(
            (
                score,
                EllipseDetection(
                    ellipse_full,
                    fit_contour_full,
                    center_full,
                    lower_full,
                    area,
                    contour_closed=contour_closed,
                ),
            )
        )
    if not candidates:
        return DetectionResult.failure(
            "no ellipse candidate passed contour, area, geometry, and position checks "
            f"(contours={len(contours)}, gradient threshold={float(gradient_threshold):.3f})",
            data,
        )
    return DetectionResult.success(
        max(candidates, key=lambda item: item[0])[1],
        data,
    )


def _fit_axis_aligned_ellipse(
    contour: np.ndarray,
) -> tuple[tuple[float, float], tuple[float, float], float] | None:
    """Fit an axis-aligned ellipse and return OpenCV ellipse format."""

    points = np.asarray(contour, dtype=float).reshape(-1, 2)
    if len(points) < 5 or not np.all(np.isfinite(points)):
        return None

    origin = np.mean(points, axis=0)
    scale = np.std(points, axis=0)
    if np.any(scale <= np.finfo(float).eps):
        return None
    normalized = (points - origin) / scale
    x_values = normalized[:, 0]
    y_values = normalized[:, 1]
    design = np.column_stack(
        [x_values * x_values, y_values * y_values, x_values, y_values]
    )
    coefficients, _residuals, rank, _singular_values = np.linalg.lstsq(
        design,
        np.ones(len(points), dtype=float),
        rcond=None,
    )
    if rank < 4:
        return None
    coefficient_xx, coefficient_yy, coefficient_x, coefficient_y = coefficients
    if coefficient_xx <= 0.0 or coefficient_yy <= 0.0:
        return None

    center_normalized = np.array(
        [
            -coefficient_x / (2.0 * coefficient_xx),
            -coefficient_y / (2.0 * coefficient_yy),
        ],
        dtype=float,
    )
    radius_term = (
        1.0
        + coefficient_x * coefficient_x / (4.0 * coefficient_xx)
        + coefficient_y * coefficient_y / (4.0 * coefficient_yy)
    )
    if radius_term <= 0.0:
        return None
    radii_normalized = np.sqrt(
        radius_term / np.array([coefficient_xx, coefficient_yy], dtype=float)
    )
    center = origin + center_normalized * scale
    radii = radii_normalized * scale
    if not np.all(np.isfinite(center)) or not np.all(np.isfinite(radii)):
        return None
    return (
        (float(center[0]), float(center[1])),
        (float(2.0 * radii[0]), float(2.0 * radii[1])),
        0.0,
    )


def _extract_outer_convex_arc(contour: np.ndarray) -> tuple[np.ndarray, bool]:
    """Extract the outer convex arc between contour endpoints."""
    if len(contour) < 5:
        return contour, True
    defect_contour = np.ascontiguousarray(contour, dtype=np.int32)
    hull_indices = cv2.convexHull(defect_contour, returnPoints=False)
    if hull_indices is None or len(hull_indices) < 3:
        return contour, True
    try:
        defects = cv2.convexityDefects(defect_contour, hull_indices)
    except cv2.error:
        return contour, True
    if defects is None or len(defects) == 0:
        return contour, True

    start, end, farthest, depth_raw = max(defects[:, 0, :], key=lambda item: int(item[3]))
    _x, _y, width, height = cv2.boundingRect(defect_contour)
    depth_px = float(depth_raw) / 256.0
    minimum_depth = max(3.0, 0.05 * min(width, height))
    if depth_px < minimum_depth:
        return contour, True

    point_count = len(defect_contour)
    forward_indices = _cyclic_indices(int(start), int(end), point_count)
    if int(farthest) in forward_indices:
        outer_indices = _cyclic_indices(int(end), int(start), point_count)
    else:
        outer_indices = forward_indices
    outer_arc = np.ascontiguousarray(defect_contour[outer_indices])
    if len(outer_arc) < 5:
        return contour, True
    return outer_arc, False


def _cyclic_indices(start: int, stop: int, count: int) -> list[int]:
    if start <= stop:
        return list(range(start, stop + 1))
    return list(range(start, count)) + list(range(0, stop + 1))


def _ellipse_lower_point(ellipse) -> np.ndarray:
    center, axes, angle = ellipse
    center_xy = np.asarray(center, dtype=float)
    semi_axis_x, semi_axis_y = np.asarray(axes, dtype=float) * 0.5
    radians = np.deg2rad(float(angle))
    sine, cosine = np.sin(radians), np.cos(radians)

    # Solve the maximum y of the rotated ellipse directly to preserve sub-pixel precision.
    vertical_radius = float(np.hypot(semi_axis_x * sine, semi_axis_y * cosine))
    if vertical_radius <= np.finfo(float).eps:
        return center_xy.copy()
    lower_x_offset = (
        (semi_axis_x * semi_axis_x - semi_axis_y * semi_axis_y)
        * sine
        * cosine
        / vertical_radius
    )
    return center_xy + np.array([lower_x_offset, vertical_radius], dtype=float)


def _meniscus_arc_elements(
    detection: MeniscusArcDetection,
) -> list[dict[str, Any]]:
    boundary = np.asarray(detection.boundary_point, dtype=float)
    return [
        _polyline_element(detection.edge_points, "#009600", 1),
        _polyline_element(detection.curve_points, "#00FF00", 2),
        _marker_element("cross", boundary, 3.0, "#FF0000", 2),
    ]


def _detection_elements(detection: EllipseDetection, color: str) -> list[dict[str, Any]]:
    contour = np.asarray(detection.contour, dtype=float).reshape(-1, 2)
    center = np.asarray(detection.center, dtype=float)
    lower = np.asarray(detection.lower, dtype=float)
    elements = [
        _polyline_element(contour, color, 2, detection.contour_closed),
        _polyline_element(_ellipse_points(detection.ellipse), color, 2, True),
        _marker_element("cross", center, 3.0, color, 2),
        _marker_element("cross", lower, 3.0, "#FF0000", 2),
    ]
    return elements


def _line_element(start: Any, end: Any, color: str, width: int) -> dict[str, Any]:
    x1, y1 = np.asarray(start, dtype=float).reshape(2)
    x2, y2 = np.asarray(end, dtype=float).reshape(2)
    return {"type": "line", "data": (float(x1), float(y1), float(x2), float(y2), None), "color": color, "width": width}


def _polyline_element(points: np.ndarray, color: str, width: int, closed: bool = False) -> dict[str, Any]:
    values = np.asarray(points, dtype=float).reshape(-1, 2)
    return {
        "type": "polyline",
        "data": [(float(x), float(y)) for x, y in values],
        "color": color,
        "width": width,
        "closed": closed,
    }


def _marker_element(marker_type: str, point: Any, size: float, color: str, width: int) -> dict[str, Any]:
    x, y = np.asarray(point, dtype=float).reshape(2)
    return {"type": marker_type, "data": (float(x), float(y), float(size)), "color": color, "width": width}


def _ellipse_points(ellipse: tuple[tuple[float, float], tuple[float, float], float]) -> np.ndarray:
    center, axes, angle_deg = ellipse
    radius_x, radius_y = np.asarray(axes, dtype=float) * 0.5
    angle = np.deg2rad(float(angle_deg))
    parameter = np.linspace(0.0, 2.0 * np.pi, 181, endpoint=False)
    cosine, sine = np.cos(parameter), np.sin(parameter)
    rotated_x = radius_x * cosine * np.cos(angle) - radius_y * sine * np.sin(angle)
    rotated_y = radius_x * cosine * np.sin(angle) + radius_y * sine * np.cos(angle)
    return np.column_stack([rotated_x + float(center[0]), rotated_y + float(center[1])])


def _ema(previous: float, raw: float, alpha: float) -> float:
    alpha = float(np.clip(alpha, 0.0, 1.0))
    if not np.isfinite(previous) or previous == 0.0:
        return float(raw)
    return float(previous + (1 - alpha) * (raw - previous))


def _ema_optional(previous: float | None, raw: float, alpha: float) -> float:
    if previous is None or not np.isfinite(previous):
        return float(raw)
    return _ema(float(previous), float(raw), alpha)


def _advance_crown_meniscus_correction(
    state: MeasurementState,
    current_diameter_mm: float,
    increment_mm: float,
    maximum_mm: float,
) -> None:
    """Increase Crown meniscus correction once per diameter growth step."""
    maximum = max(float(maximum_mm), 0.0)
    state.crown_meniscus_correction_mm = float(
        np.clip(state.crown_meniscus_correction_mm, 0.0, maximum)
    )
    if state.crown_last_diameter_mm is None:
        state.crown_last_diameter_mm = float(current_diameter_mm)
        return
    if float(current_diameter_mm) - state.crown_last_diameter_mm <= 1.0:
        return
    state.crown_last_diameter_mm = float(current_diameter_mm)
    state.crown_meniscus_correction_mm = min(
        maximum,
        state.crown_meniscus_correction_mm + max(float(increment_mm), 0.0),
    )



