import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class GaussianBlurNode(BaseNode):
    name = "GaussianBlur"

    def __init__(self):
        super().__init__()
        # self.name = "GaussianBlur"
        # ⭐参数
        self.ksize = 5
        self.sigma = 1.0

    def process(self, image):
        result = cv2.GaussianBlur(image, (self.ksize, self.ksize), self.sigma)
        self.output = result
        return result
    
    # ⭐参数描述（核心）
    def get_params(self):
        return {
            "ksize": {
                "type": int,
                "value": self.ksize
            },
            "sigma": {
                "type": float,
                "value": self.sigma
            }
        }
