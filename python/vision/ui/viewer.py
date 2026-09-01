from PySide6.QtGui import QPixmap
from vision.utils.image_utils import cv_to_qt


class Viewer:
    def __init__(self, label=None, graphics_view=None):
        self.label = label
        self.graphics_view = graphics_view

    def show_image(self, img, mode):
        qt_img = cv_to_qt(img)
        pix = QPixmap.fromImage(qt_img)

        if mode == "label" and self.label:
            self.label.setPixmap(pix)

        elif mode == "graphics" and self.graphics_view:
            scene = self.graphics_view.scene()
            scene.clear()
            scene.addPixmap(pix)