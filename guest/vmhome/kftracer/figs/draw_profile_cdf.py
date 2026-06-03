#!/usr/bin/env python3
"""
Plot CDF-style coverage curves for multiple syscalls' nginx profiles.

Scans `syscall_profiles/` for directories matching `*:<sys_name>` for each syscall in SYS_NAMES,
collects files named `nginx-<num>.txt`, computes coverage over the merged function set, and
plots coverage vs. number of profiles for each syscall, including the (0,0) origin point.
"""
import os
import re
import glob
import sys
import matplotlib.pyplot as plt

import matplotlib.font_manager as fm
from matplotlib.font_manager import FontProperties

sys.path.append("../")
from kfunc_filter import should_filter_function

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
# 向 matplotlib 注册字体，并设置全局使用
fm.fontManager.addfont(font_path)
# 注意：这里的名称要与字体实际内部名称一致
plt.rcParams['font.family'] = 'Times New Roman'

plt.rcParams.update({'font.size': 20})


# —— 配置 —— #
PROFILES_BASE = "../syscall_profiles"
PREFIX = "nginx"
SYS_NAMES = [
    "write", "close", "writev", "recvfrom",
    "setsockopt", "epoll_wait", "epoll_ctl", "newfstatat"
]
# —— —— —— #

pattern = re.compile(rf"^{re.escape(PREFIX)}-(\d+)\.txt$")
results = {}

for sys_name in SYS_NAMES:
    # 查找对应目录
    dirs = glob.glob(os.path.join(PROFILES_BASE, f"*:{sys_name}"))
    if not dirs:
        print(f"Warning: no directory found for syscall '{sys_name}'", file=sys.stderr)
        continue
    dirpath = dirs[0]

    # 收集并排序 profile 文件
    files = []
    for fname in os.listdir(dirpath):
        m = pattern.match(fname)
        if m:
            idx = int(m.group(1))
            files.append((idx, os.path.join(dirpath, fname)))
    if not files:
        print(f"Warning: no '{PREFIX}-<num>.txt' profiles in {dirpath}" , file=sys.stderr)
        continue
    files.sort(key=lambda x: x[0])

    # 读取每个 profile 的函数集
    sets = []
    for _, path in files:
        with open(path) as f:
            funcs = {line.strip() for line in f if line.strip()}
            
        # Filter?
        funcs = {f for f in funcs if not should_filter_function(f)}
        sets.append(funcs)

    # 计算全集并集
    all_funcs = set().union(*sets)
    total = len(all_funcs)
    if total == 0:
        print(f"Warning: no functions found for syscall '{sys_name}'", file=sys.stderr)
        continue
    else:
        print(f"Syscall '{sys_name}' has {total} functions")
        
    # 累积覆盖率
    coverages = []
    acc = set()
    for s in sets:
        acc |= s
        coverages.append(len(acc) / total * 100)
    results[sys_name] = coverages

# 绘图
plt.figure(figsize=(9, 4))
for sys_name, coverage in results.items():
    n = len(coverage)
    # 从 (0,0) 开始
    x = list(range(0, n + 1))
    coverage_points = [0.0] + coverage
    plt.plot(x, coverage_points, linewidth=2.5, label=sys_name)

plt.xlabel("Times of profiles (#)")
plt.ylabel("Total coverage (%)")
# xticks 从 0 到最大序号
max_n = max(len(c) for c in results.values())
plt.xticks([0,3,6,9,12,15])
plt.yticks([20,40,60,80,100])
plt.ylim(0, 103)
plt.xlim(0, 15)

plt.xlim(left=0)  # 强制 x 轴从 0 开始
plt.ylim(bottom=0)  # 强制 y 轴从 0 开始

plt.grid(False)
# plt.axhline(y=80, color='gray', linestyle='--', linewidth=1)
plt.axhline(y=100, color='gray', linestyle='--', linewidth=1)

plt.legend(ncol=2,handletextpad=0.3,columnspacing=0.5)
plt.tight_layout()
# 保存为 PDF
plt.savefig("profile-sameprog-diff.pdf", bbox_inches='tight')
