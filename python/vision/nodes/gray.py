import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class GrayNode(BaseNode):
    name = "Gray"

    def __init__(self):
        super().__init__()

    def process(self, image):
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if len(image.shape) == 3 else image
        self.output = gray     
        return gray
    
    def get_params(self):
        return {}
