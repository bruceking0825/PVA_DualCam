from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QLabel
# 自定义 PressableLabel 类（基于你提供的 C++ 定义）
class PressableLabel(QLabel):
    clicked = Signal()
    doubleClicked = Signal()

    def __init__(self, text, parent=None):
        super().__init__(text, parent)
        self.setMouseTracking(True)  # 启用鼠标跟踪，与 C++ 一致

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.clicked.emit()
        super().mousePressEvent(event)

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.doubleClicked.emit()
        super().mouseDoubleClickEvent(event)
# 简单的数据结构替代 IniEntry
class IniEntry:
    def __init__(self, group, key, value):
        self.group = group
        self.key = key
        self.value = value

# FormRow 数据结构
class FormRow:
    def __init__(self, widget=None, label=None, edit=None, key=""):
        self.widget = widget
        self.label = label
        self.edit = edit
        self.key = key
