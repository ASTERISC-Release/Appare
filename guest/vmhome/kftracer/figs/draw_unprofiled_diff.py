#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np

import matplotlib.font_manager as fm
from matplotlib.font_manager import FontProperties

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
fm.fontManager.addfont(font_path)
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams.update({'font.size': 20})

# 原始数据
data = {
    "read": {"cp": 11.8, "ls": 0, "tar": 12.4, "wget": 3.9, "unqlite": 17},
    "write": {"cp": 0, "ls": 1.7, "tar": 4.5, "wget": 3.4, "unqlite": 2},
    "close": {"cp": 0, "ls": 0, "tar": 0, "wget": 0, "unqlite": 0},
    "openat": {"cp": 4.4, "ls": 0, "tar": 0, "wget": 4.2, "unqlite": 0},
    "mmap": {"cp": 3.5, "ls": 4.1, "tar": 3.6, "wget": 3, "unqlite": 3.1},
    "munmap": {"cp": 9.1, "ls": 8, "tar": 8.1, "wget": 8, "unqlite": 11.8},
    "brk": {"cp": 0, "ls": 3.8, "tar": 0, "wget": 0, "unqlite": 10},
    "socket": {"cp": None, "ls": 6.2, "tar": 6.2, "wget": 4.1, "unqlite": None},
    "execve": {"cp": 5.6, "ls": 5.9, "tar": 5.9, "wget": 5, "unqlite": 6.5},
    "newfstatat": {"cp": 1.3, "ls": 2, "tar": 2, "wget": 8.1, "unqlite": 0},
    # "recvfrom": {"cp": None, "ls": None, "tar": None, "wget": 0.1, "unqlite": None},
}

programs = ["cp", "ls", "tar", "wget", "unqlite"]
syscalls = list(data.keys())

na_marker_value = -5
markers = ['o', 's', '^', 'D', 'x']

fig, ax = plt.subplots(figsize=(9, 4))

for idx, prog in enumerate(programs):
    y_values = []
    for sc in syscalls:
        val = data[sc][prog]
        y_values.append(na_marker_value if val is None else val)
    ax.plot(syscalls, y_values, marker=markers[idx], label=prog, linewidth=2)

# 标记 NA 区域
ax.axhline(y=na_marker_value, color='gray', linestyle='--', linewidth=1)
ax.text(7.3, na_marker_value + 0.5, 'NA (not executed)', va='bottom', ha='left', fontsize=14, color='gray')

# Aggregate all non-None values from the data
all_values = [v for syscall in data.values() for v in syscall.values() if v is not None]

# Compute statistics
mean_val = np.mean(all_values)
median_val = np.median(all_values)
std_val = np.std(all_values)
min_val = np.min(all_values)
max_val = np.max(all_values)

# Print the statistics
print("Statistics across all syscalls and all programs:")
print(f"Mean: {mean_val:.2f}")
print(f"Median: {median_val:.2f}")
print(f"Standard Deviation: {std_val:.2f}")
print(f"Minimum: {min_val}")
print(f"Maximum: {max_val}")

for sc, values in data.items():
    valid_values = [v for v in values.values() if v is not None]
    if valid_values:
        mean_sc = np.mean(valid_values)
        median_sc = np.median(valid_values)
        std_sc = np.std(valid_values)
        min_sc = np.min(valid_values)
        max_sc = np.max(valid_values)
        print(f"\n{sc} statistics:")
        print(f"  Mean: {mean_sc:.2f}")
        print(f"  Median: {median_sc:.2f}")
        print(f"  Standard Deviation: {std_sc:.2f}")
        print(f"  Minimum: {min_sc}")
        print(f"  Maximum: {max_sc}")
    else:
        print(f"\n{sc} statistics: No valid data.")

plt.xticks(rotation=30)
ax.set_ylabel('Uncovered behavior (%)')
ax.yaxis.set_label_coords(-0.06, 0.4)
ax.set_ylim(na_marker_value - 2, 30)
ax.legend(title='', ncol=3)
plt.grid(False)
plt.tight_layout()
# plt.show()
plt.savefig('profile-diffprog-diff.pdf', bbox_inches='tight')
