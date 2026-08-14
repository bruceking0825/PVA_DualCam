import os
import importlib

NODE_REGISTRY = {}

def register_node(cls):
    NODE_REGISTRY[cls.name] = cls
    return cls

def load_all_nodes():
    base_dir = os.path.dirname(__file__)

    for file in os.listdir(base_dir):
        if file.endswith(".py") and file not in ["__init__.py", "registry.py", "base_node.py"]:
            module_name = f"vision.nodes.{file[:-3]}"
            importlib.import_module(module_name)