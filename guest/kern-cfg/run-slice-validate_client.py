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
import random


# python run-slice-validate_client.py --port 11432 --syscalls 0:read 1:write 3:close 257:openat 9:mmap 11:munmap 12:brk 41:socket 59:execve 262:newfstatat

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


# dump statistics (include numbers of nodes/edges, and its markdown tree) of the subgraph
def dump_subgraph_statistics(subgraph, file_name):
    with open(str(file_name), "w") as f:
        f.write(f"Subgraph has {len(subgraph.nodes)} nodes and {len(subgraph.edges)} edges.\n")
        f.write("Markdown tree representation:\n")
        f.write(graph_to_markdown_tree(subgraph, note_profiled=True))
    print(f"Subgraph statistics saved to {file_name}")


def LLM_validate(
    subgraph: nx.DiGraph,
    k_cg: nx.DiGraph,
    all_blocks: dict[str, str],
    client: Client,
    N: int = 2,
    model: str = "qwen3:32b",
    role: str = "user",
    num_ctx: int = 14336,
    syscall_info: str = "3:close",
    nothink: bool = True,
    vali_thres=50) -> nx.DiGraph:
    """
    Proof-of-concept: iteratively expand `subgraph` by querying LLM for
    semantically valid static edges, up to N hops per dynamic start node.
    """
    
    mode = "nothink" if nothink else "think"
    nothink_prompt = "\\nothink" if nothink else ""
    
    
    out_dir = f"{proc_directory}/{syscall_info}"
    # profile_info_output = f"{out_dir}/profiled_info.txt"
    
    the_output = f"{out_dir}/llm_logs_validate_{mode}.txt"
    
    result_file = f"{out_dir}/validation.txt"
    result_file_noctx = f"{out_dir}/validation_noctx.txt"
    
    
    # with open(profile_info_output, 'w', encoding='utf-8') as f:
    #     f.write(f"Profiled functions: {len(subgraph.nodes)} nodes and {len(subgraph.edges)} edges.\n")
    #     # write down all functions
    #     f.write("Profiled (filtered) functions:\n")
    #     for node in subgraph.nodes:
    #         f.write(f"{node}\n")
    
    # Clear existing log file once
    open(the_output, 'w', encoding='utf-8').close()
    
    # Precompute set of nodes in full call graph for fast lookup
    k_cg_nodes = set(k_cg.nodes)
    
    # Compile regex patterns once
    regex_think = re.compile(r'<think>.*?</think>', flags=re.DOTALL)
    regex_whitespace = re.compile(r'[ \t]+$', flags=re.MULTILINE)
    
    open(result_file, 'w', encoding='utf-8').close()
    open(result_file_noctx, 'w', encoding='utf-8').close()

    function_sources = {node: all_blocks.get(node) for node in subgraph.nodes}

    with open(result_file, 'a', encoding='utf-8') as f_res:
        with open(result_file_noctx, 'a', encoding='utf-8') as f_res_noctx:
            with open(the_output, 'a', encoding='utf-8') as log_file:
                # Gather all nodes in the subgraph that have outgoing edges
                potential_src_nodes = {node for node in subgraph.nodes if subgraph.out_degree(node) > 0}
                if len(potential_src_nodes) < 50:
                    thres = len(potential_src_nodes) // 2
                else:
                    thres = vali_thres
                
                # Randomly select pairs of nodes and destinations
                selected_pairs = random.sample(
                    [
                    (node, random.choice(list(subgraph.neighbors(node))))
                    for node in potential_src_nodes
                    ],
                    min(thres, len(potential_src_nodes))
                )
                
                def __query_llm(prompt, src, dst, f_res, f_res_noctx, with_ctx: bool):
                    print(f"[{syscall_info}; ctx: {with_ctx}] Querying LLM for validation. {src} -> {dst}...")
                    s_time = time.time()
                    llm_chat = client.chat if client else chat
                    response = llm_chat(
                        model=model,
                        messages=[{'role': role, 'content': prompt}],
                        options={'num_ctx': num_ctx}
                    )
                    #log the query time
                    e_time = time.time()
                    query_yes = "NO"

                    resp = regex_think.sub('', response.message.content).strip()
                    resp = regex_whitespace.sub('', resp)

                    # Log response
                    log_file.write(f"RESPONSE:\n{resp}\n\n")
                    if 'FINAL ANSWER -> YES' in resp.upper():
                        query_yes = "YES"
                        if with_ctx:
                            f_res.write(f"[YES] {src} -> {dst}\n")
                        else:
                            f_res_noctx.write(f"[YES] {src} -> {dst}\n")
                    else:
                        if with_ctx:                    
                            f_res.write(f"[NO] {src} -> {dst}\n")
                        else:
                            f_res_noctx.write(f"[NO] {src} -> {dst}\n")
                    print(f"RES: {query_yes} ({src}->{dst})")
                
                for src, dst in selected_pairs:
                    # use backward slicing to find historical traces that reach `src`
                    back_slice_grh = backward_slice(subgraph, src)
                    markdown_output = graph_to_markdown_tree(back_slice_grh)
                        
                
                    # Build prompt text
                    historical_srccode = (
                        f"Here are some Linux kernel function's source code:\n"
                    )
                    # use backward slicing
                    for node in back_slice_grh.nodes:
                        code = function_sources.get(node, "")
                        if code:
                            historical_srccode += f"-- {node}:\n{code}\n"

                    # prompt with ctx
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
                        f"execution of {src} will reach {dst}? "
                        # f"Don't be too restrictive. Your justification can be a bit loose and optimistic. "
                        f"Provide a concise justification, "
                        f"then a literal answer: '{{Your justification}}\nFINAL ANSWER -> YES/NO'{nothink_prompt}"
                    )

                    final_prompt = historical_srccode + prompt_text + question_text
                    # Log prompt (excluding long source context)
                    log_file.write(f"PROMPT (ctx: True):\n{prompt_text + question_text}\n\n")
                    __query_llm(final_prompt, src, dst, f_res, f_res_noctx, with_ctx=True)
                    
                    # prompt without ctx
                    final_prompt_text = (
                        f"You are a Linux security expert analyzing kernel call-graph edges.\n"
                        f"Caller: {src}\n\n"
                        f"Callee candidate: {dst}\n\n"
                        f"From a security-enforcement standpoint, "
                        f"please predict is it semantically and functionally reasonable to expect that "
                        f"execution of {src} will reach {dst}? "
                        # f"Don't be too restrictive. Your justification can be a bit loose and optimistic. "
                        f"Provide a concise justification, "
                        f"then a literal answer: '{{Your justification}}\nFINAL ANSWER -> YES/NO'\\nothink"
                    )
                    # Log prompt
                    log_file.write(f"PROMPT (ctx: False):\n{final_prompt_text}\n\n")
                    __query_llm(final_prompt_text, src, dst, f_res, f_res_noctx, with_ctx=False)
       
    # f_res.close()
    # f_res_noctx.close()
    # Count statistics for f_res and f_res_noctx
    def count_yes_percentage(file_path):
        total_lines = 0
        yes_count = 0
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                total_lines += 1
                if "[YES]" in line:
                    yes_count += 1
        return (yes_count / total_lines * 100) if total_lines > 0 else 0

    yes_percentage_ctx = count_yes_percentage(result_file)
    yes_percentage_noctx = count_yes_percentage(result_file_noctx)
    
    with open(result_file, 'r+', encoding='utf-8') as f:
        content = f.read()
        f.seek(0, 0)
        f.write(f"Percentage of [YES]: {yes_percentage_ctx:.2f}%\n" + content)

    with open(result_file_noctx, 'r+', encoding='utf-8') as f:
        content = f.read()
        f.seek(0, 0)
        f.write(f"Percentage of [YES]: {yes_percentage_noctx:.2f}%\n" + content)

    # print(f"Percentage of [YES] in {result_file}: {yes_percentage_ctx:.2f}%")
    # print(f"Percentage of [YES] in {result_file_noctx}: {yes_percentage_noctx:.2f}%")
    print(f"Validation results saved to {result_file} and {result_file_noctx}.")
    
    return subgraph


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run the slice client with a custom port for the client connection.")
    parser.add_argument("--port", type=int, default=0, help="Port number for client connection")
    parser.add_argument("--syscalls", nargs="+", default=[], help="List of syscalls in format sys_id:syscall (e.g., 3:close 9:mmap).")
    parser.add_argument("--nothink", action="store_true", help="Enable nothink mode; if provided, nothink is True")
    
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
        
    if args.nothink:
        print("Running in Qwen NO-THINK mode.")
    else:
        print("Running in Qwen THINK mode.")
    
    if not args.port:
        print("No port specified. Using default Ollama client connection.")
    
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
    if args.port:
        client = Client(host=f"http://localhost:{args.port}")
        llm_chat = client.chat
    else:
        client = None
        llm_chat = chat
    
    nothink_prompt = "\\nothink" if args.nothink else ""
    respond = llm_chat(
        model="qwen3:32b",
        messages=[{"role": "user", "content": "Warm up. Just say {{MF}}!" + nothink_prompt}],
        options={"num_ctx": 32768}
    )
    print(f"Ollama client connected. Response: {respond.message.content}")
    
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
        
        profiled_functions = extract_profiled_functions(kall_functions, profile_file)
        print(f"Extracted {len(profiled_functions)} profiled functions for {syscall_info}.")
        
        # Dump some statistics of the profiled functions
        entry_name = ""
        if f"__x64_sys_{syscall_name}" in profiled_functions:
            entry_name = f"__x64_sys_{syscall_name}"
        elif f"__x64_{syscall_name}64" in profiled_functions:
            entry_name = f"__x64_{syscall_name}64"
        else:
            print(f"Entry function for syscall {syscall_name} not found in profiled functions.")
        
        # pure_static_subgraph = gen_subgraph(k_cg_acyc, sys_entry_function=entry_name, function_set=None)
        # profiled_static_subgraph = gen_subgraph(k_cg_acyc, sys_entry_function=entry_name, function_set=profiled_functions, hops=2)    
        # dump_subgraph_statistics(pure_static_subgraph, os.path.join(out_dir, proc_pure_static_file))
        # dump_subgraph_statistics(profiled_static_subgraph, os.path.join(out_dir, proc_profile_static_file))
        
        # Start LLM!
        dyn_graph = gen_subgraph(k_cg_acyc, sys_entry_function=None, function_set=profiled_functions)
        
        thinkmode = "nothink" if args.nothink else "think"
        
        # import IPython; IPython.embed()
        
        # profile the time taken
        start_time = time.time()
        LLM_validate(
            subgraph=dyn_graph,
            k_cg=k_cg_acyc,
            all_blocks=all_blocks,
            client=client,
            N=2,
            model="qwen3:32b",
            role="user",
            num_ctx=32768,
            syscall_info=syscall_info,
            nothink=args.nothink,
        )
        end_time = time.time()
        elapsed_time = end_time - start_time
        print(f"Validation for syscall {syscall_info} completed in {elapsed_time:.2f} seconds.")