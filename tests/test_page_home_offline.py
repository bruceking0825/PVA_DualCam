from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from PySide6.QtWidgets import QApplication  # noqa: E402
from modules.page_home import (  # noqa: E402
    PageHome,
    _diagnostic_group,
    _diagnostic_label_and_unit,
    _format_diagnostic_value,
    _list_image_files,
    _stage_from_offline_mode,
    _stage_from_plc_controls,
)
from measurement_core import MeasurementResult, MeasurementValues, MeasurementStage  # noqa: E402


class PageHomeOfflineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = QApplication.instance() or QApplication([])

    def test_image_folder_uses_natural_sort_and_supported_extensions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("frame10.bmp", "frame2.PNG", "frame1.jpg", "notes.txt"):
                (root / name).touch()

            paths = _list_image_files(root)

        self.assertEqual([path.name for path in paths], ["frame1.jpg", "frame2.PNG", "frame10.bmp"])

    def test_timestamp_image_names_are_sorted_chronologically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in (
                "20260720_161027.bmp",
                "20260720_153236.bmp",
                "20260720_153226.bmp",
            ):
                (root / name).touch()

            paths = _list_image_files(root)

        self.assertEqual(
            [path.stem for path in paths],
            ["20260720_153226", "20260720_153236", "20260720_161027"],
        )

    def test_offline_modes_map_to_measurement_stages(self) -> None:
        self.assertEqual(_stage_from_offline_mode("neck"), MeasurementStage.NECK)
        self.assertEqual(_stage_from_offline_mode("crown"), MeasurementStage.CROWN)
        self.assertEqual(_stage_from_offline_mode("body"), MeasurementStage.BODY)
        self.assertEqual(_stage_from_offline_mode("endcone"), MeasurementStage.ENDCONE)
        self.assertEqual(_stage_from_plc_controls(2, False), MeasurementStage.CROWN)
        self.assertEqual(_stage_from_plc_controls(2, True), MeasurementStage.BODY)
        self.assertEqual(_stage_from_plc_controls(3, True), MeasurementStage.ENDCONE)

    def test_process_diagnostics_are_grouped_and_formatted(self) -> None:
        self.assertEqual(_diagnostic_group("body_center_camera1_px"), "Camera 1")
        self.assertEqual(_diagnostic_group("pair_offset_ms"), "Stereo")
        self.assertEqual(_diagnostic_group("source"), "Runtime")
        self.assertEqual(
            _diagnostic_label_and_unit("body_edge_fit_error_camera2_px"),
            ("Body Edge Fit Error", "px"),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "crown_column_strengths_mean_camera1"
            ),
            ("Crown Col Strength Mean", ""),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "crown_column_strengths_maximum_camera2"
            ),
            ("Crown Col Strength Max", ""),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "body_search_stop_y_camera1_px"
            ),
            ("Body Search Y1", "px"),
        )
        self.assertEqual(
            _diagnostic_label_and_unit("body_tracking_half_height_camera1_px"),
            ("Body Track Half H", "px"),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "crown_column_strengths_maximum_camera2",
                MeasurementStage.CROWN,
            ),
            ("Col Strength Max", ""),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "body_tracking_half_height_camera1_px",
                MeasurementStage.BODY,
            ),
            ("Track Half H", "px"),
        )
        self.assertEqual(
            _diagnostic_label_and_unit(
                "neck_gradient_threshold_camera1",
                MeasurementStage.NECK,
            ),
            ("Neck Gradient Thresh", ""),
        )
        self.assertEqual(_format_diagnostic_value(None), "NA")
        self.assertEqual(_format_diagnostic_value(float("nan")), "NA")
        self.assertEqual(_format_diagnostic_value([1.0, 2.25]), "[1.000, 2.250]")
        self.assertEqual(_format_diagnostic_value(True), "Yes")

    def test_process_diagnostics_keep_definition_order_and_show_na(self) -> None:
        page = PageHome()
        page._update_process_diagnostics(
            {
                "light_camera1": 10.0,
                "body_sagitta_camera1_px": None,
                "body_bottom_margin_camera1_px": 4.0,
            },
            MeasurementStage.BODY,
        )

        camera_group = page.ui.treeProcess.topLevelItem(0)
        self.assertEqual(camera_group.text(0), "Camera 1")
        self.assertEqual(
            [camera_group.child(index).text(0) for index in range(camera_group.childCount())],
            ["Light", "Sagitta", "Bottom Margin"],
        )
        self.assertEqual(camera_group.child(1).text(1), "NA")
        page.close()

    def test_invalid_measurement_message_is_written_to_log(self) -> None:
        page = PageHome()
        image = np.zeros((20, 20, 3), dtype=np.uint8)
        message = "Camera 1: Crown gradient columns below minimum (3 < 24)"
        result = MeasurementResult(
            valid=False,
            stage=MeasurementStage.BODY,
            values=MeasurementValues(),
            diagnostics={"light_camera1": 0.0, "light_camera2": 0.0, "cycle_ms": 1.0},
            preview1=image,
            preview2=image.copy(),
            overlay1=[],
            overlay2=[],
            timestamp_s=1.0,
            message=message,
        )

        page._on_measurement_result(result)

        self.assertIn(message, page.ui.txtLog.toPlainText())
        page.close()

    def test_online_camera_failure_switches_directly_to_offline(self) -> None:
        page = PageHome()
        page.ui.btnOnline.blockSignals(True)
        page.ui.btnOnline.setChecked(True)
        page.ui.btnOnline.blockSignals(False)

        page._on_online_camera_failed("CAM1 device offline")

        self.assertFalse(page.ui.btnOnline.isChecked())
        self.assertEqual(page.ui.btnOnline.text(), "Offline")
        self.assertFalse(page.running)
        self.assertIn("CAM1 device offline", page.ui.txtLog.toPlainText())
        page.close()


if __name__ == "__main__":
    unittest.main()
