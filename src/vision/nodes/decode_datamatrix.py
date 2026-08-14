import cv2
from vision.nodes.base_node import BaseNode
from vision.nodes.registry import register_node
import zxingcpp

@register_node
class DecodeDataMatrixNode(BaseNode):
    name = "DecodeDataMatrix"

    def __init__(self):
        super().__init__()

    def process(self, image):
        results = zxingcpp.read_barcodes(image,
                                formats=zxingcpp.BarcodeFormat.DataMatrix)
        if results:
            self.output = results[0].text  # 只有一个码，直接取第一个
        else:
            self.output = None

        return self.output
    
    def get_params(self):
        return { 
        }
