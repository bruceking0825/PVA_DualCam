from PySide6.QtWidgets import QListWidget, QListWidgetItem
from PySide6.QtCore import Qt, QMimeData
from PySide6.QtGui import QDrag

from vision.nodes.registry import NODE_REGISTRY


class ToolboxPanel(QListWidget):
    def __init__(self):
        super().__init__()

        self.setDragEnabled(True)

        self.load_nodes()

    def load_nodes(self):
        for name in NODE_REGISTRY.keys():
            item = QListWidgetItem(name)
            self.addItem(item)

    def startDrag(self, supportedActions):
        item = self.currentItem()
        if not item:
            return

        mime = QMimeData()
        mime.setText(item.text())

        drag = QDrag(self)
        drag.setMimeData(mime)
        drag.exec(Qt.CopyAction)