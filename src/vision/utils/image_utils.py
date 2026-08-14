import cv2
from PySide6.QtGui import QImage
import numpy as np

def cv_to_qt(img):
    if img is None:
        return None
    # ⭐保证连续
    if not img.flags['C_CONTIGUOUS']:
        img = np.ascontiguousarray(img)

    img_rgb = cv2.cvtColor(img, cv2.COLOR_GRAY2RGB) if len(img.shape) != 3 else img
    h, w, ch = img_rgb.shape
    bytes_per_line = ch * w
    return QImage(img_rgb.data, w, h, bytes_per_line, QImage.Format_BGR888)