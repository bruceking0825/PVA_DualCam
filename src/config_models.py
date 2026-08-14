from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class RoiSettings:
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0


@dataclass
class CameraSettings:
    initial_exposure_camera1: float = 13000.0
    gain_camera1: float = 1.0
    initial_exposure_camera2: float = 13000.0
    gain_camera2: float = 1.0
    offline_crop_roi: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 5120, 5120))
    online_crop_roi: RoiSettings = field(default_factory=lambda: RoiSettings(1800, 0, 1600, 5120))
    auto_exposure_enabled: bool = False
    auto_exposure_target: float = 120.0
    auto_exposure_min_us: float = 1000.0
    auto_exposure_max_us: float = 50000.0
    auto_exposure_gain: float = 0.2
    auto_exposure_deadband: float = 3.0
    auto_exposure_interval_ms: int = 500


@dataclass
class RuntimeSettings:
    disable_camera_for_plc_test: bool = False
    connect_plc_in_offline: bool = False
    idle_sample_interval_ms: int = 8000
    neck_sample_interval_ms: int = 300
    crown_sample_interval_ms: int = 1000
    body_sample_interval_ms: int = 1000
    endcone_sample_interval_ms: int = 1000
    offline_image_dir: Path = Path("../live_img")
    loop_interval_ms: int = 500
    state_file: Path = Path("measurement_state.json")
    stereo_pair_max_delta_ms: int = 100


@dataclass
class MeasurementSettings:
    brightness_min: float = 100.0
    brightness_max: float = 255.0
    diameter_min_mm: float = 0.0
    diameter_max_mm: float = 350.0
    light_alpha: float = 0.2
    mm_per_pixel_alpha: float = 0.5
    auto_exposure_roi_camera1: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))
    auto_exposure_roi_camera2: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))


@dataclass
class NeckSettings:
    min_contour_area_px: float = 80.0
    neck_min_edge_points: int = 24
    neck_gradient_threshold_cam1: float = 70.0
    neck_gradient_threshold_cam2: float = 70.0
    neck_start_search_ratio: float = 0.0
    neck_stop_search_ratio: float = 0.65
    neck_pixels_per_mm: float = 24.0
    neck_diameter_alpha: float = 0.5


@dataclass
class CrownSettings:
    crown_roi_camera1: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))
    crown_roi_camera2: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))
    crown_min_edge_points: int = 24
    crown_edge_column_max_factor: float = 0.5
    crown_edge_use_previous_boundary_y: bool = True
    crown_edge_search_half_height_px: int = 300
    crown_edge_horizontal_margin_px: int = 40
    crown_edge_bottom_margin_px: int = 100
    crown_edge_fit_residual_px: float = 10.0


@dataclass
class BodySettings:
    body_roi_camera1: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))
    body_roi_camera2: RoiSettings = field(default_factory=lambda: RoiSettings(0, 0, 512, 512))
    body_min_edge_points: int = 24
    body_brightness_offset_cam1: float = 6.0
    body_brightness_offset_cam2: float = 15.0
    body_start_search_ratio: float = 0.0
    body_stop_search_ratio: float = 1.0
    body_edge_use_previous_boundary_y: bool = True
    body_edge_search_half_height_px: int = 300
    body_edge_horizontal_margin_px: int = 40
    body_edge_bottom_margin_px: int = 100
    body_edge_min_coverage_ratio: float = 0.55
    body_edge_fit_residual_px: float = 10.0


@dataclass
class EndconeSettings:
    endcone_diameter_alpha: float = 0.2
    endcone_boundary_offset_px: int = 0


SETTING_GROUP_TARGETS = {
    "Measurement": "measurement",
    "Neck": "neck",
    "Crown": "crown",
    "Body": "body",
    "Endcone": "endcone",
}


@dataclass
class MeasurementConfig:
    runtime: RuntimeSettings = field(default_factory=RuntimeSettings)
    measurement: MeasurementSettings = field(default_factory=MeasurementSettings)
    neck: NeckSettings = field(default_factory=NeckSettings)
    crown: CrownSettings = field(default_factory=CrownSettings)
    body: BodySettings = field(default_factory=BodySettings)
    endcone: EndconeSettings = field(default_factory=EndconeSettings)

    def target_for_group(self, group: str):
        attr_name = SETTING_GROUP_TARGETS.get(group)
        return getattr(self, attr_name) if attr_name is not None else None
