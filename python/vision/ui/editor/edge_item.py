from PySide6.QtWidgets import QGraphicsPathItem, QMenu, QGraphicsItem
from PySide6.QtGui import QPainterPath, QPen
from PySide6.QtCore import QPointF, Qt


# edge是从port出来的直线或者叫边
class EdgeItem(QGraphicsPathItem):
    def __init__(self, source_port):
        super().__init__()
        self.source_port = source_port
        self.target_port = None
        self.end_pos = None  # 拖拽时用
        self.setPen(QPen(Qt.blue, 2))
        # ⭐注册到source
        self.source_port.edges.append(self)
        self.setFlag(QGraphicsItem.ItemIsSelectable)
        self.setFlag(QGraphicsItem.ItemIsFocusable, True)

    def paint(self, painter, option, widget):
        pen = QPen(Qt.red if self.isSelected() else Qt.blue, 2)
        painter.setPen(pen)
        painter.drawPath(self.path())

    def update_end(self, pos):
        self.end_pos = pos
        self.update_path()

    def set_target(self, target_port):
        self.target_port = target_port
        # ⭐防止空
        if not self.source_port or not target_port:
            return   
        # ⭐注册到target
        target_port.edges.append(self)         
        self.update_path()           
        # self.draw_path(self.source_port.scenePos(), target_port.scenePos())

        # ⭐关键：建立节点关系
        source_node = self.source_port.parentItem().node
        target_node = target_port.parentItem().node

        self.scene().graph.connect(source_node, target_node)

    def intersects_node(self, path):
        for item in self.scene().items():
            if item == self:
                continue

            if hasattr(item, "node"):  # NodeItem
                rect = item.sceneBoundingRect()  # ⭐关键

                if path.intersects(rect):
                    return True
        return False

    def update_path(self):
        if not self.source_port:
            return

        p1 = self.source_port.scenePos()

        if self.target_port:
            p2 = self.target_port.scenePos()
        elif self.end_pos:
            p2 = self.end_pos
        else:
            return

        dx = abs(p2.x() - p1.x())

        # 默认路径
        def make_path(offset_y=0):
            path = QPainterPath()
            path.moveTo(p1)

            ctrl1 = QPointF(p1.x() + dx * 0.5, p1.y() + offset_y)
            ctrl2 = QPointF(p2.x() - dx * 0.5, p2.y() + offset_y)

            path.cubicTo(ctrl1, ctrl2, p2)
            return path

        # 尝试不同偏移
        for offset in [0, -80, 80, -150, 150]:
            path = make_path(offset)
            if not self.intersects_node(path):
                self.setPath(path)
                return

        # 实在不行就用默认
        self.setPath(make_path())

    def contextMenuEvent(self, event):
        menu = QMenu()

        delete_action = menu.addAction("Delete Edge")

        action = menu.exec(event.screenPos())

        if action == delete_action:
            self.remove()
                
    def remove(self):
        # 1️⃣ 从 source port 移除
        if self.source_port and self in self.source_port.edges:
            self.source_port.edges.remove(self)

        # 2️⃣ 从 target port 移除
        if self.target_port and self in self.target_port.edges:
            self.target_port.edges.remove(self)

        # 3️⃣ 从 Graph 中移除连接（❗很多人漏掉）
        if self.source_port and self.target_port:
            source_node = self.source_port.parentItem().node
            target_node = self.target_port.parentItem().node

            graph = self.scene().graph

            if target_node in graph.edges.get(source_node, []):
                graph.edges[source_node].remove(target_node)

        # 4️⃣ 从 Scene 删除
        self.scene().removeItem(self)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Delete:
            self.remove()