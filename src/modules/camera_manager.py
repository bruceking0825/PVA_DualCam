import gxipy as gx
from .camera_device import CameraDevice, CameraRole


class CameraManager:
    def __init__(self):
        self.dm = gx.DeviceManager()
        self.cameras: dict[CameraRole, CameraDevice] = {}

    def enumerate(self):
        dev_num, dev_info_list = self.dm.update_device_list()
        self.cameras.clear()

        for dev_info in dev_info_list:
            user_id = dev_info.get("user_id", "")
            role = CameraRole.from_user_id(user_id)
            if role is None:
                continue
            self.cameras[role] = CameraDevice(self.dm, dev_info)

        return self.cameras

    def get_by_role(self, role: CameraRole) -> CameraDevice | None:
        return self.cameras.get(role)

    def get_all(self) -> list[CameraDevice]:
        return list(self.cameras.values())

    def close_all(self) -> list[str]:
        errors = []
        for role, cam in self.cameras.items():
            try:
                # close() 同时负责停流；单台掉线不能阻止另一台相机释放。
                cam.close()
            except Exception as exc:
                errors.append(f"{role.name}: {exc}")
        return errors
