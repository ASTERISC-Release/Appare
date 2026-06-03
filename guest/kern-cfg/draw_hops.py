import argparse
import numpy as np
import networkx as nx

from tools import *
from kfunc_filter import should_filter_function


import matplotlib.font_manager as fm
from matplotlib.font_manager import FontProperties

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
fm.fontManager.addfont(font_path)
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams.update({'font.size': 20})

def file_to_sys2func(file_path):
    """
    Parse an unprofiled-all file into a dict mapping
    "sysid:syscall" -> [list of function names]
    """
    sys2funcs = {}
    current = None

    with open(file_path, 'r') as f:
        for line in f:
            stripped = line.strip()
            # Match lines like "0:read" or "1:write"
            m = re.match(r'^(\d+:[\w\.]+)$', stripped)
            if m:
                current = m.group(1)
                sys2funcs[current] = []
            # Match lines containing a [...] list of functions
            elif current and '[' in stripped and ']' in stripped:
                funcs_str = re.search(r'\[([^\]]*)\]', stripped).group(1)
                funcs = [fn.strip() for fn in funcs_str.split(',') if fn.strip()]
                
                for i, fn in enumerate(funcs):
                    m = re.match(r'^(.*)\.(?:isra|part|constprop)\.0$', fn)
                    if m:
                        funcs[i] = m.group(1)
                        
                sys2funcs[current].extend(funcs)

    # Deduplicate while preserving order
    for key, funcs in sys2funcs.items():
        seen = set()
        deduped = []
        for fn in funcs:
            if fn not in seen:
                seen.add(fn)
                deduped.append(fn)
        sys2funcs[key] = deduped

    return sys2funcs

target_syscalls = [
    "0:read",
    "1:write",
    "3:close",
    "257:openat",
    "9:mmap",
    "11:munmap",
    "12:brk",
    "41:socket",
    "59:execve",
    "262:newfstatat",
]


def find_shortest_path_hops(graph, profiled_sources, unprofiled_target):
    """
    Find the shortest path hops from sources to the target.
    Returns a tuple (min_hops, src) where src is the nearest source function.
    If no path exists, returns (-1, None).
    """
    min_hops = float('inf')
    nearest_source = None

    for source in profiled_sources:
        if source not in graph:
            continue
        try:
            hops = nx.shortest_path_length(graph, source, unprofiled_target)
            if hops < min_hops:
                min_hops = hops
                nearest_source = source
        except nx.NetworkXNoPath:
            # print(f"No path from {source} to {unprofiled_target}.")
            continue

    if min_hops == float('inf'):
        return -1, None  # No path found
    return min_hops, nearest_source
    

if __name__ == '__main__':

    parser = argparse.ArgumentParser(description="Process an unprofiled file for hop calculation.")
    parser.add_argument("-i", "--unprofiled_file_path", help="Path to the unprofiled file")
    args = parser.parse_args()

    # get unprofiled functions
    sys2funcs = file_to_sys2func(args.unprofiled_file_path)
    
    # load kernel static callgraph
    k_cg = nx.read_graphml("./callgraph.graphml")
    
    # k_cg = k_cg.to_undirected()
    kall_functions = [node for node in k_cg.nodes if not should_filter_function(node)]
    
    sys2hops = {}
    for syscall_info in target_syscalls:
        unprofiled_functions = sys2funcs.get(syscall_info, [])
        profiled_functions = extract_profiled_functions(kall_functions, f"syscall_profiles/{syscall_info}/nginx-ltp-redis")
    
        src_functions = profiled_functions | set([f for f in kall_functions if should_filter_function(f)])
    
        unprofiled_in_cg = [fn for fn in unprofiled_functions if fn in k_cg.nodes]
    
        hops_mapping = {}
        for fn in unprofiled_in_cg:
            hops, src = find_shortest_path_hops(k_cg, profiled_sources=src_functions, unprofiled_target=fn)
            print(f"[{syscall_info}] Function: {fn} <-- min hops: {hops} -- Src: {src}")
            if hops >= 0:
                hops_mapping[fn] = hops
        sys2hops[syscall_info] = hops_mapping
    
    # import IPython; IPython.embed()  
    
    # for each syscall, calculate it's statistics from hops_mapping to draw a box plot
    import matplotlib.pyplot as plt

    # Prepare combined data for all syscalls
    labels = []
    boxplot_data = []
    for syscall, hops_mapping in sys2hops.items():
        if not hops_mapping:
            print(f"No unprofiled functions with hops for {syscall}. Use 0.")
            labels.append(syscall.split(':')[1])  # Use syscall name only
            boxplot_data.append([0])
            continue
        labels.append(syscall.split(':')[1])
        boxplot_data.append(list(hops_mapping.values()))
        
        min_hops = min(hops_mapping.values())
        max_hops = max(hops_mapping.values())
        avg_hops = sum(hops_mapping.values()) / len(hops_mapping)
        print(f"Syscall: {syscall} | Min Hops: {min_hops}, Max Hops: {max_hops}, Avg Hops: {avg_hops:.2f}")

    if boxplot_data:
        fig, ax = plt.subplots(figsize=(9, 4))
        x = np.arange(len(labels))
        
        # 计算每组的平均值（可选）
        means = [np.mean(d) for d in boxplot_data]
        maxs  = np.array([np.max(d)  for d in boxplot_data])
        
        # 80% and 2-hop lines
        p80s  = [np.percentile(d, 80) for d in boxplot_data]
        print(f"80th percentiles: {p80s}. Average: {np.mean(p80s):.2f}")
        pct2s = [100.0 * sum(np.array(d) <= 2) / len(d) for d in boxplot_data]
        
        if True:
            # 准备误差线：下方误差=0，上方误差=max-mean
            upper_err = maxs - means
            lower_err = np.zeros_like(upper_err)
            yerr = [lower_err, upper_err]

            
            bars = ax.bar(x, means, width=0.3, edgecolor='black', 
                        facecolor=None,
                        label='Mean hops')

            # 只画上方的“误差线”来表示 max
            ax.errorbar(
                x, means,
                yerr=yerr,
                fmt='none',       # 不画点
                ecolor='orange',
                elinewidth=1,
                capsize=5,
                capthick=1
            )
            
            bar = ax.bar(x, p80s, width=0.3, edgecolor=None,
                        facecolor='lightblue', alpha=0.5,
                        label='80th-percentile hops')
            
            handles, lg_labels = ax.get_legend_handles_labels()
            ax.legend(handles, lg_labels, loc='upper right', ncol=2)
            
            # 坐标、标签
            ax.set_xticks(x)
            ax.set_yticks([0, 1, 2, 3, 4])
            ax.set_xticklabels(labels, rotation=30, ha='right')
            ax.set_ylabel('#Shortest-hops')
            # ax.set_title('Average hops per syscall (mean) with max whisker')
        else:
            # 平均值柱状图及最大值误差线
            # bars = ax.bar(x, means, width=0.4, edgecolor='black', label='Mean hops')
            err_low = np.zeros_like(means)
            err_high = np.array(maxs) - np.array(means)
            ax.errorbar(x, means, yerr=[err_low, err_high], fmt='none', ecolor='orange', elinewidth=1, capsize=3)

            # 80th 百分位数线
            ax.plot(x, p80s, marker='o', linestyle='-', label='80th percentile hops')

            ax.set_xticks(x)
            ax.set_xticklabels(labels, rotation=30, ha='right')
            ax.set_ylabel('# of Hops')

            # 次级 Y 轴：2-hop 百分比
            ax2 = ax.twinx()
            ax2.plot(x, pct2s, marker='s', linestyle='--', label='Pct ≤2 hops')
            ax2.set_ylabel('Percentage ≤2 hops (%)')

            # 合并图例
            h1, l1 = ax.get_legend_handles_labels()
            h2, l2 = ax2.get_legend_handles_labels()
            ax.legend(h1 + h2, l1 + l2, loc='upper right')

        plt.tight_layout()
        plt.savefig('syscall_hops.pdf', bbox_inches='tight')
        plt.close()
    else:
        print("No data available for box plot.")
    
    