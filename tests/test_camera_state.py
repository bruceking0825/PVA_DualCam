from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from modules.camera_state import CameraStateStore  # noqa: E402


class CameraStateStoreTests(unittest.TestCase):
    def test_missing_state_uses_clamped_initial_exposures(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = CameraStateStore(Path(directory) / "camera_state.json")
            values = store.load_exposures(
                {"camera1": 500.0, "camera2": 90000.0},
                1000.0,
                80000.0,
            )

        self.assertEqual(values, {"camera1": 1000.0, "camera2": 80000.0})

    def test_saved_exposures_are_loaded_and_clamped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "camera_state.json"
            store = CameraStateStore(path)
            store.save_exposures({"camera1": 18500.0, "camera2": 95000.0})

            values = store.load_exposures(
                {"camera1": 13000.0, "camera2": 13000.0},
                1000.0,
                80000.0,
            )
            payload = json.loads(path.read_text(encoding="utf-8"))

        self.assertEqual(values, {"camera1": 18500.0, "camera2": 80000.0})
        self.assertEqual(payload["version"], 1)

    def test_invalid_state_reports_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "camera_state.json"
            path.write_text("not-json", encoding="ascii")

            with self.assertRaisesRegex(ValueError, "Cannot read camera state"):
                CameraStateStore(path).load_exposures(
                    {"camera1": 13000.0},
                    1000.0,
                    80000.0,
                )


if __name__ == "__main__":
    unittest.main()
