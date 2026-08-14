class Pipeline:
    def __init__(self, graph):
        self.graph = graph
        self.ordered_nodes = None

    # def run(self):
    #     ordered_nodes = self.graph.topological_sort()
    #     data_map = {}

    #     for node in ordered_nodes:
    #         # ⭐StartNode 不需要输入
    #         if node.name == "Start":
    #             result = node.process(None)
    #         else:
    #             input_img = None

    #             # 找输入（简化：单输入）
    #             for src, targets in self.graph.edges.items():
    #                 if node in targets:
    #                     input_img = data_map.get(src)

    #             result = node.process(input_img)

    #         data_map[node] = result
    #     return result
    def sort_nodes(self):
        self.ordered_nodes = self.graph.topological_sort()

    def run(self, img):
        if img is None or self.ordered_nodes is None:
            return None
        result = None
        data_map = {}

        for node in self.ordered_nodes:
            if node not in data_map:
                data_map[node] = img

            input_img = data_map[node]

            # ⭐执行节点
            result = node.process(input_img)

            # ⭐传递给后续节点
            for next_node in self.graph.edges.get(node, []):
                data_map[next_node] = result

        return result