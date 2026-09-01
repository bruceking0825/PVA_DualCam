from vision.nodes.registry import load_all_nodes
from vision.core.graph import Graph
import json
from vision.core.pipeline import Pipeline
from vision.nodes.registry import NODE_REGISTRY
from vision.ui.editor.node_item import NodeItem
from vision.ui.editor.edge_item import EdgeItem

class PipelineService:
    def __init__(self, graph):
        load_all_nodes()  # ⭐关键
        graph = graph
        self.pipeline = Pipeline(graph)

    def configure(self):
        self.pipeline.sort_nodes()

    def run(self, img):
        return self.pipeline.run(img)

    def save_graph(self, scene, filename):
        data = {
            "nodes": [],
            "edges": []
        }

        node_id_map = {}

        # 1️⃣ 保存节点
        for i, item in enumerate(scene.items()):
            if hasattr(item, "node"):
                node = item.node
                node_id = f"n{i}"
                node_id_map[node] = node_id

                data["nodes"].append({
                    "id": node_id,
                    "type": node.name,
                    "params": node.serialize_params(),
                    "pos": [item.pos().x(), item.pos().y()]
                })

        # 2️⃣ 保存连接
        for src, targets in scene.graph.edges.items():
            for tgt in targets:
                data["edges"].append({
                    "source": node_id_map[src],
                    "target": node_id_map[tgt]
                })

        with open(filename, "w") as f:
            json.dump(data, f, indent=4)

    def load_graph(self, scene, filename):


        with open(filename, "r") as f:
            data = json.load(f)

        scene.clear()
        scene.graph.clear()

        id_to_node = {}
        id_to_item = {}

        # 1️⃣ 创建节点
        for node_data in data["nodes"]:
            node_type = node_data["type"]

            node = NODE_REGISTRY[node_type]()

            # 恢复参数
            for k, v in node_data["params"].items():
                node.set_param(k, v)

            item = NodeItem(node, scene)
            item.setPos(*node_data["pos"])

            scene.addItem(item)
            scene.graph.add_node(node)

            id_to_node[node_data["id"]] = node
            id_to_item[node_data["id"]] = item

        # 2️⃣ 创建连接
        for edge_data in data["edges"]:
            src_node = id_to_node[edge_data["source"]]
            tgt_node = id_to_node[edge_data["target"]]

            src_item = id_to_item[edge_data["source"]]
            tgt_item = id_to_item[edge_data["target"]]

            edge = EdgeItem(src_item.output_port)
            scene.addItem(edge)    
            edge.set_target(tgt_item.input_port)
