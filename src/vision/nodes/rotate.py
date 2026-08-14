import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class RotateNode(BaseNode):
    name = "Rotate"

    def __init__(self):
        super().__init__()
        self.angle = 1

    def process(self, image):
        (h, w) = image.shape[:2]
        center = (w / 2, h / 2)

        M = cv2.getRotationMatrix2D(center, self.angle, 1.0)

        # 计算新边界
        cos = abs(M[0, 0])
        sin = abs(M[0, 1])

        new_w = int((h * sin) + (w * cos))
        new_h = int((h * cos) + (w * sin))

        # 调整平移
        M[0, 2] += (new_w / 2) - center[0]
        M[1, 2] += (new_h / 2) - center[1]

        # 旋转
        result = cv2.warpAffine(image, M, (new_w, new_h))
        self.output = result
        return result
    
    def get_params(self):
        return {
            "angle": {
                "type": float,
                "value": self.angle
            }
        }
