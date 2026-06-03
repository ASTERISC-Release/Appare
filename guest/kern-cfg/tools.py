###########################################
# Graph
###########################################
from kfunc_filter import should_filter_function
import re
import matplotlib.pyplot as plt
import networkx as nx
from tqdm import tqdm

def extract_profiled_functions(kall_functions, profile_file):
    profiled_functions = set()
    with open(profile_file, "r") as f:
        lines = f.readlines()
        for line in lines:
            func_name = line.strip()
            if func_name in kall_functions and not should_filter_function(func_name):
                profiled_functions.add(func_name)
    return profiled_functions

def draw_subgraph(graph, graph_name):
    plt.figure(figsize=(12, 8))
    pos = nx.spring_layout(graph)  # Use the graph layout
    in_degree_zero_nodes = [node for node in graph if graph.in_degree(node) == 0]
    node_colors = ['red' if node in in_degree_zero_nodes else 'lightblue' for node in graph.nodes]
    nx.draw(graph, pos, with_labels=True, node_size=500, font_size=10, node_color=node_colors, edge_color='black')
    print(f"Subgraph number of nodes: {len(graph.nodes())}, number of edges: {len(graph.edges())}")
    plt.title(f"Subgraph: {graph_name}")
    plt.show()
    plt.savefig(f"{graph_name}.png", format='png', dpi=300)

def gen_subgraph(static_graph, sys_entry_function=None, function_set=None, hops=2):
    """
    if sys_entry_function is set, and function_set is None: 
        Generate a subgraph starting from the sys_entry_function, including all reachable nodes.
    
    if sys_entry_function is None, and function_set is set:
        Generate a subgraph containing only the nodes in function_set.
    
    if both sys_entry_function and function_set are set:
        This is a "tricky" indicator.
        Generate a subgraph containing the nodes in function_set, and also bfs traverse the nodes in N `hops`
        from each node in function_set.
    """
    if not sys_entry_function and not function_set:
        # return an empty graph
        print("Both sys_entry_function and function_set are None, returning an empty graph.")
        return nx.DiGraph()
    
    if not function_set:
        # If function_set is empty, use all reachable nodes from the sys_entry_function
        if sys_entry_function in static_graph.nodes:
            subgraph_nodes = nx.descendants(static_graph, sys_entry_function)
            subgraph_nodes.add(sys_entry_function)  # Include the entry function itself
        else:   
            print(f"sys_entry_function {sys_entry_function} is not in the static graph, returning an empty graph.")
            subgraph_nodes = set()
    else:
        # Filter nodes to include only those in the function_set
        function_set = [f for f in function_set if not should_filter_function(f)]
        for f in function_set:
            if f not in static_graph.nodes:
                print(f"Function {f} is not in the static graph, skipping it.")
        function_set = [f for f in function_set if f in static_graph.nodes]
        subgraph_nodes = set(function_set)

    if sys_entry_function is not None and function_set is not None:
        # Include nodes in function_set and traverse `hops` from each node
        if hops >= 0:
            for node in function_set:
                visited = set()
                queue = [(node, 0)]  # (current_node, current_hop)
                while queue:
                    current_node, current_hop = queue.pop(0)
                    if current_hop < hops and current_node not in visited:
                        visited.add(current_node)
                        neighbors = list(static_graph.successors(current_node))
                        subgraph_nodes.update(neighbors)
                        queue.extend([(neighbor, current_hop + 1) for neighbor in neighbors])
        
        else:   # hops = -1: consider all descendant nodes of function_set
            for node in function_set:
                _desc = nx.descendants(static_graph, node)
                subgraph_nodes.update(_desc)
                
        # Mark all nodes in the original function_set as "profiled"
        for node in function_set:
            if node in static_graph.nodes:
                static_graph.nodes[node]["profiled"] = True

    # Create the subgraph with the filtered nodes
    subgraph = static_graph.subgraph(subgraph_nodes).copy()
    return subgraph

def backward_slice(graph, node):
    """
    Traverse the graph in reverse order starting from `node`, and include all nodes up to the root nodes.
    A root node is defined as a node with an in-degree of 0.
    
    Returns a subgraph containing all collected nodes.
    """
    backward_nodes = set()
    stack = [node]
    
    while stack:
        current = stack.pop()
        if current in backward_nodes:
            continue
        backward_nodes.add(current)
        if graph.in_degree(current) == 0:
            continue
        for predecessor in graph.predecessors(current):
            if predecessor not in backward_nodes:
                # print(f"src: {current} adding  predecessor: {predecessor}")
                stack.append(predecessor)
    
    return graph.subgraph(backward_nodes).copy()

def outedges_callgraph(graph, function_name, do_filter=True):
    """ 
    function_name is the source node (u), it will return all out-edges (u, v1), (u, v1),... of this node.
    """
    if function_name not in graph:
        print(f"Function {function_name} does not exist in the graph.")
        return []

    out_edges = []
    for _, target, edge_data in graph.out_edges(function_name, data=True):
        if do_filter and should_filter_function(target):
            continue
        out_edges.append((function_name, target, edge_data))
    
    return out_edges

def graph_to_markdown_tree(graph, note_profiled=False):
    """
    Convert a directed graph to a markdown tree representation
    """
    def dfs(node, indent=0, visited=set()):
        lines = []
        prefix = "  " * indent + "- " + node
        if note_profiled and graph.nodes[node].get("profiled", False):
            prefix += " <profiled>"
        lines.append(prefix)
        visited.add(node)
        for _, neighbor in graph.out_edges(node):
            if neighbor not in visited:
                lines.extend(dfs(neighbor, indent + 1, visited))
        return lines
    roots = [n for n in graph.nodes if graph.in_degree(n) == 0]
    all_lines = []
    for root in roots:
        all_lines.extend(dfs(root, indent=0, visited=set()))
    return "\n".join(all_lines)