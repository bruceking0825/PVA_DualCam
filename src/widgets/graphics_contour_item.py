import numpy as np
from PySide6.QtCore import QPointF, QRectF, QSize
from PySide6.QtGui import QColor, QFontMetrics, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QGraphicsItem


class ContourItem(QGraphicsItem):

    def __init__(self, image_size: QSize):
        super().__init__()

        self.image_rect = QRectF(0, 0, image_size.width(), image_size.height())


        # 所有图元存在这里
        self.elements = []
        self.export_scale = None
        # {
        #     "type": "points" | "line" | "rectangle" | "circle",
        #     "data": ...,
        #     "color": QColor,
        #     "width": int
        # }
        # 如果想让点不随缩放变大
        # self.setFlag(QGraphicsItem.ItemIgnoresTransformations, True)

    # ----------- 数据接口 -----------
    def clear_elements(self):
        self.elements.clear()
        # self.update()

    def add_points(self, data, color, width=1):
        self.elements.append({
            "type": "points",
            "data": data,
            "color": color,
            "width": width
        })
        # self.update()

    def add_line(self, data, color, width=2):
        # line = (x1, y1, x2, y2)
        self.elements.append({
            "type": "line",
            "data": data,
            "color": color,
            "width": width
        })
        # self.update()

    def add_polyline(self, data, color, width=2, closed=False):
        self.elements.append({
            "type": "polyline",
            "data": data,
            "color": color,
            "width": width,
            "closed": closed,
        })

    def add_rectangle(self, data, color, width=2, font_size=10.0):
        # rectangle = (x, y, w, h, text)
        self.elements.append({
            "type": "rectangle",
            "data": data,
            "color": color,
            "width": width,
            "font_size": font_size,
        })

    def add_circle(self, data, show_data, color, width=2):
        # circle = (cx, cy, r)
        self.elements.append({
            "type": "circle",
            "data": data,
            "show_data": show_data, 
            "color": color,
            "width": width
        })
        # self.update()
    def add_cross(self, data, color, width=2):
        # circle = (cx, cy, r)
        self.elements.append({
            "type": "cross",
            "data": data,
            "color": color,
            "width": width
        })
    def add_star(self, data, color, width=2):
        # circle = (cx, cy, r)
        self.elements.append({
            "type": "star",
            "data": data,
            "color": color,
            "width": width
        })
    def add_triangle(self, data, color, width=2):
        # circle = (cx, cy, r)
        self.elements.append({
            "type": "triangle",
            "data": data,
            "color": color,
            "width": width
        })  

    def add_text(self, data, color, width=1):
        # text = (x, y, text)，用于把 ROI 等标签固定到指定图像坐标。
        self.elements.append({
            "type": "text",
            "data": data,
            "color": color,
            "width": width
        })

    def add_square(self, data, color, width=2):
        # square = (cx, cy, size)
        self.elements.append({
            "type": "square",
            "data": data,
            "color": color,
            "width": width
        })

    def add_text_block(self, data, color, width=1):
        # text_block = (x, bottom_y, [(text, QColor), ...])，行距随缩放修正，避免缩小时重叠。
        self.elements.append({
            "type": "text_block",
            "data": data,
            "color": color,
            "width": width
        })

    def add_legend_block(self, data, color, width=1):
        # legend_block = (right_x, top_y, [(shape, text, QColor), ...])。
        self.elements.append({
            "type": "legend_block",
            "data": data,
            "color": color,
            "width": width
        })

    def add_dimension(self, data, color, width=1):
        # dimension = (fx1, fy1, fx2, fy2, dx1, dy1, dx2, dy2, text, text_x, text_y)。
        # f* 是被测特征点，d* 是偏移后的尺寸线端点；绘制时自动补延长线和双箭头。
        self.elements.append({
            "type": "dimension",
            "data": data,
            "color": color,
            "width": width
        })

    def boundingRect(self):
        return self.image_rect

    def current_draw_scale(self) -> float:
        """返回绘制补偿用缩放；保存 PNG 时固定为 1，避免线宽和字号受当前视图缩放影响。"""

        if self.export_scale is not None:
            return max(float(self.export_scale), 1e-6)
        views = self.scene().views() if self.scene() is not None else []
        if not views:
            return 1.0
        return max(float(views[0].transform().m11()), 1e-6)

    def paint(self, painter, option, widget):
        painter.setRenderHint(QPainter.Antialiasing, True)
        for element in self.elements:
            scale = self.current_draw_scale()
            pen = QPen(element["color"])
            pen.setWidthF(element["width"] / scale)
            painter.setPen(pen)
            self._draw_element(painter, element, scale)

    def _draw_element(self, painter, element: dict, scale: float) -> None:
        draw_map = {
            "points": self._draw_points,
            "line": self._draw_line,
            "polyline": self._draw_polyline,
            "dimension": self._draw_dimension,
            "rectangle": self._draw_rectangle,
            "circle": self._draw_circle,
            "cross": self._draw_cross,
            "square": self._draw_square,
            "star": self._draw_star,
            "triangle": self._draw_triangle,
            "text": self._draw_text,
            "text_block": self._draw_text_block,
            "legend_block": self._draw_legend_block,
        }
        draw_func = draw_map.get(element["type"])
        if draw_func:
            draw_func(painter, element, scale)

    def _draw_points(self, painter, element: dict, scale: float) -> None:
        for x, y in element["data"]:
            painter.drawPoint(QPointF(x + 0.5, y + 0.5))

    def _draw_line(self, painter, element: dict, scale: float) -> None:
        x1, y1, x2, y2, text = element["data"]
        painter.drawLine(QPointF(x1 + 0.5, y1 + 0.5), QPointF(x2 + 0.5, y2 + 0.5))
        if text is None:
            return
        dx = x2 - x1
        dy = y2 - y1
        length = max(float(np.hypot(dx, dy)), 1e-6)
        offset = 10 / scale
        tx = (x1 + x2) / 2 + (-dy / length) * offset
        ty = (y1 + y2) / 2 + (dx / length) * offset
        self._set_font_size(painter, 10 / scale)
        rect = QFontMetrics(painter.font()).boundingRect(text)
        painter.drawText(QPointF(tx - rect.width() / 2, ty + rect.height() / 2), text)

    def _draw_polyline(self, painter, element: dict, scale: float) -> None:
        points = QPolygonF([QPointF(float(x) + 0.5, float(y) + 0.5) for x, y in element["data"]])
        if len(points) < 2:
            return
        if element.get("closed", False):
            painter.drawPolygon(points)
        else:
            painter.drawPolyline(points)

    def _draw_dimension(self, painter, element: dict, scale: float) -> None:
        data = element["data"]
        if len(data) == 7:
            fx1, fy1, fx2, fy2, text, text_x, text_y = data
            dx1, dy1, dx2, dy2 = fx1, fy1, fx2, fy2
        else:
            fx1, fy1, fx2, fy2, dx1, dy1, dx2, dy2, text, text_x, text_y = data

        painter.drawLine(QPointF(fx1 + 0.5, fy1 + 0.5), QPointF(dx1 + 0.5, dy1 + 0.5))
        painter.drawLine(QPointF(fx2 + 0.5, fy2 + 0.5), QPointF(dx2 + 0.5, dy2 + 0.5))
        painter.drawLine(QPointF(dx1 + 0.5, dy1 + 0.5), QPointF(dx2 + 0.5, dy2 + 0.5))

        dx = dx2 - dx1
        dy = dy2 - dy1
        length = max(float(np.hypot(dx, dy)), 1e-6)
        ux = dx / length
        uy = dy / length
        arrow_len = 8.0 / scale
        arrow_w = 4.0 / scale
        for px, py, sign in ((dx1, dy1, 1.0), (dx2, dy2, -1.0)):
            tip = QPointF(px + 0.5, py + 0.5)
            back_x = px + sign * ux * arrow_len
            back_y = py + sign * uy * arrow_len
            normal_x = -uy
            normal_y = ux
            painter.drawLine(tip, QPointF(back_x + normal_x * arrow_w + 0.5, back_y + normal_y * arrow_w + 0.5))
            painter.drawLine(tip, QPointF(back_x - normal_x * arrow_w + 0.5, back_y - normal_y * arrow_w + 0.5))

        self._set_font_size(painter, 7.0 / scale)
        fm = QFontMetrics(painter.font())
        text = str(text)
        rect = fm.boundingRect(text)
        painter.setPen(QPen(element["color"]))
        painter.drawText(QPointF(text_x - rect.width() / 2, text_y + rect.height() / 2 - fm.descent()), text)

    def _draw_rectangle(self, painter, element: dict, scale: float) -> None:
        x, y, w, h, text = element["data"]
        painter.drawRect(QRectF(x + 0.5, y + 0.5, w, h))
        if text is not None:
            self._set_font_size(painter, float(element.get("font_size", 10.0)) / scale)
            painter.drawText(QPointF(x + 4 / scale, y - 4 / scale), text)

    def _draw_circle(self, painter, element: dict, scale: float) -> None:
        cx, cy, radius, text = element["data"]
        painter.drawEllipse(QPointF(cx + 0.5, cy + 0.5), radius, radius)
        if not element.get("show_data", False):
            return
        end_x = cx + radius
        painter.drawLine(QPointF(cx + 0.5, cy + 0.5), QPointF(end_x + 0.5, cy + 0.5))
        self._set_font_size(painter, 10 / scale)
        rect = QFontMetrics(painter.font()).boundingRect(text)
        painter.drawText(QPointF((cx + end_x) / 2 - rect.width() / 2, cy + 10 / scale + rect.height() / 2), text)

    def _draw_cross(self, painter, element: dict, scale: float) -> None:
        cx, cy, size = element["data"]
        cx += 0.5
        cy += 0.5
        # 十字尺寸使用屏幕像素语义，缩放视图时仅改变其所在图像位置。
        display_size = size / scale
        painter.drawLine(QPointF(cx - display_size, cy), QPointF(cx + display_size, cy))
        painter.drawLine(QPointF(cx, cy - display_size), QPointF(cx, cy + display_size))

    def _draw_square(self, painter, element: dict, scale: float) -> None:
        cx, cy, size = element["data"]
        cx += 0.5
        cy += 0.5
        painter.drawRect(QRectF(cx - size, cy - size, size * 2, size * 2))

    def _draw_star(self, painter, element: dict, scale: float) -> None:
        cx, cy, size = element["data"]
        cx += 0.5
        cy += 0.5
        painter.drawLine(QPointF(cx - size, cy), QPointF(cx + size, cy))
        painter.drawLine(QPointF(cx, cy - size), QPointF(cx, cy + size))
        painter.drawLine(QPointF(cx - size, cy - size), QPointF(cx + size, cy + size))
        painter.drawLine(QPointF(cx - size, cy + size), QPointF(cx + size, cy - size))

    def _draw_triangle(self, painter, element: dict, scale: float) -> None:
        cx, cy, size = element["data"]
        cx += 0.5
        cy += 0.5
        height = size * np.sqrt(3) / 2
        painter.drawPolygon(QPolygonF([
            QPointF(cx, cy - height),
            QPointF(cx - size, cy + height / 2),
            QPointF(cx + size, cy + height / 2),
        ]))

    def _draw_text(self, painter, element: dict, scale: float) -> None:
        x, y, text = element["data"]
        self._set_font_size(painter, 10 / scale)
        painter.drawText(QPointF(x + 4 / scale, y - 4 / scale), str(text))

    def _draw_text_block(self, painter, element: dict, scale: float) -> None:
        data = element["data"]
        x, bottom_y, lines = data[:3]
        anchor = data[3] if len(data) >= 4 else "left"
        self._set_font_size(painter, 9 / scale)
        fm = QFontMetrics(painter.font())
        line_gap = max(13.0 / scale, fm.height() * 1.12)
        start_y = bottom_y - 8 / scale - line_gap * (len(lines) - 1)
        text_widths = [fm.horizontalAdvance(str(line[0] if isinstance(line, (tuple, list)) else line)) for line in lines]
        top_pad = 9.0 / scale
        bottom_pad = 9.0 / scale
        side_pad = 10.0 / scale
        # drawText ? y ? baseline???? ascent/descent ???
        panel_width = (max(text_widths) if text_widths else 0) + side_pad * 2
        panel_height = fm.ascent() + fm.descent() + line_gap * max(0, len(lines) - 1) + top_pad + bottom_pad
        panel_top = start_y - fm.ascent() - top_pad
        panel_left = x - panel_width if anchor == "right" else x
        text_x = panel_left + side_pad
        painter.fillRect(QRectF(panel_left, panel_top, panel_width, panel_height), QColor(0, 0, 0, 175))
        for index, line in enumerate(lines):
            if isinstance(line, (tuple, list)) and len(line) >= 2:
                text, line_color = line[0], line[1]
            else:
                text, line_color = line, element["color"]
            painter.setPen(QPen(line_color if isinstance(line_color, QColor) else QColor(line_color)))
            painter.drawText(QPointF(text_x, start_y + index * line_gap), str(text))

    def _draw_legend_block(self, painter, element: dict, scale: float) -> None:
        right_x, top_y, rows = element["data"]
        self._set_font_size(painter, 7.5 / scale)
        fm = QFontMetrics(painter.font())
        line_gap = max(12.0 / scale, fm.height() * 1.10)
        marker_size = 4.5 / scale
        row_texts = [str(row[1]) for row in rows]
        text_width = max((fm.horizontalAdvance(text) for text in row_texts), default=0)
        side_pad = 9.0 / scale
        marker_x_offset = 12.0 / scale
        text_x_offset = 25.0 / scale
        panel_width = max(text_width + text_x_offset + side_pad, 72.0 / scale)
        panel_height = line_gap * max(1, len(rows)) + 10 / scale
        panel_left, panel_top = self._clamped_panel(float(right_x) - panel_width, float(top_y), panel_width, panel_height, scale)
        painter.fillRect(QRectF(panel_left, panel_top, panel_width, panel_height), QColor(0, 0, 0, 175))
        for index, row in enumerate(rows):
            shape, text, row_color = row
            row_color = row_color if isinstance(row_color, QColor) else QColor(row_color)
            cy = panel_top + 9 / scale + index * line_gap
            cx = panel_left + marker_x_offset
            painter.setPen(QPen(row_color, max(1.0, 2.0 / scale)))
            self._draw_legend_marker(painter, shape, cx, cy, marker_size)
            painter.setPen(QPen(row_color))
            painter.drawText(QPointF(panel_left + text_x_offset, cy + fm.ascent() / 2), str(text))

    def _draw_legend_marker(self, painter, shape: str, cx: float, cy: float, marker_size: float) -> None:
        if shape == "square":
            painter.drawRect(QRectF(cx - marker_size, cy - marker_size, marker_size * 2, marker_size * 2))
        elif shape == "triangle":
            triangle_size = marker_size * 1.25
            height = triangle_size * np.sqrt(3)
            painter.drawPolygon(QPolygonF([
                QPointF(cx, cy - height / 2),
                QPointF(cx - triangle_size, cy + height / 2),
                QPointF(cx + triangle_size, cy + height / 2),
            ]))
        elif shape == "star":
            painter.drawLine(QPointF(cx - marker_size, cy), QPointF(cx + marker_size, cy))
            painter.drawLine(QPointF(cx, cy - marker_size), QPointF(cx, cy + marker_size))
            painter.drawLine(QPointF(cx - marker_size, cy - marker_size), QPointF(cx + marker_size, cy + marker_size))
            painter.drawLine(QPointF(cx - marker_size, cy + marker_size), QPointF(cx + marker_size, cy - marker_size))
        elif shape == "circle":
            painter.drawEllipse(QPointF(cx, cy), marker_size, marker_size)

    def _clamped_panel(self, left: float, top: float, width: float, height: float, scale: float) -> tuple[float, float]:
        margin = 2.0 / scale
        image_left = self.image_rect.left() + margin
        image_right = self.image_rect.right() - margin
        image_top = self.image_rect.top() + margin
        image_bottom = self.image_rect.bottom() - margin
        return (
            max(image_left, min(left, image_right - width)),
            max(image_top, min(top, image_bottom - height)),
        )

    @staticmethod
    def _set_font_size(painter, size: float) -> None:
        font = painter.font()
        font.setPointSizeF(size)
        painter.setFont(font)

