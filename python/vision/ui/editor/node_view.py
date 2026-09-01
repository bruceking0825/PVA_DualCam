from PySide6.QtWidgets import QGraphicsView
from PySide6.QtGui import QPainter
from PySide6.QtCore import QPoint, Qt


class NodeView(QGraphicsView):
    def __init__(self, scene):
        super().__init__(scene)
        self._panning = False
        self._pan_start = QPoint()

        self.setRenderHints(QPainter.Antialiasing)
        self.setViewportUpdateMode(QGraphicsView.FullViewportUpdate)
        # self.setDragMode(QGraphicsView.ScrollHandDrag)

    def wheelEvent(self, event):
        scale = 1.2 if event.angleDelta().y() > 0 else 0.8
        self.scale(scale, scale)

    # -------------------------
    # 鼠标按下
    # -------------------------
    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            item = self.itemAt(event.pos())

            # ⭐只有点击空白才进入平移
            if item is None:
                self._panning = True
                self._pan_start = event.pos()
                self.setCursor(Qt.ClosedHandCursor)
                return

        super().mousePressEvent(event)

    # -------------------------
    # 鼠标移动
    # -------------------------
    def mouseMoveEvent(self, event):
        if self._panning:
            delta = event.pos() - self._pan_start
            self._pan_start = event.pos()

            # ⭐移动视图
            self.horizontalScrollBar().setValue(
                self.horizontalScrollBar().value() - delta.x()
            )
            self.verticalScrollBar().setValue(
                self.verticalScrollBar().value() - delta.y()
            )
            return

        super().mouseMoveEvent(event)

    # -------------------------
    # 鼠标释放
    # -------------------------
    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton and self._panning:
            self._panning = False
            self.setCursor(Qt.ArrowCursor)
            return

        super().mouseReleaseEvent(event)
