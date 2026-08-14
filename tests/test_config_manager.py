from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from modules.app_config import ConfigManager  # noqa: E402
from modules.utils import IniEntry  # noqa: E402


class ConfigManagerTest(unittest.TestCase):
    def test_shared_settings_update_immediately(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "cnf.ini"
            config_path.write_text(
                "[Calibration]\n"
                "image_dir=[images]\n"
                "capture_count=[6]\n"
                "camera1_roi=[120,30,640,480]\n"
                "[Camera]\n"
                "initial_exposure_camera1=[12500]\n"
                "initial_exposure_camera2=[13500]\n"
                "offline_crop_roi=[0,0,5120,4096]\n"
                "online_crop_roi=[10,20,300,400]\n"
                "auto_exposure_enabled=[1]\n"
                "auto_exposure_target=[123]\n"
                "auto_exposure_min_us=[2000]\n"
                "auto_exposure_max_us=[40000]\n"
                "auto_exposure_gain=[0.25]\n"
                "auto_exposure_deadband=[4]\n"
                "auto_exposure_interval_ms=[250]\n"
                "[Runtime]\n"
                "offline_image_dir=[images]\n"
                "calibration_json=[calibration.json]\n"
                "[PLC]\n"
                "endpoint_url=[opc.tcp://10.0.0.1:4840]\n"
                "mode_node=[ns=3;s=\"External\".\"mode\"]\n"
                "[Measurement]\n"
                "brightness_min=[55]\n"
                "auto_exposure_roi_camera1=[5,6,70,80]\n"
                "auto_exposure_roi_camera2=[9,10,90,100]\n"
                "[Neck]\n"
                "neck_melt_alpha=[0.4]\n"
                "neck_reflector_x_span_max_diff_px=[12]\n"
                "reflector_side_score_max_factor=[0.45]\n"
                "neck_min_edge_points=[12]\n"
                "neck_start_search_ratio=[0.1]\n"
                "neck_stop_search_ratio=[0.6]\n"
                "[Crown]\n"
                "crown_min_edge_points=[14]\n"
                "crown_transition_diameter=[300]\n"
                "crown_edge_column_max_factor=[0.4]\n"
                "crown_edge_search_half_height_px=[33]\n"
                "crown_edge_horizontal_margin_px=[11]\n"
                "crown_edge_bottom_margin_px=[22]\n"
                "crown_edge_fit_residual_px=[7]\n"
                "crown_melt_alpha=[0.7]\n"
                "crown_diameter_alpha=[0.8]\n"
                "[Body]\n"
                "body_min_edge_points=[16]\n"
                "body_start_search_ratio=[0.2]\n"
                "body_stop_search_ratio=[0.8]\n"
                "body_edge_search_half_height_px=[44]\n"
                "body_edge_horizontal_margin_px=[12]\n"
                "body_edge_bottom_margin_px=[24]\n"
                "body_edge_min_coverage_ratio=[0.6]\n"
                "body_edge_fit_residual_px=[8]\n"
                "body_edge_use_previous_boundary_y=[0]\n",
                encoding="utf-8",
            )
            manager = ConfigManager()
            manager.load(config_path, emit_changes=False)

            shared = manager.measurement.crown
            changes = []
            manager.changed.connect(lambda group, key, value: changes.append((group, key, value)))
            manager.set_entry(IniEntry("Crown", "crown_roi_camera1", "10,20,300,400"))

        self.assertIs(shared, manager.measurement.crown)
        self.assertEqual(
            (shared.crown_roi_camera1.x, shared.crown_roi_camera1.y, shared.crown_roi_camera1.width, shared.crown_roi_camera1.height),
            (10, 20, 300, 400),
        )
        self.assertEqual(
            (
                manager.camera.offline_crop_roi.x,
                manager.camera.offline_crop_roi.y,
                manager.camera.offline_crop_roi.width,
                manager.camera.offline_crop_roi.height,
            ),
            (0, 0, 5120, 4096),
        )
        self.assertEqual(
            (
                manager.camera.online_crop_roi.x,
                manager.camera.online_crop_roi.y,
                manager.camera.online_crop_roi.width,
                manager.camera.online_crop_roi.height,
            ),
            (10, 20, 300, 400),
        )
        self.assertTrue(manager.camera.auto_exposure_enabled)
        self.assertAlmostEqual(manager.camera.initial_exposure_camera1, 12500.0)
        self.assertAlmostEqual(manager.camera.initial_exposure_camera2, 13500.0)
        self.assertAlmostEqual(manager.camera.auto_exposure_target, 123.0)
        self.assertAlmostEqual(manager.camera.auto_exposure_min_us, 2000.0)
        self.assertAlmostEqual(manager.camera.auto_exposure_max_us, 40000.0)
        self.assertAlmostEqual(manager.camera.auto_exposure_gain, 0.25)
        self.assertAlmostEqual(manager.camera.auto_exposure_deadband, 4.0)
        self.assertEqual(manager.camera.auto_exposure_interval_ms, 250)
        self.assertEqual(
            (
                manager.measurement.measurement.auto_exposure_roi_camera1.x,
                manager.measurement.measurement.auto_exposure_roi_camera1.y,
                manager.measurement.measurement.auto_exposure_roi_camera1.width,
                manager.measurement.measurement.auto_exposure_roi_camera1.height,
            ),
            (5, 6, 70, 80),
        )
        self.assertEqual(
            (
                manager.measurement.measurement.auto_exposure_roi_camera2.x,
                manager.measurement.measurement.auto_exposure_roi_camera2.y,
                manager.measurement.measurement.auto_exposure_roi_camera2.width,
                manager.measurement.measurement.auto_exposure_roi_camera2.height,
            ),
            (9, 10, 90, 100),
        )
        self.assertEqual(manager.measurement.runtime.offline_image_dir, root / "images")
        self.assertFalse(hasattr(manager.measurement, "plc"))
        self.assertAlmostEqual(manager.measurement.measurement.brightness_min, 55.0)
        self.assertEqual(manager.measurement.neck.neck_min_edge_points, 12)
        self.assertAlmostEqual(manager.measurement.neck.neck_start_search_ratio, 0.1)
        self.assertAlmostEqual(manager.measurement.neck.neck_stop_search_ratio, 0.6)
        self.assertEqual(manager.measurement.crown.crown_min_edge_points, 14)
        self.assertAlmostEqual(manager.measurement.crown.crown_edge_column_max_factor, 0.4)
        self.assertEqual(manager.measurement.crown.crown_edge_search_half_height_px, 33)
        self.assertEqual(manager.measurement.crown.crown_edge_horizontal_margin_px, 11)
        self.assertEqual(manager.measurement.crown.crown_edge_bottom_margin_px, 22)
        self.assertAlmostEqual(manager.measurement.crown.crown_edge_fit_residual_px, 7.0)
        self.assertEqual(manager.measurement.body.body_min_edge_points, 16)
        self.assertAlmostEqual(manager.measurement.body.body_start_search_ratio, 0.2)
        self.assertAlmostEqual(manager.measurement.body.body_stop_search_ratio, 0.8)
        self.assertEqual(manager.measurement.body.body_edge_search_half_height_px, 44)
        self.assertEqual(manager.measurement.body.body_edge_horizontal_margin_px, 12)
        self.assertEqual(manager.measurement.body.body_edge_bottom_margin_px, 24)
        self.assertAlmostEqual(manager.measurement.body.body_edge_min_coverage_ratio, 0.6)
        self.assertAlmostEqual(manager.measurement.body.body_edge_fit_residual_px, 8.0)
        self.assertFalse(manager.measurement.body.body_edge_use_previous_boundary_y)
        self.assertEqual(changes, [("Crown", "crown_roi_camera1", (10, 20, 300, 400))])


if __name__ == "__main__":
    unittest.main()

