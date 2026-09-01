import base64
import json
from typing import Any

import cv2
import numpy as np
from PySide6.QtCore import QBuffer, QByteArray, QIODevice, Qt
from PySide6.QtGui import QColor, QImage, QPixmap
from PySide6.QtWidgets import QGraphicsPixmapItem, QGraphicsTextItem

from .graphics_contour_item import ContourItem


def serialize_contour_data(data: Any) -> Any:
    """保存 Figure JSON 时把 QColor 递归转成字符串。"""

    if isinstance(data, QColor):
        return data.name()
    if isinstance(data, tuple):
        return [serialize_contour_data(item) for item in data]
    if isinstance(data, list):
        return [serialize_contour_data(item) for item in data]
    return data


def save_figure(view, file_path: str) -> None:
    """保存 CustomGraphicsView 的图片、文字、轮廓和光标状态。"""

    data: dict[str, Any] = {
        "version": 1,
        "mode": view.mode,
    }

    if view.original_image:
        byte_array = QByteArray()
        buffer = QBuffer(byte_array)
        buffer.open(QIODevice.WriteOnly)
        view.original_image.save(buffer, "PNG")
        data["image"] = {
            "format": "PNG",
            "data": base64.b64encode(byte_array.data()).decode("utf-8"),
        }

    if view.text_item and view.text_item.isVisible():
        pos = view.text_item.pos()
        data["text"] = {
            "content": view.text_item.toPlainText(),
            "x": pos.x(),
            "y": pos.y(),
        }

    if view.contour_item:
        data["contours"] = []
        for element in view.contour_item.elements:
            item = {
                "type": element["type"],
                "data": serialize_contour_data(element["data"]),
                "width": element["width"],
                "color": element["color"].name(),
            }
            if "show_data" in element:
                item["show_data"] = element["show_data"]
            if "font_size" in element:
                item["font_size"] = element["font_size"]
            data["contours"].append(item)

    if view.cursor_item:
        pos = view.cursor_item.pos()
        data["cursor"] = {
            "x": pos.x(),
            "y": pos.y(),
        }

    with open(file_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=4, ensure_ascii=False)


def load_figure(view, file_path: str) -> None:
    """从 Figure JSON 恢复 CustomGraphicsView 状态。"""

    with open(file_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)

    view.scene_obj.clear()
    view.image_item = QGraphicsPixmapItem()
    view.scene_obj.addItem(view.image_item)
    view.text_item = QGraphicsTextItem()
    view.text_item.setDefaultTextColor(Qt.yellow)
    view.scene_obj.addItem(view.text_item)
    view.contour_item = None
    view.cursor_item = None
    view.mode = data.get("mode", "image")

    if "text" in data:
        text_data = data["text"]
        view.text_item.setPlainText(text_data["content"])
        view.text_item.setPos(text_data["x"], text_data["y"])
        view.text_item.setVisible(True)

    if "image" in data and "data" in data["image"]:
        img_bytes = base64.b64decode(data["image"]["data"])
        image = QImage()
        image.loadFromData(img_bytes)
        pixmap = QPixmap.fromImage(image)
        view.image_item.setPixmap(pixmap)
        view.image_item.setVisible(True)
        view.original_image = image
        view.scene_obj.setSceneRect(pixmap.rect())

        cv_img = cv2.imdecode(np.frombuffer(img_bytes, np.uint8), cv2.IMREAD_COLOR)
        view.image_loaded_signal.emit(cv_img)

        view.contour_item = ContourItem(pixmap.size())
        view.scene_obj.addItem(view.contour_item)

    if "contours" in data and view.contour_item:
        for element in data["contours"]:
            add_contour_element(view.contour_item, element)

    if "cursor" in data:
        cursor = data["cursor"]
        view.draw_cursor(int(cursor["x"]), int(cursor["y"]))


def add_contour_element(contour_item, element: dict[str, Any]) -> None:
    """把 Figure JSON 中的一条轮廓记录恢复成 ContourItem 元素。"""

    default_color = QColor("red") if element.get("type") == "points" else QColor("green")
    color = element.get("color", default_color)
    if not isinstance(color, QColor):
        color = QColor(color)
    item_type = element.get("type")
    default_width = 2 if item_type in {"line", "polyline", "rectangle", "circle", "cross", "star", "triangle", "square"} else 1
    width = element.get("width", default_width)
    data = element.get("data")

    if item_type == "points":
        contour_item.add_points(data, color, width)
    elif item_type == "line":
        contour_item.add_line(data, color, width)
    elif item_type == "polyline":
        contour_item.add_polyline(data, color, width, element.get("closed", False))
    elif item_type == "rectangle":
        contour_item.add_rectangle(data, color, width, element.get("font_size", 10.0))
    elif item_type == "circle":
        contour_item.add_circle(data, element.get("show_data", False), color, width)
    elif item_type == "cross":
        contour_item.add_cross(data, color, width)
    elif item_type == "star":
        contour_item.add_star(data, color, width)
    elif item_type == "triangle":
        contour_item.add_triangle(data, color, width)
    elif item_type == "square":
        contour_item.add_square(data, color, width)
    elif item_type == "text":
        contour_item.add_text(data, color, width)
    elif item_type == "text_block":
        contour_item.add_text_block(data, color, width)
    elif item_type == "legend_block":
        contour_item.add_legend_block(data, color, width)
    elif item_type == "dimension":
        contour_item.add_dimension(data, color, width)
