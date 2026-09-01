import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class BinarizeNode(BaseNode):
    name = "Binarize"

    def __init__(self):
        super().__init__()
        self.threshold = 100

    def process(self, image):
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if len(image.shape) == 3 else image
        _, binary = cv2.threshold(gray, self.threshold, 255, cv2.THRESH_BINARY)   
        self.output = binary     
        return binary
    
    def get_params(self):
        return {
            "threshold": {
                "type": int,
                "value": self.threshold
            }
        }
