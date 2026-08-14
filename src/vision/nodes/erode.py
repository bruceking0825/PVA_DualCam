import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node

@register_node
class ErodeNode(BaseNode):
    name = "Erode"

    def __init__(self):
        super().__init__()
        self.kx = 3
        self.ky = 3
        self.iterations = 1

    def process(self, image):
        """
        白色区域变大
        ksize: 核大小
        iterations: 迭代次数
        """

        #  非法值修正
        kx = max(1, int(self.kx))
        ky = max(1, int(self.ky))

        #  限制过大（防止性能问题）
        kx = min(kx, 99)
        ky = min(ky, 99)

        # iterations 修正
        iterations = max(1, int(self.iterations))

        # 创建结构元素
        kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (kx, ky))

        result = cv2.erode(image, kernel, iterations=iterations)
        self.output = result
        return result
        
    def get_params(self):
        return {
            "kx": {
                "type": int,
                "value": self.kx
            },
            "ky": {
                "type": int,
                "value": self.ky
            },
            "iterations": {
                "type": int,
                "value": self.iterations
            },
            
        }
