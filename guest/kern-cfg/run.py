import networkx as nx
import matplotlib.pyplot as plt
import os
import time
import re
import subprocess
import scipy
from kfunc_filter import should_filter_function

from tools import *
from ollama import chat
from collections import defaultdict, deque


kernel_cg_path = "./callgraph.graphml"
kernel_cg_acyc_path = "./pruned_callgraph_filtered.graphml"

# Directory paths for syscall profiles and processes

src_directory = "syscall_profiles/"
proc_directory = "syscall_procs/"

linux_src_directory = "linux/"
func_src_file = "the_functions_all.txt"

# statistics derived from pure static CFG
proc_pure_static_file = "pure-static.txt"

# statistics derived from static CFG based-on profiling information
proc_profile_static_file = "profile-static.txt"

def extract_profiled_functions(profile_file):
    profiled_functions = set()
    with open(profile_file, "r") as f:
        lines = f.readlines()
        for line in lines:
            func_name = line.strip()
            if func_name in kall_functions and not should_filter_function(func_name):
                profiled_functions.add(func_name)
    return profiled_functions

# dump statistics (include numbers of nodes/edges, and its markdown tree) of the subgraph
def dump_subgraph_statistics(subgraph, file_name):
    with open(str(file_name), "w") as f:
        f.write(f"Subgraph has {len(subgraph.nodes)} nodes and {len(subgraph.edges)} edges.\n")
        f.write("Markdown tree representation:\n")
        f.write(graph_to_markdown_tree(subgraph, note_profiled=True))
    print(f"Subgraph statistics saved to {file_name}")



def LLM_hybrid_expand_profile(
    subgraph: nx.DiGraph,
    k_cg: nx.DiGraph,
    all_blocks: dict[str, str],
    N: int = 2,
    model: str = "qwen3:32b",
    role: str = "user",
    num_ctx: int = 14336,
    syscall_info: str = "3:close") -> nx.DiGraph:
    """
    Proof-of-concept: iteratively expand `subgraph` by querying LLM for
    semantically valid static edges, up to N hops per dynamic start node.
    """
    
    out_dir = f"{proc_directory}/{syscall_info}"
    the_output = f"{out_dir}/llm_logs.txt"
    
    # Clear existing log file once
    open(the_output, 'w', encoding='utf-8').close()
    queried = set()
    
    # Precompute set of nodes in full call graph for fast lookup
    k_cg_nodes = set(k_cg.nodes)
    
    # Compile regex patterns once
    regex_think = re.compile(r'<think>.*?</think>', flags=re.DOTALL)
    regex_whitespace = re.compile(r'[ \t]+$', flags=re.MULTILINE)
    
    # Open log file once to reduce repeated I/O overhead
    with open(the_output, 'a', encoding='utf-8') as log_file:
        # Topologically sort dynamic subgraph and process bottom-up
        dynamic_order = list(nx.topological_sort(subgraph))[::-1]
    
        with tqdm(total=len(dynamic_order) * N, desc="Expanding subgraph") as pbar:
            for D in dynamic_order:
                frontier = [D]
                for depth in range(N):
                    if not frontier:
                        pbar.update(N - depth)
                        break
                    next_frontier = []
                    markdown_output = graph_to_markdown_tree(subgraph)
                    # Pre-gather sources for current subgraph
                    function_sources = {node: all_blocks.get(node) for node in subgraph.nodes}
    
                    for src in frontier:
                        if src not in k_cg_nodes:
                            continue
    
                        for _, dst in k_cg.out_edges(src):
                            if subgraph.has_edge(src, dst) or (src, dst) in queried:
                                continue
                            queried.add((src, dst))
    
                            # Build prompt text
                            prompt_historical = (
                                f"Here are some Linux kernel function's source code:\n"
                            )
                            # Chuqi: TODO: use backward slicing
                            for node, code in function_sources.items():
                                prompt_historical += f"-- {node}:\n{code}\n"
                            
                            prompt_text = (
                                f"Please first read the above functions' source code.\n"
                                f"You are a Linux security expert analyzing kernel call-graph edges.\n"
                                f"Historical dynamic function call-graph (from prior executions):\n{markdown_output}\n\n"
                                f"Caller: {src}\nSource code:\n{function_sources.get(src)}\n\n"
                                f"Callee candidate: {dst}\nSource code:\n{all_blocks.get(dst)}\n\n"
                            )
                            
                            question_text = (
                                f"\nFrom a security-enforcement standpoint, and given the historical execution contexts, "
                                f"please predict is it semantically and functionally reasonable to expect that "
                                f"execution of {src} will reach {dst}? Provide a concise justification, "
                                f"then a literal answer: '{{Your justification}}\nFINAL ANSWER -> YES/NO'"
                            )
    
                            final_prompt = prompt_historical + prompt_text + question_text
    
                            # Log prompt
                            # Chuqi: I don't log the long source code context blocks
                            log_file.write(f"PROMPT (depth {depth}):\n{prompt_text + question_text}\n\n")
    
                            # Query LLM
                            response = chat(
                                model=model,
                                messages=[{'role': role, 'content': final_prompt}],
                                options={'num_ctx': num_ctx}
                            )
    
                            resp = regex_think.sub('', response.message.content).strip()
                            resp = regex_whitespace.sub('', resp)
    
                            # Log response
                            log_file.write(f"RESPONSE:\n{resp}\n\n")
    
                            if 'FINAL ANSWER -> YES' in resp.upper():
                                subgraph.add_node(dst)
                                subgraph.add_edge(src, dst, inferred=True)
                                next_frontier.append(dst)
                                log_file.write(f"Added edge: {src} -> {dst}\n")
    
                    frontier = next_frontier
                    pbar.update(1)
            
            if pbar.n < pbar.total:
                pbar.update(pbar.total - pbar.n)
    
    # Dump statistics of final subgraph
    dump_subgraph_statistics(subgraph, os.path.join(out_dir, "result.txt"))
    return subgraph


if __name__ == "__main__":
    if not os.path.exists(kernel_cg_path):
        print(f"Callgraph file {kernel_cg_path} does not exist. Please generate it first.")
        exit(1)
    if not os.path.exists(kernel_cg_acyc_path):
        print(f"Callgraph acyclic file {kernel_cg_acyc_path} does not exist. Please generate it first.")
        exit(1)
    
    # Load the call graph and acyclic call graph
    k_cg = nx.read_graphml(kernel_cg_path)
    k_cg.remove_edges_from(nx.selfloop_edges(k_cg))
    k_cg_acyc = nx.read_graphml(kernel_cg_acyc_path)
    print(f"Loaded kernel call graph with {len(k_cg.nodes)} nodes and {len(k_cg.edges)} edges.")
    print(f"Loaded kernel acyclic call graph with {len(k_cg_acyc.nodes)} nodes and {len(k_cg_acyc.edges)} edges.")
    
    # Load all functions
    kall_functions = [node for node in k_cg.nodes if not should_filter_function(node)]
    print(f"Total functions in the kernel call graph: {len(kall_functions)}")
    
    # Load kernel's source code blocks
    content = None
    with open("the_functions_all.txt", "r") as f:
        content = f.read()

    ending = 'XXXTHISENDSHEREXXX'
    pattern = rf"Source Code for\s+([\w_]+)\s*:\s*\n(.*?)(?={re.escape(ending)})"
    all_blocks = dict(re.findall(pattern, content, flags=re.DOTALL))
    print(f"Loaded {len(all_blocks)} function source blocks from the_functions_all.txt.")
    
    # syscall candidates. Let's do some easy ones first.
    syscall_candidates = [
        "demo:close", "3:close", "9:mmap", "10:mproptect", "11:munmap", "12:brk", "13:rt_sigaction",
        "14:rt_sigprocmask", "15:rt_sigreturn",
    ]
    
    for syscall_info in syscall_candidates:
        print(f"Processing syscall: {syscall_info}")
        
        sys_id, syscall_name = syscall_info.split(":", 1)
        
        # Create output directory if it doesn't exist
        out_dir = f"{proc_directory}/{syscall_info}"
        os.makedirs(out_dir, exist_ok=True)
        
        # Load profiled functions from the corresponding profile file
        profile_file = f"{src_directory}/{syscall_info}/nginx-ltp-redis"
        if not os.path.exists(profile_file):
            print(f"Profile file {profile_file} does not exist. Skipping.")
            continue
        
        profiled_functions = extract_profiled_functions(profile_file)
        print(f"Extracted {len(profiled_functions)} profiled functions for {syscall_info}.")
        
        # Dump some statistics of the profiled functions
        entry_name = ""
        if f"__x64_sys_{syscall_name}" in profiled_functions:
            entry_name = f"__x64_sys_{syscall_name}"
        elif f"__x64_{syscall_name}64" in profiled_functions:
            entry_name = f"__x64_{syscall_name}64"
        else:
            print(f"Entry function for syscall {syscall_name} not found in profiled functions.")
        
        pure_static_subgraph = gen_subgraph(k_cg_acyc, sys_entry_function=entry_name, function_set=None)
        profiled_static_subgraph = gen_subgraph(k_cg_acyc, sys_entry_function=entry_name, function_set=profiled_functions, hops=2)    
        dump_subgraph_statistics(pure_static_subgraph, os.path.join(out_dir, proc_pure_static_file))
        dump_subgraph_statistics(profiled_static_subgraph, os.path.join(out_dir, proc_profile_static_file))
        
        # Start LLM!
        dyn_graph = gen_subgraph(k_cg_acyc, sys_entry_function=None, function_set=profiled_functions)
        LLM_hybrid_expand_profile(
            subgraph=dyn_graph,
            k_cg=k_cg_acyc,
            all_blocks=all_blocks,
            N=2,
            model="qwen3:32b",
            role="user",
            num_ctx=32768,
            syscall_info=syscall_info,
        )