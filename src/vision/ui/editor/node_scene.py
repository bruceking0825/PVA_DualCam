from PySide6.QtWidgets import QGraphicsScene
from PySide6.QtCore import Qt, QPointF
from vision.core.graph import Graph
from vision.nodes.registry import NODE_REGISTRY
from vision.ui.editor.node_item import NodeItem


class NodeScene(QGraphicsScene):
    def __init__(self):
        super().__init__()
        self.graph = Graph()
        self.temp_edge = None


    def dragEnterEvent(self, event):
        event.acceptProposedAction()

    def dragMoveEvent(self, event):
        event.acceptProposedAction()

    def dropEvent(self, event):
        node_type = event.mimeData().text()

        if node_type not in NODE_REGISTRY:
            return

        node = NODE_REGISTRY[node_type]()

        item = NodeItem(node, self)

        pos = event.scenePos()
        item.setPos(pos)

        self.addItem(item)

        # ⭐关键：注册到 Graph
        self.graph.add_node(node)

        event.acceptProposedAction()