import gxipy as gx
import cv2
import numpy as np
from enum import Enum

class CameraRole(Enum):
    CAM1 = "1"
    CAM2 = "2"

    @classmethod
    def from_user_id(cls, user_id:str):
        for role in cls:
            if role.value == user_id:
                return role
        return None

STEREO_CAMERA_ROLES = (CameraRole.CAM1, CameraRole.CAM2)


class CameraDevice:
    def __init__(self, device_manager: gx.DeviceManager, dev_info: dict):
        self.dev_info = dev_info
        self.ip = dev_info["ip"]
        self.user_id = dev_info.get("user_id", "")
        self.role: CameraRole | None = CameraRole.from_user_id(self.user_id)
        self.dm = device_manager
        
        if self.role is None:
            raise ValueError(f"UnKnown camera user id: {self.user_id}")
        self.camera = None
        self.is_open = False
        self.is_streaming = False

        self.exposure = 13000
        self.gain = 0
        self.trigger_mode = gx.GxSwitchEntry.ON # 0:off, 1:on
        self.trigger_source = gx.GxTriggerSourceEntry.SOFTWARE # 0:software, 1:line0, 3:line2, 4:line3
        self.trigger_edge = gx.GxTriggerActivationEntry.FALLINGEDGE # 0:FallingEdge, 1:RisingEdge

        self.width = 1024
        self.height = 1024
        self.offsetX = 0
        self.offsetY = 0

        self.capture_callback = None

    # ---------- 生命周期 ----------
    def open(self):
        if self.is_open:
            return

        try:
            self.camera = self.dm.open_device_by_user_id(self.user_id)
        except Exception as exc:
            if not self._is_access_denied_error(exc):
                raise

            mac_address = self.dev_info.get("mac", "")
            if not mac_address:
                raise

            # 异常退出可能让千兆网相机保留控制权；仅重连一次后再次打开。
            self.dm.gige_reset_device(mac_address, gx.GxResetDeviceModeEntry.RECONNECT)
            self.dm.update_device_list()
            try:
                self.camera = self.dm.open_device_by_user_id(self.user_id)
            except Exception as retry_exc:
                raise RuntimeError(
                    f"Camera {self.user_id} reconnect succeeded but open retry failed: {retry_exc}"
                ) from retry_exc

        try:
            self.camera.AcquisitionMode.set(gx.GxAcquisitionModeEntry.CONTINUOUS)
            self.camera.TriggerMode.set(gx.GxSwitchEntry.ON)
            # self.camera.ExposureTime.set(self.exposure)
            # self.camera.Gain.set(self.gain)
        except Exception:
            try:
                self.camera.close_device()
            finally:
                self.camera = None
            raise

        self.is_open = True

    @staticmethod
    def _is_access_denied_error(exc: Exception) -> bool:
        message = str(exc).lower()
        return any(
            marker in message
            for marker in ("access denied", "-1005", "setprivilege", "ccp register")
        )

    def close(self):
        if not self.is_open and self.camera is None:
            self.is_streaming = False
            self.capture_callback = None
            return

        first_error = None
        if self.is_streaming:
            try:
                self.stop_stream()
            except Exception as exc:
                first_error = exc

        camera = self.camera
        try:
            if camera is not None:
                camera.close_device()
        except Exception as exc:
            if first_error is None:
                first_error = exc
        finally:
            # 设备掉线时 SDK 的关闭调用也可能失败，本地状态仍必须清理。
            self.camera = None
            self.is_open = False
            self.is_streaming = False
            self.capture_callback = None

        if first_error is not None:
            raise first_error

    # ---------- 采集 ----------
    def start_stream(self, callback):
        if not self.is_open or self.is_streaming:
            return

        self.capture_callback = callback
        self.camera.data_stream[0].register_capture_callback(callback)
        self.camera.stream_on()
        self.is_streaming = True

    def stop_stream(self):
        if not self.is_streaming:
            return

        first_error = None
        camera = self.camera
        try:
            if camera is not None:
                camera.stream_off()
        except Exception as exc:
            first_error = exc

        try:
            if camera is not None:
                camera.data_stream[0].unregister_capture_callback()
        except Exception as exc:
            if first_error is None:
                first_error = exc
        finally:
            self.is_streaming = False
            self.capture_callback = None

        if first_error is not None:
            raise first_error

    def soft_trigger(self):
        if self.is_streaming:
            self.camera.TriggerSoftware.send_command()

    # ---------- 参数 ----------
    def set_trigger_mode(self, value):
        self.trigger_mode = value
        if self.is_open:
            if value == 0:
                self.camera.TriggerMode.set(gx.GxSwitchEntry.OFF)
            elif value == 1:
                self.camera.TriggerMode.set(gx.GxSwitchEntry.ON)

    def get_trigger_mode_range(self):
        if self.is_open:
            return self.camera.TriggerMode.get_range()
        return None
                    
    def get_trigger_mode(self):
        if self.is_open:
            return self.camera.TriggerMode.get()
        return None
                            
    def set_exposure(self, value: float):
        self.exposure = value
        if self.is_open:
            self.camera.ExposureTime.set(value)

    def get_exposure(self):
        if self.is_open:
            return self.camera.ExposureTime.get()
        return None

    def set_gain(self, value: float):
        self.gain = value
        if self.is_open:
            self.camera.Gain.set(value)

    def get_gain(self):
        if self.is_open:
            return self.camera.Gain.get()
        return None
        
    def set_trigger_source(self, value):
        self.trigger_source = value
        if self.is_open:
            if value == 0:
                self.camera.TriggerSource.set(gx.GxTriggerSourceEntry.SOFTWARE)
            elif value == 1:
                self.camera.TriggerSource.set(gx.GxTriggerSourceEntry.LINE0)     
            elif value == 2:
                self.camera.TriggerSource.set(gx.GxTriggerSourceEntry.LINE2)     
            elif value == 3:
                self.camera.TriggerSource.set(gx.GxTriggerSourceEntry.LINE3)      

    def get_trigger_source_range(self):
        if self.is_open:
            return self.camera.TriggerSource.get_range()
        return None
    
    def get_trigger_source(self):
        if self.is_open:
            return self.camera.TriggerSource.get()
        return None
    
    def set_trigger_edge(self, value):
        self.trigger_edge = value
        if self.is_open:
            if value == 0:
                self.camera.TriggerActivation.set(gx.GxTriggerActivationEntry.FALLINGEDGE)
            elif value == 1:
                self.camera.TriggerActivation.set(gx.GxTriggerActivationEntry.RISINGEDGE)       

    def get_trigger_edge_range(self):
        if self.is_open:
            return self.camera.TriggerActivation.get_range()
        return None     
    
    def get_trigger_edge(self):
        if self.is_open:
            return self.camera.TriggerActivation.get()
        return None        
    
    def set_width(self, value: int):
        aligned_value = (value // 8) * 8
        self.width = aligned_value
        if self.is_open:
            self.camera.Width.set(aligned_value) 

    def get_width(self):
        if self.is_open:
            return self.camera.Width.get()
        return None
    
    def set_height(self, value: int):
        aligned_value = (value // 2) * 2
        self.height = aligned_value
        if self.is_open:
            self.camera.Height.set(aligned_value)                

    def get_height(self):
        if self.is_open:
            return self.camera.Height.get()
        return None
    
    def set_offsetX(self, value: int):
        aligned_value = (value // 8) * 8
        self.offsetX = aligned_value
        if self.is_open:
            self.camera.OffsetX.set(aligned_value) 

    def get_offsetX(self):
        if self.is_open:
            return self.camera.OffsetX.get()
        return None
    
    def set_offsetY(self, value: int):
        aligned_value = (value // 2) * 2
        self.offsetY = aligned_value
        if self.is_open:
            self.camera.OffsetY.set(aligned_value)    

    def get_offsetY(self):
        if self.is_open:
            return self.camera.OffsetY.get()
        return None            
