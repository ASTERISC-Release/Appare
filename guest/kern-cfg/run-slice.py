import networkx as nx
import matplotlib.pyplot as plt
import os
import time
import re
import subprocess
import scipy
from kfunc_filter import should_filter_function

from tools import *
from ollama import Client
from tqdm import tqdm
from ollama import chat
from collections import defaultdict, deque
import argparse


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
    client: Client,
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
    profile_info_output = f"{out_dir}/profiled_info.txt"
    the_output = f"{out_dir}/llm_logs.txt"
    
    with open(profile_info_output, 'w', encoding='utf-8') as f:
        f.write(f"Profiled functions: {len(subgraph.nodes)} nodes and {len(subgraph.edges)} edges.\n")
        # write down all functions
        f.write("Profiled (filtered) functions:\n")
        for node in subgraph.nodes:
            f.write(f"{node}\n")
    
    # Clear existing log file once
    open(the_output, 'w', encoding='utf-8').close()
    
    queried = set()
    queried_nodes = set(subgraph.nodes) # these mfc nodes are already exist in the subgraph (profiled functions)
    
    # Precompute set of nodes in full call graph for fast lookup
    k_cg_nodes = set(k_cg.nodes)
    
    # Compile regex patterns once
    regex_think = re.compile(r'<think>.*?</think>', flags=re.DOTALL)
    regex_whitespace = re.compile(r'[ \t]+$', flags=re.MULTILINE)
    
    # Open log file once to reduce repeated I/O overhead
    with open(the_output, 'a', encoding='utf-8') as log_file:
        # Topologically sort dynamic subgraph and process bottom-up
        dynamic_order = list(nx.topological_sort(subgraph))[::-1]
    
        start_counter = 0
        counter = 0
        for D in dynamic_order:
            
            print(
                f"[Expanding {syscall_info}] Process ({start_counter}/{len(dynamic_order)}). "
                f"Root {D}, 1-hop neighbors ({len(list(k_cg.neighbors(D)))}) [{list(k_cg.neighbors(D))}]"
                )
            start_counter += 1
            
            frontier = [D]
            
            for depth in range(N):
                # during iteractions, frontier will be updated -> next_frontier
                if not frontier:
                    break
                
                next_frontier = []
                
                # Pre-gather sources for current subgraph
                function_sources = {node: all_blocks.get(node) for node in subgraph.nodes}
        
                for src in frontier:
                    if src not in k_cg_nodes:
                        continue
                    
                    # use backward slicing to find historical traces that reach `src`
                    back_slice_grh = backward_slice(subgraph, src)
                    markdown_output = graph_to_markdown_tree(back_slice_grh)
                    
                    if src == "free_unref_page_list":
                        import IPython; IPython.embed()
                        exit(0)
                        
                    for _, dst in k_cg.out_edges(src):
                        if subgraph.has_edge(src, dst) or (src, dst) in queried:
                            continue
                        if dst in queried_nodes:
                            continue
                        
                        queried.add((src, dst))
                        queried_nodes.add(dst)   # be mfc rigorous now first. also optimize performance. 
                        
                        # Build prompt text
                        historical_srccode = (
                            f"Here are some Linux kernel function's source code:\n"
                        )
                        # Chuqi: use backward slicing
                        for node in back_slice_grh.nodes:
                            code = function_sources.get(node, "")
                            if code:
                                historical_srccode += f"-- {node}:\n{code}\n"
        
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
                            f"then a literal answer: '{{Your justification}}\nFINAL ANSWER -> YES/NO'\\nothink"
                        )
        
                        final_prompt = historical_srccode + prompt_text + question_text
        
                        # Log prompt (excluding long source context)
                        log_file.write(f"PROMPT (depth {depth}):\n{prompt_text + question_text}\n\n")
        
                        # Query LLM
                        # log the query and time to the console
                        print(f"[Expanding {syscall_info}] Querying LLM for edge {src} -> {dst} at depth {depth}...")
                        s_time = time.time()
                        response = chat(
                            model=model,
                            messages=[{'role': role, 'content': final_prompt}],
                            options={'num_ctx': num_ctx}
                        )
                        #log the query time
                        e_time = time.time()
                        print(f"=> Query time: {e_time - s_time:.2f} seconds\n")
        
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
            
            counter += 1
            if counter % 10 == 0:
                print(f"[Expanding {syscall_info}] {counter}/{len(dynamic_order)} nodes processed.")
    
    # Dump statistics of final subgraph
    dump_subgraph_statistics(subgraph, os.path.join(out_dir, "result.txt"))
    # Dump the expanded subgraph to a file
    nx.write_graphml(subgraph, os.path.join(out_dir, "expanded_subgraph.graphml"))
    return subgraph


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run!")
    parser.add_argument("--syscalls", nargs="+", default=[], help="List of syscalls in format sys_id:syscall (e.g., 3:close 9:mmap).")
    args = parser.parse_args()

    if not args.syscalls:
        print("No syscalls provided. Please specify syscalls in the format sys_id:syscall (e.g., 3:close 9:mmap).")
        exit(1)

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
    
    syscall_candidates = args.syscalls if args.syscalls else []
    
    if not syscall_candidates:
        print("No syscalls provided. Please specify syscalls in the format sys_id:syscall (e.g., 3:close 9:mmap).")
        exit(1)
        
    # Start ollama client
    # client = Client(host=f"http://localhost:{args.port}")
    # respond = client.chat(
    #     model="qwen3:32b",
    #     messages=[{"role": "user", "content": "Warm up. Just say {{MF}}!"}],
    #     options={"num_ctx": 32768}
    # )
    # print(f"Ollama client connected. Response: {respond.message.content}")
    
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
        
        # profile the time taken
        start_time = time.time()
        LLM_hybrid_expand_profile(
            subgraph=dyn_graph,
            k_cg=k_cg_acyc,
            all_blocks=all_blocks,
            client=None,
            N=2,
            model="qwen3:32b",
            role="user",
            num_ctx=32768,
            syscall_info=syscall_info,
        )
        end_time = time.time()
        elapsed_time = end_time - start_time
        # write time taken to a output file 
        with open(os.path.join(out_dir, "time_taken.txt"), "w") as f:
            f.write(f"Time taken for LLM expansion of {syscall_info}: {elapsed_time:.2f} seconds\n")