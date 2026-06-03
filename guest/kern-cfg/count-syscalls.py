#!/usr/bin/env python3
"""
count_syscalls.py

统计给定 trace 文件中各 syscall 的出现次数，并按次数从多到少排序输出。
"""

import re
from collections import Counter

def count_syscalls(filename):
    counter = Counter()
    pattern = re.compile(r'^\s*([a-zA-Z0-9_]+)\(')
    
    with open(filename, 'r') as f:
        for line in f:
            m = pattern.match(line)
            if m:
                counter[m.group(1)] += 1

    # 按出现次数从多到少排序
    for syscall, cnt in counter.most_common():
        print(f"{syscall:20s} {cnt}")

if __name__ == '__main__':
    import sys
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <trace_file>")
        sys.exit(1)
    count_syscalls(sys.argv[1])
