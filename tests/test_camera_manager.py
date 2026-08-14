import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from modules.camera_device import CameraRole
from modules.camera_manager import CameraManager


class _FakeCamera:
    def __init__(self, error: Exception | None = None):
        self.error = error
        self.close_calls = 0

    def close(self) -> None:
        self.close_calls += 1
        if self.error is not None:
            raise self.error


class CameraManagerTests(unittest.TestCase):
    def test_close_all_continues_after_one_camera_fails(self) -> None:
        manager = CameraManager.__new__(CameraManager)
        camera1 = _FakeCamera(RuntimeError("device offline"))
        camera2 = _FakeCamera()
        manager.cameras = {
            CameraRole.CAM1: camera1,
            CameraRole.CAM2: camera2,
        }

        errors = manager.close_all()

        self.assertEqual(camera1.close_calls, 1)
        self.assertEqual(camera2.close_calls, 1)
        self.assertEqual(len(errors), 1)
        self.assertIn("CAM1", errors[0])
        self.assertIn("device offline", errors[0])


if __name__ == "__main__":
    unittest.main()
