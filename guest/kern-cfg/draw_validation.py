import argparse
import os
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

from tools import *
from kfunc_filter import should_filter_function

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
fm.fontManager.addfont(font_path)
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams.update({'font.size': 20})

def read_percentage(file_path):
    """
    Read the first line of file_path and extract the percentage of [YES].
    Expected format: "Percentage of [YES]: 86.00%"
    Returns a float.
    """
    with open(file_path, 'r') as f:
        first_line = f.readline().strip()
    m = re.search(r'Percentage of \[YES\]:\s*([\d\.]+)%', first_line)
    if not m:
        raise ValueError(f"Cannot parse percentage from {file_path}: '{first_line}'")
    return float(m.group(1))

if __name__ == "__main__":
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

    base_dir = "syscall_procs"

    names = []
    vals = []
    vals_noctx = []

    for sysid_name in target_syscalls:
        name = sysid_name.split(":", 1)[1]
        names.append(name)

        dir_path = os.path.join(base_dir, sysid_name)
        val_file = os.path.join(dir_path, "validation.txt")
        noctx_file = os.path.join(dir_path, "validation_noctx.txt")

        pct_val = read_percentage(val_file)
        pct_noctx = read_percentage(noctx_file)

        vals.append(pct_val)
        vals_noctx.append(pct_noctx)

    vals = np.array(vals)
    vals_noctx = np.array(vals_noctx)

    # ========= 这里开始修改 =========
    width = 0.3      # 每根柱子的宽度
    interval = 0.05  # 同组内两根柱子的间隙
    extra_gap = 0.5  # 额外留给“组与组之间”的空白

    # 组宽度 = 2 根柱子 + 组内间隙 + 额外空白
    group_width = width * 2 + interval + extra_gap
    x = np.arange(len(names)) * group_width
    # ========= 修改结束 =========

    # 计算并打印平均值
    mean_vals = vals.mean()
    mean_noctx = vals_noctx.mean()
    print(f"Mean accuracy (LLM-RAG): {mean_vals:.2f}%")
    print(f"Mean accuracy (LLM-nonRAG): {mean_noctx:.2f}%")

    fig, ax = plt.subplots(figsize=(9, 4))

    bars1_x = x - width/2 - interval/2
    bars2_x = x + width/2 + interval/2

    color_rag = '#a53860'
    color_nonrag = '#ffa5ab'

    bars1 = ax.bar(
        bars1_x,
        vals,
        width,
        label='LLM-RAG',
        color=color_rag,
        edgecolor='black',
        linewidth=0.5,
        zorder=2
    )
    bars2 = ax.bar(
        bars2_x,
        vals_noctx,
        width,
        label='LLM-nonRAG',
        color=color_nonrag,
        edgecolor='black',
        linewidth=0.5,
        zorder=2
    )

    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=30, ha='right')

    ax.set_ylabel('Prediction accuracy (%)')
    ax.set_yticks([0, 50, 70, 80, 90])
    ax.set_ylim(0, 100)

    ax.legend(ncols=2, loc='lower right')
    ax.grid(axis='y', linestyle='dotted', linewidth=0.75, zorder=1)

    plt.tight_layout()
    plt.savefig("llm_validation.pdf", bbox_inches='tight')
    # plt.show()
