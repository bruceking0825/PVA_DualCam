class BaseNode:
    name = "BaseNode"

    def __init__(self):
        self.params = {}
        self.output = None  # ⭐关键

    def process(self, data):
        raise NotImplementedError
    
    def set_param(self, key, value):
        setattr(self, key, value)
        
    def serialize_params(self):
        result = {}
        for key, info in self.get_params().items():
            result[key] = info["value"]
        return result