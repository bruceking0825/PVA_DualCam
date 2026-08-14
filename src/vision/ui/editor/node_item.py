from PySide6.QtWidgets import QGraphicsItem, QMenu
from PySide6.QtGui import QPainter, QPen, QPixmap
from PySide6.QtCore import QRectF, Qt
from vision.ui.editor.port_item import PortItem
from vision.ui.dialogs.param_dialog import ParamDialog
from vision.utils.image_utils import cv_to_qt
from vision.nodes.start_node import StartNode
from vision.nodes.end_node import EndNode

class NodeItem(QGraphicsItem):
    def __init__(self, node, scene):
        super().__init__()
        self.node = node
        self.scene_ref = scene
        self.preview = None
        self.setFlag(QGraphicsItem.ItemIsMovable)
        self.setFlag(QGraphicsItem.ItemIsSelectable)
        self.setFlag(QGraphicsItem.ItemSendsGeometryChanges)
        self.setFlag(QGraphicsItem.ItemIsFocusable, True)
        # ⭐关键：允许子Item接收事件
        self.setHandlesChildEvents(False)
        
        self.width = 120
        self.height = 60
        # ⭐注册节点
        # self.scene_ref.graph.add_node(self.node)

        self.input_port = PortItem(self, is_output=False)
        self.input_port.setPos(0, self.height / 2)

        self.output_port = PortItem(self, is_output=True)
        self.output_port.setPos(self.width, self.height / 2)

        # self.input_port = PortItem(self, is_output=False)
        # self.input_port.setPos(0, self.height / 2)

        # self.output_port = PortItem(self, is_output=True)
        # self.output_port.setPos(self.width, self.height / 2)
        if isinstance(self.node, StartNode):
            self.input_port.setVisible(False)

        if isinstance(self.node, EndNode):
            self.output_port.setVisible(False)

    def boundingRect(self):
        return QRectF(0, 0, self.width, self.height)

    def paint(self, painter, option, widget):
        pen = QPen(Qt.red if self.isSelected() else Qt.darkGreen, 2)
        painter.setPen(pen)
        painter.drawRect(0, 0, self.width, self.height)
        painter.drawText(10, 20, self.node.name)

        # painter.drawPath(self.path())

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionChange:
            # ⭐更新所有连接的边
            self.update_edges()

        return super().itemChange(change, value)

    def update_edges(self):
        for port in [self.input_port, self.output_port]:
            for edge in port.edges:
                edge.update_path()
    # def mousePressEvent(self, event):
    #     # 如果点在端口上，不允许拖动节点
    #     items = self.childItems()
    #     for item in items:
    #         if item.isUnderMouse():
    #             event.ignore()
    #             return

    #     super().mousePressEvent(event)

    def contextMenuEvent(self, event):
        menu = QMenu()

        delete_action = menu.addAction("Delete Node")

        action = menu.exec(event.screenPos())

        if action == delete_action:
            self.remove()

    def remove(self):
        scene = self.scene()

        # 1️⃣ 删除所有 edge（从两个端口）
        for port in [self.input_port, self.output_port]:
            # ⚠️ copy 一份列表，避免遍历时修改
            for edge in list(port.edges):
                edge.remove()

        # 2️⃣ 从 Graph 中删除节点
        graph = scene.graph
        node = self.node

        if node in graph.nodes:
            graph.nodes.remove(node)

        # 删除所有指向它的边（反向）
        for src, targets in graph.edges.items():
            if node in targets:
                targets.remove(node)

        # 删除它自己的边列表
        if node in graph.edges:
            del graph.edges[node]

        # 3️⃣ 从 Scene 删除
        scene.removeItem(self)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Delete:
            self.remove()

    def mouseDoubleClickEvent(self, event):
        dialog = ParamDialog(self.node)
        dialog.exec()
