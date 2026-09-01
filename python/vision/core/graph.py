class Graph:
    def __init__(self):
        self.nodes = []
        self.edges = {}

    def add_node(self, node):
        if node not in self.nodes:
            self.nodes.append(node)
            self.edges[node] = []

    def connect(self, node1, node2):
        if node2 not in self.edges[node1]:
            self.edges[node1].append(node2)

    def topological_sort(self):
        visited = set()
        temp = set()
        result = []

        def dfs(node):
            if node in temp:
                raise Exception("Graph has cycle!")  # 防止死循环
            if node in visited:
                return

            temp.add(node)

            for nxt in self.edges[node]:
                dfs(nxt)

            temp.remove(node)
            visited.add(node)
            result.append(node)

        for node in self.nodes:
            dfs(node)

        return list(reversed(result))
    
    def clear(self):
        self.nodes = []
        self.edges = {}