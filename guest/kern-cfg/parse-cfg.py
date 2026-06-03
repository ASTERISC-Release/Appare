#!/usr/bin/env python3
"""
parse-cfg-no-dag.py

Builds a directed call graph from BBMapping_later.json and ICInfo.json,
marks indirect-call nodes/edges, and writes the full graph to GraphML
without condensing strong components.
"""

import json
import networkx as nx

# Load direct calls mapping
with open('BBMapping_later.json') as f:
    bb_entries = json.load(f)

# Load indirect call info
with open('ICInfo.json') as f:
    ic_entries = json.load(f)

# Build directed graph G
G = nx.DiGraph()

# Add direct call edges (indirect=False)
for e in bb_entries:
    caller = e['name'].rstrip('&')
    G.add_node(caller)
    for succ in e.get('successors', []):
        tgt = succ.rstrip('&')
        G.add_node(tgt)
        G.add_edge(caller, tgt, indirect=False)

# Add indirect calls: mark nodes and edges
for e in ic_entries:
    caller = e['BBName'].rstrip('&')
    G.add_node(caller)
    # mark node with indirect call site
    G.nodes[caller]['has_indirect'] = True
    for callee in e.get('callees', []):
        tgt = callee.rstrip('&')
        G.add_node(tgt)
        if G.has_edge(caller, tgt):
            # update existing edge to mark indirect
            G[caller][tgt]['indirect'] = True
        else:
            # add new indirect-only edge
            G.add_edge(caller, tgt, indirect=True)

# Write the full graph to GraphML
nx.write_graphml(G, 'callgraph.graphml')

print(f"Graph written: {G.number_of_nodes()} nodes, {G.number_of_edges()} edges")
