import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class ROInode(BaseNode):
    name = "ROI"

    def __init__(self):
        super().__init__()
        self.x = 0
        self.y = 0
        self.width = 100
        self.height = 100

    def process(self, image):
        h, w = image.shape[:2]
        x1 = max(0, self.x)
        y1 = max(0, self.y)
        x2 = min(w, self.x + self.width)
        y2 = min(h, self.y + self.height)

        roi = image[y1:y2, x1:x2]
        self.output = roi
        return roi
    
    def get_params(self):
        return {
            "x": {
                "type": int,
                "value": self.x
            },
            "y": {
                "type": int,
                "value": self.y
            },
            "width": {
                "type": int,
                "value": self.width
            },
            "height": {
                "type": int,
                "value": self.height
            }
        }
