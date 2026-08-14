from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from config_models import MeasurementConfig
from measurement_core import (
    MeasurementEngine,
    MeasurementStage,
    MeasurementState,
    MeasurementValues,
    StateStore,
    StereoFramePair,
    prepare_stereo_pair,
)
from measurement_core.engine import MeniscusArcDetection, DetectionResult, _manual_meniscus_roi


class MeasurementCoreTests(unittest.TestCase):
    def test_stage_mapping(self) -> None:
        self.assertEqual(MeasurementStage.from_value(1), MeasurementStage.NECK)
        self.assertEqual(MeasurementStage.from_value(2), MeasurementStage.CROWN)
        self.assertEqual(MeasurementStage.from_value(3), MeasurementStage.ENDCONE)
        self.assertEqual(MeasurementStage.from_value(4), MeasurementStage.BODY)

    def test_prepare_pair_preserves_configured_origin(self) -> None:
        image = np.zeros((20, 30), dtype=np.uint8)
        pair = prepare_stereo_pair(image, image.copy(), 1.0, "test", (10, 20))
        self.assertEqual(pair.origin1_xy, (10, 20))
        self.assertEqual(pair.origin2_xy, (10, 20))

    def test_state_store_roundtrip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = StateStore(Path(directory) / "state.json")
            state = MeasurementState(values=MeasurementValues(diameter_mm=12.5), valid_neck=True)
            store.save(state)
            loaded = store.load()
        self.assertEqual(loaded.values.diameter_mm, 12.5)
        self.assertTrue(loaded.valid_neck)

    def test_manual_roi_is_clamped_and_centered(self) -> None:
        config = MeasurementConfig()
        roi = config.crown.crown_roi_camera1
        roi.x, roi.y, roi.width, roi.height = 10, 20, 50, 60
        state, center = _manual_meniscus_roi(roi, (100, 100))
        self.assertEqual(state["left_boundary"], [10.0, 79.0])
        self.assertEqual(state["right_boundary"], [59.0, 79.0])
        np.testing.assert_array_equal(center, [34.5, 20.0])

    def test_crown_and_body_only_update_lower_vertices(self) -> None:
        config = MeasurementConfig()
        config.measurement.brightness_min = 0
        config.measurement.brightness_max = 255
        config.crown.crown_min_edge_points = 2
        config.body.body_min_edge_points = 2
        image = np.full((100, 100), 120, dtype=np.uint8)
        pair = StereoFramePair(image, image.copy(), 1.0, "test")
        state = MeasurementState(values=MeasurementValues(diameter_mm=25.0))
        engine = MeasurementEngine(config, state)

        detection = MeniscusArcDetection(
            edge_points=np.array([[20.0, 70.0], [80.0, 70.0]]),
            curve_points=np.array([[20.0, 70.0], [80.0, 70.0]]),
            boundary_point=np.array([50.0, 70.0]),
            coefficients=np.array([0.0, 0.0, 70.0]),
            seed_y=70.0,
            fit_error_px=0.0,
            coverage_ratio=1.0,
            fit_strength_mean=1.0,
        )
        found = DetectionResult.success(detection, {})
        with patch("measurement_core.engine._find_meniscus_arc", return_value=found), patch(
            "measurement_core.engine._find_body_brightness_curve", return_value=found
        ):
            crown = engine.process(pair, MeasurementStage.CROWN)
            body = engine.process(pair, MeasurementStage.BODY)

        self.assertTrue(crown.valid)
        self.assertTrue(body.valid)
        self.assertEqual(state.values.diameter_mm, 25.0)
        self.assertEqual(state.crown_boundary_points_px, [[50.0, 70.0], [50.0, 70.0]])
        self.assertEqual(state.body_boundary_points_px, [[50.0, 70.0], [50.0, 70.0]])

    def test_endcone_processor_is_still_dispatched(self) -> None:
        config = MeasurementConfig()
        config.measurement.brightness_min = 0
        config.measurement.brightness_max = 255
        image = np.full((20, 20), 120, dtype=np.uint8)
        pair = StereoFramePair(image, image.copy(), 1.0, "test")
        engine = MeasurementEngine(config)
        with patch.object(engine, "_process_endcone", return_value=(True, "ok")) as process:
            result = engine.process(pair, MeasurementStage.ENDCONE)
        self.assertTrue(result.valid)
        process.assert_called_once()


if __name__ == "__main__":
    unittest.main()
