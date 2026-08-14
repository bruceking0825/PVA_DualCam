from PySide6.QtWidgets import QGraphicsEllipseItem
from PySide6.QtCore import Qt
from vision.ui.editor.edge_item import EdgeItem


class PortItem(QGraphicsEllipseItem):
    def __init__(self, parent, is_output=True):
        super().__init__(-5, -5, 10, 10, parent)

        self.setBrush(Qt.black)
        self.is_output = is_output
        
        self.edges = []   # ⭐关键：一个port可以有多条边

        self.setZValue(10)
        self.setAcceptHoverEvents(True)
        self.setAcceptedMouseButtons(Qt.LeftButton)

    def mousePressEvent(self, event):

        if self.is_output:
            # self.scene()就是NodeScene的实例
            self.scene().temp_edge = EdgeItem(self)
            self.scene().addItem(self.scene().temp_edge)
        # ⭐关键1：接受事件（阻止冒泡）
        event.accept()
        # ⭐关键：抓住鼠标
        # self.grabMouse()

        # super().mousePressEvent(event)

    def mouseMoveEvent(self, event):

        if self.scene().temp_edge:
            # self.scene().temp_edge.update_path()
            self.scene().temp_edge.update_end(event.scenePos())

        event.accept()

    def mouseReleaseEvent(self, event):

        temp_edge = self.scene().temp_edge
        if not temp_edge:
            return

        connected = False

        items = self.scene().items(event.scenePos())

        for item in items:
            if isinstance(item, PortItem) and not item.is_output:
                # ❗禁止连接自己
                source_node = self.parentItem().node
                target_node = item.parentItem().node

                if source_node == target_node:
                    continue          

                # ❗关键限制：input只能连一个
                if item.edges:
                    # 已经有连接 → 拒绝
                    connected = False
                    break                
                temp_edge.set_target(item)
                connected = True
                break

        # ❗关键：如果没有连接成功 → 删除悬空边
        if not connected:
            self.scene().removeItem(temp_edge)

            # ⭐从 source_port 移除（防止脏数据）
            if temp_edge in self.edges:
                self.edges.remove(temp_edge)

        self.scene().temp_edge = None

        # self.ungrabMouse()
        event.accept()