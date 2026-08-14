from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path


os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from PySide6.QtGui import QColor, QPixmap  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from widgets.cutom_widgets import CustomGraphicsView  # noqa: E402
from widgets.graphics_contour_item import ContourItem  # noqa: E402


class RecordingPainter:
    def __init__(self) -> None:
        self.lines = []

    def drawLine(self, start, end) -> None:
        self.lines.append((start, end))


class CustomGraphicsViewTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = QApplication.instance() or QApplication([])

    def test_preserve_view_keeps_zoom_between_pixmap_updates(self) -> None:
        view = CustomGraphicsView()
        view.resize(400, 300)
        view.show()
        self.app.processEvents()

        first = QPixmap(800, 600)
        first.fill(QColor("black"))
        view.show_pixmap(first, preserve_view=True)
        fitted_scale = view.transform().m11()

        view.scale(1.75, 1.75)
        zoomed_scale = view.transform().m11()
        self.assertGreater(zoomed_scale, fitted_scale)
        view.horizontalScrollBar().setValue(view.horizontalScrollBar().maximum() // 3)
        view.verticalScrollBar().setValue(view.verticalScrollBar().maximum() // 3)
        scroll_x = view.horizontalScrollBar().value()
        scroll_y = view.verticalScrollBar().value()

        for _ in range(20):
            second = QPixmap(800, 600)
            second.fill(QColor("white"))
            view.show_pixmap(second, preserve_view=True)

        self.assertAlmostEqual(view.transform().m11(), zoomed_scale, places=10)
        self.assertAlmostEqual(view.image_scale, zoomed_scale, places=10)
        self.assertEqual(view.horizontalScrollBar().value(), scroll_x)
        self.assertEqual(view.verticalScrollBar().value(), scroll_y)
        view.close()

    def test_contour_cross_keeps_constant_screen_size(self) -> None:
        item = ContourItem(QPixmap(100, 100).size())
        element = {"data": (40.0, 50.0, 12.0)}

        for scale in (0.5, 1.0, 2.0, 4.0):
            painter = RecordingPainter()
            item._draw_cross(painter, element, scale)
            horizontal_start, horizontal_end = painter.lines[0]
            scene_width = horizontal_end.x() - horizontal_start.x()
            self.assertAlmostEqual(scene_width * scale, 24.0, places=10)


if __name__ == "__main__":
    unittest.main()
