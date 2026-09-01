import numpy as np
from PySide6.QtWidgets import QFileDialog, QGraphicsView, QGraphicsScene, QGraphicsItem, QGraphicsItemGroup, QGraphicsLineItem, QGraphicsPixmapItem, QGraphicsTextItem, QMenu
from PySide6.QtGui import QImage, QPixmap, QPainter, QColor, QPen
from PySide6.QtCore import QPoint, Qt, QRectF, Signal, QTimer
import pyqtgraph as pg

from .graphics_figure_io import add_contour_element
from .graphics_figure_io import load_figure as load_graphics_figure
from .graphics_figure_io import save_figure as save_graphics_figure

# ===============================
# 轮廓点图元（屏幕大小恒定）
# ===============================
from .graphics_contour_item import ContourItem
                
# ===============================
# 主视图
# ===============================

class CustomGraphicsView(QGraphicsView):

    update_info_signal = Signal(int, int, int, int)
    image_loaded_signal = Signal(np.ndarray)

    def __init__(self, parent=None, view_id=1):
        super().__init__(parent)
        # Designer 创建控件时不会额外传入编号，先保存默认 view_id，避免鼠标取点时报错。
        self.view_id = view_id
        # 如果 Designer 没设置 scene，则创建
        self.scene_obj = QGraphicsScene(self)
        self.setScene(self.scene_obj)
        # 图片层
        self.image_item = QGraphicsPixmapItem()
        self.scene_obj.addItem(self.image_item)
        self.contour_item = None
        # 文字层
        self.text_item = QGraphicsTextItem()
        self.text_item.setDefaultTextColor(Qt.yellow)
        self.scene_obj.addItem(self.text_item)
        # 曲线图
        self.plot_widget = None
        self.plot_proxy = None
        self.mode = "image"   # 或 "curve"
        self.image_scale = 1.0
        # 轮廓层和光标层在需要时动态创建和添加到 scene 中
        self.contour_item = None
        self.cursor_item = None
        # 默认都隐藏
        self.image_item.setVisible(False)
        self.text_item.setVisible(False)

        self._panning = False
        self._pan_start = QPoint()
        # 滚轮缩放可能触发滚动条变化并间接触发 resizeEvent，此时不能自动 fit。
        self._suppress_resize_fit = False
        
        self.original_image = None
        self.setViewportUpdateMode(QGraphicsView.MinimalViewportUpdate)
        # self.setViewportUpdateMode(QGraphicsView.NoViewportUpdate)
        self.setRenderHint(QPainter.Antialiasing)
        # self.setDragMode(QGraphicsView.ScrollHandDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.AnchorUnderMouse)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.StrongFocus)

    def resizeEvent(self, event):
        """视图尺寸变化后自动让图像铺满可视区域，避免 splitter 调整后留下空白。"""

        super().resizeEvent(event)
        if not self._suppress_resize_fit:
            self.fit_in_view()

    def _release_resize_fit_suppression(self):
        """释放滚轮缩放保护，让后续真正的窗口或 splitter 尺寸变化继续自动铺满图像。"""

        self._suppress_resize_fit = False

    def set_view_id(self, view_id):
        """设置视图 ID"""
        self.view_id = view_id

    def set_text(self, text: str):
        self.resetTransform()

        # 清空图片内容（逻辑上清空）
        self.image_item.setVisible(False)

        # 更新文字
        self.text_item.setPlainText(text)
        self.text_item.setVisible(True)

        rect = self.text_item.boundingRect()
        self.text_item.setPos(-rect.width() / 2, -rect.height() / 2)

        self.scene_obj.setSceneRect(rect)
        self.centerOn(self.text_item)

    def load_image(self, file_path: str):
        pixmap = QPixmap(file_path)
        if pixmap.isNull():
            self.set_text("无法加载图片")
            return

        self.show_pixmap(pixmap)

    def get_image(self):
        """获取当前显示的图片"""
        return self.original_image
    # ===============================
    # 加载图像
    # ===============================
    def toggle_view(self, mode):
        self.mode = mode
        if mode == "image":
            if self.plot_proxy:
                self.plot_proxy.setVisible(False)               
            self.set_scale(self.image_scale)
        else:
            self.resetTransform()



    def show_pixmap(self, pixmap: QPixmap, preserve_view: bool = False):
        had_image = self.image_item.isVisible() and not self.image_item.pixmap().isNull()
        keep_current_view = preserve_view and had_image
        previous_transform = self.transform()
        previous_scroll_x = self.horizontalScrollBar().value()
        previous_scroll_y = self.verticalScrollBar().value()
        # 更新图片内容
        self.image_item.setPixmap(pixmap)
        self.image_item.setVisible(True)

        # 隐藏文字
        self.text_item.setVisible(False)
        self.scene_obj.setSceneRect(pixmap.rect())
        # 隐藏曲线
        if self.plot_proxy:
            self.plot_proxy.setVisible(False)   

        self.original_image = pixmap.toImage()

        # 如果没有轮廓层，创建
        if not self.contour_item:
            rect = self.image_item.boundingRect()
            self.contour_item = ContourItem(rect)
            self.scene_obj.addItem(self.contour_item)

        else:
            self.contour_item.setVisible(True)
            self.contour_item.clear_elements()
            self.contour_item._rect = QRectF(pixmap.rect())

        if keep_current_view:
            # Keep the user's zoom and viewing position during live image updates.
            self.setTransform(previous_transform)
            self.horizontalScrollBar().setValue(previous_scroll_x)
            self.verticalScrollBar().setValue(previous_scroll_y)
            self.image_scale = self.transform().m11()
        else:
            self.fit_in_view()
    
    # 添加轮廓
    # ===============================

    # 显示曲线
    def show_curve_plot(self, x, y, mark_index):
        # ---------- 隐藏图像相关 ----------
        self.image_item.setVisible(False)
        self.text_item.setVisible(False)

        if self.contour_item:
            self.contour_item.setVisible(False)

        # ---------- 创建 PlotWidget（只创建一次） ----------
        if self.plot_widget is None:
            self.plot_widget = pg.PlotWidget()
            self.plot_widget.setBackground('k')  # 可选：黑底

            # 加入 scene
            self.plot_proxy = self.scene_obj.addWidget(self.plot_widget)
            self.plot_proxy.setZValue(10)  # 保证在最上层

        # ---------- 更新数据 ----------
        self.plot_widget.clear()
        self.plot_widget.plot(x, y, pen='y')

        # ---------- 显示 ----------
        self.plot_proxy.setVisible(True)

        # 提取对应点
        x_mark = x[mark_index]
        y_mark = y[mark_index]

        # 画点
        self.plot_widget.plot(
            x_mark, y_mark,
            pen=None,
            symbol='o',
            symbolBrush='r',
            symbolSize=8
        )

        # ---------- 自适应尺寸 ----------
        view_rect = self.viewport().rect()
        self.plot_widget.resize(view_rect.width(), view_rect.height())

        self.scene_obj.setSceneRect(0, 0, view_rect.width(), view_rect.height())

    def update_contours(self, elements):
        """
        elements: list of dict
        """
        if not self.contour_item:
            return

        self.contour_item.clear_elements()

        for element in elements:
            add_contour_element(self.contour_item, element)

        # 更新轮廓层，确保外部调用 update_contours 后立即刷新视图。
        self.contour_item.update()
    # ===============================
    # 鼠标点击选点
    # ===============================

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton and self.image_item:
            scene_pos = self.mapToScene(event.pos())
            x = int(scene_pos.x())
            y = int(scene_pos.y())

            if self.original_image:
                if (0 <= x < self.original_image.width() and
                    0 <= y < self.original_image.height()):

                    self.draw_cursor(x, y)
                    self.update_pixel_info(x, y)

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

    # ===============================
    # 绘制当前点
    # ===============================

    def draw_cursor(self, x, y):
        if self.cursor_item:
            self.scene_obj.removeItem(self.cursor_item)

        pen = QPen(QColor(0, 255, 0))
        pen.setWidth(2)
        self.cursor_item = QGraphicsItemGroup()
        self.cursor_item.addToGroup(QGraphicsLineItem(-9, 0, 9, 0))
        self.cursor_item.addToGroup(QGraphicsLineItem(0, -9, 0, 9))
        for child in self.cursor_item.childItems():
            child.setPen(pen)

        self.cursor_item.setPos(x + 0.5, y + 0.5)
        # 关键：让十字线大小不随缩放变化
        self.cursor_item.setFlag(
            QGraphicsItem.ItemIgnoresTransformations, True
        )

        self.scene_obj.addItem(self.cursor_item)

    # ===============================
    # 键盘移动
    # ===============================

    def keyPressEvent(self, event):
        if not self.cursor_item:
            return super().keyPressEvent(event)

        move = 1
        dx, dy = 0, 0

        if event.key() == Qt.Key_Up:
            dy = -move
        elif event.key() == Qt.Key_Down:
            dy = move
        elif event.key() == Qt.Key_Left:
            dx = -move
        elif event.key() == Qt.Key_Right:
            dx = move
        else:
            return super().keyPressEvent(event)

        # ✅ 使用当前光标位置
        pos = self.cursor_item.pos()

        new_x = int(round(pos.x() - 0.5)) + dx
        new_y = int(round(pos.y() - 0.5)) + dy
        if (0 <= new_x < self.original_image.width() and
            0 <= new_y < self.original_image.height()):
            self.draw_cursor(new_x, new_y)
            self.update_pixel_info(new_x, new_y)

    # ===============================
    # 滚轮缩放
    # ===============================

    def wheelEvent(self, event):

        # ---------------- 图像模式 ----------------
        if self.mode == "image":
            zoom_in_factor = 1.15
            zoom_out_factor = 1 / zoom_in_factor

            if event.angleDelta().y() > 0:
                factor = zoom_in_factor
            else:
                factor = zoom_out_factor

            current_scale = self.transform().m11()
            self.image_scale = current_scale * factor

            if self.image_scale < 0.001 or self.image_scale > 20:
                return

            self._suppress_resize_fit = True
            self.scale(factor, factor)
            QTimer.singleShot(0, self._release_resize_fit_suppression)

        event.accept()

    # ===============================
    # 读取灰度值
    # ===============================

    def update_pixel_info(self, x, y):
        pixel = QColor(self.original_image.pixel(x, y))

        gray = int(0.299 * pixel.red() +
                   0.587 * pixel.green() +
                   0.114 * pixel.blue())

        self.update_info_signal.emit(
            self.view_id, x, y, gray
        )

    def full_fill(self):
        """视图大小改变时重新填充"""
        self.resetTransform()
        # self.centerOn(self.image_item)

    def fit_in_view(self):    
        if self.mode == "image" and self.image_item.isVisible() and not self.image_item.pixmap().isNull():
            self.fitInView(self.image_item, Qt.KeepAspectRatio)
            self.image_scale = self.transform().m11()

    def set_scale(self, target_scale):
        self.resetTransform()                  # 清空当前变换
        self.scale(target_scale, target_scale) # 设置为指定缩放


    def contextMenuEvent(self, event):
        # ===== 曲线模式 =====
        if self.mode == "curve":
            event.ignore()
            return

        menu = QMenu(self)  

        act_png = menu.addAction("Save as PNG")
        act_fig = menu.addAction("Save as Figure")
        act_load = menu.addAction("Load Figure")

        action = menu.exec(event.globalPos())

        if action == act_png:
            self.save_as_png()
        elif action == act_fig:
            self.save_as_figure()
        elif action == act_load:
            self.load_figure_dialog()

    def save_as_png(self, file_path: str | None = None):
        if file_path is None:
            file_path, _ = QFileDialog.getSaveFileName(
                self, "保存 PNG", "", "PNG Files (*.png)"
            )
        if not file_path or self.original_image is None:
            return

        rect = self.scene_obj.sceneRect()
        image = QImage(rect.size().toSize(), QImage.Format_ARGB32)
        image.fill(Qt.black)

        painter = QPainter(image)
        old_export_scale = self.contour_item.export_scale if self.contour_item is not None else None
        if self.contour_item is not None:
            self.contour_item.export_scale = 0.25
        try:
            self.scene_obj.render(painter)
        finally:
            painter.end()
            if self.contour_item is not None:
                self.contour_item.export_scale = old_export_scale

        image.save(file_path)

    def save_as_figure(self):
        file_path, _ = QFileDialog.getSaveFileName(
            self, "?? Figure", "", "Figure Files (*.json)"
        )
        if not file_path or self.original_image is None:
            return
        save_graphics_figure(self, file_path)

    def load_figure_dialog(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self, "?? Figure", "", "Figure Files (*.json)"
        )
        if file_path:
            self.load_figure(file_path)

    def load_figure(self, file_path):
        load_graphics_figure(self, file_path)
            
