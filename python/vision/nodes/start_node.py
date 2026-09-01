from vision.nodes.base_node import BaseNode
import cv2
from vision.nodes.registry import register_node

@register_node
class StartNode(BaseNode):
    name = "Start"

    def __init__(self):
        super().__init__()
        self.file_path = "1.png"

    def process(self, _):
        result = cv2.imread(self.file_path)
        self.output = result
        return result

    def get_params(self):
        return {
            "file_path": {"type": str, "value": self.file_path},
        }
