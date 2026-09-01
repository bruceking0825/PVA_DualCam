from vision.nodes.base_node import BaseNode
import cv2
from vision.nodes.registry import register_node

@register_node
class EndNode(BaseNode):
    name = "End"

    def __init__(self):
        super().__init__()

        self.output_type = "label"  # "label" | "graphics" | "file"
        self.save_path = "output.png"

        self.viewer = None  # ⭐由UI注入

    def process(self, image):
        self.output = image

        if image is None:
            return None

        # ⭐显示/保存逻辑
        if self.output_type == "file":
            cv2.imwrite(self.save_path, image)

        elif self.viewer:
            self.viewer.show_image(image, self.output_type)

        return image

    def get_params(self):
        return {
            "output_type": {"type": str, "value": self.output_type},
            "save_path": {"type": str, "value": self.save_path},
        }
