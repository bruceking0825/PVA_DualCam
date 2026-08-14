import cv2
from PySide6.QtWidgets import (
    QMainWindow, QPushButton, QVBoxLayout, QWidget, QLabel, QHBoxLayout
)
from PySide6.QtGui import QPixmap

from ui.editor.node_scene import NodeScene
from ui.editor.node_view import NodeView

from services.pipeline_service import PipelineService
from utils.image_utils import cv_to_qt

from ui.panels.toolbox_panel import ToolboxPanel
from ui.viewer import Viewer

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.scene = NodeScene()
        self.view = NodeView(self.scene)

        self.label = QLabel("Result")

        # =========================
        # service
        # =========================
        self.service = PipelineService()
        self.scene.graph = self.service.graph

        self.toolbox = ToolboxPanel()

        # =========================
        # 顶部按钮
        # =========================
        self.btn_run = QPushButton("Run")

        top_layout = QHBoxLayout()
        top_layout.addWidget(self.btn_run)

        self.btn_save = QPushButton("Save")
        top_layout.addWidget(self.btn_save)
        self.btn_load = QPushButton("Load")
        top_layout.addWidget(self.btn_load)

        # =========================
        # 中间主区域（关键修复）
        # =========================
        middle_layout = QHBoxLayout()
        middle_layout.addWidget(self.toolbox)   # 左侧
        middle_layout.addWidget(self.view)      # 右侧

        # =========================
        # 总布局
        # =========================
        main_layout = QVBoxLayout()
        main_layout.addLayout(top_layout)
        main_layout.addLayout(middle_layout)
        main_layout.addWidget(self.label)

        container = QWidget()
        container.setLayout(main_layout)
        self.setCentralWidget(container)


        self.btn_run.clicked.connect(self.run_pipeline)
        self.btn_save.clicked.connect(self.save)
        self.btn_load.clicked.connect(self.load)

        self.node_count = 0

        self.scene.selectionChanged.connect(self.on_selection_changed)
        self.viewer = Viewer(label=self.label, graphics_view=self.view)


    def run_pipeline(self):
        result = self.service.run(self.scene.graph)
        qt_img = cv_to_qt(result)
        self.label.setPixmap(QPixmap.fromImage(qt_img))

    def on_selection_changed(self):
        items = self.scene.selectedItems()

        if not items:
            return

        item = items[0]

        # 只处理 NodeItem
        if hasattr(item, "node"):
            node = item.node
            self.show_node_image(node)

    def show_node_image(self, node):
        if node.output_image is None:
            self.label.setText("No output")
            return

        qt_img = cv_to_qt(node.output_image)
        self.label.setPixmap(QPixmap.fromImage(qt_img))

    def save(self):
        self.service.save_graph(self.scene, "graph.json")

    def load(self):
        self.service.load_graph(self.scene, "graph.json")