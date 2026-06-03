import os
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
fm.fontManager.addfont(font_path)
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams.update({'font.size': 20})

def extract_node_percent(first_line: str) -> float:
    """
    从类似 "Profiled functions: 111 nodes and 1112 edges."
    或者  "Subgraph has 2973 nodes and 3042 edges."
    中提取第一个出现的 “<整数> nodes”，然后计算占 41837 个函数的百分比。
    """
    m = re.search(r'(\d+)\s+nodes', first_line)
    if not m:
        raise ValueError(f"Not found node numbers: '{first_line}'")

    node_count = int(m.group(1))
    # total numbers of linux kernel functions is 41837
    return node_count * 100.0 / 41837

if __name__ == "__main__":
    # ----------------------------------------------------------------------
    # 1. sysid:sysname 列表，每个元素形如 "13:rt_sigaction"
    # ----------------------------------------------------------------------
    target_syscalls = [
        "1:write", "0:read", "61:wait4", "59:execve", "58:vfork", "39:getpid",
        "21:access", "10:mprotect", 
        # "17:pread64"
        # "13:rt_sigaction", "14:rt_sigprocmask","15:rt_sigreturn", 
        "43:accept", "54:setsockopt", "56:clone", "234:tgkill",
        "46:sendmsg", "52:getpeername", "230:clock_nanosleep", "80:chdir", "99:sysinfo",
        "100:times",
        "41:socket", "42:connect", "63:newuname", "79:getcwd",
        "89:readlink", "92:chown", "128:rt_sigtimedwait", "204:sched_getaffinity", "217:getdents64",
        "3:close", "9:mmap", "74:fsync", "82:rename", "202:futex", "232:epoll_wait", "233:epoll_ctl",
        "293:pipe2", "20:writev", "40:sendfile", "45:recvfrom", "288:accept4", "231:exit_group",
        "257:openat", "262:newfstatat", "302:prlimit64", "318:getrandom", "32:dup", "37:alarm",
        "33:dup2", "8:lseek", "72:fcntl", "110:getppid", "158:arch_prctl", "218:set_tid_address"
    ]

    # ----------------------------------------------------------------------
    # 2. 按 sysid 从小到大排序
    # ----------------------------------------------------------------------
    def parse_sysid(item: str) -> int:
        return int(item.split(":", 1)[0])

    sorted_syscalls = sorted(target_syscalls, key=parse_sysid)

    # ----------------------------------------------------------------------
    # 3. 遍历每个 sysid:sysname，提取三个文件中的“节点数百分比”
    # ----------------------------------------------------------------------
    base_dir = "syscall_procs"
    names = []
    nodes_dyn = []
    nodes_mid = []
    nodes_sta = []

    for entry in sorted_syscalls:
        sysid, sysname = entry.split(":", 1)
        dir_path = os.path.join(base_dir, entry)

        # 3.1 profiled_info.txt → Appare-DYN
        file1 = os.path.join(dir_path, "profiled_info.txt")
        if not os.path.exists(file1):
            print(f"Warning: {file1} does not exist, skipping this syscall.")
            continue

        names.append(sysname)
        with open(file1, "r") as f1:
            first_line1 = f1.readline().strip()
        nodes_dyn.append(extract_node_percent(first_line1))

        # 3.2 result_nothink.txt（如果没有就用 result_think.txt）→ Appare
        file2 = os.path.join(dir_path, "result_nothink.txt")
        if not os.path.exists(file2):
            file2 = os.path.join(dir_path, "result_think.txt")
            if not os.path.exists(file2):
                raise FileNotFoundError(
                    f"Neither 'result_nothink.txt' nor 'result_think.txt' found in {dir_path}"
                )
        with open(file2, "r") as f2:
            first_line2 = f2.readline().strip()
        nodes_mid.append(extract_node_percent(first_line2))

        # 3.3 profile-static.txt → Appare-STA
        file3 = os.path.join(dir_path, "profile-static.txt")
        with open(file3, "r") as f3:
            first_line3 = f3.readline().strip()
        nodes_sta.append(extract_node_percent(first_line3))

    nodes_dyn = np.array(nodes_dyn)
    nodes_mid = np.array(nodes_mid)
    nodes_sta = np.array(nodes_sta)

    # ----------------------------------------------------------------------
    # 额外：计算并输出三个模式下的均值
    # ----------------------------------------------------------------------
    mean_dyn = nodes_dyn.mean()
    mean_mid = nodes_mid.mean()
    mean_sta = nodes_sta.mean()
    max_dyn = nodes_dyn.max()
    max_mid = nodes_mid.max()
    max_sta = nodes_sta.max()
    print(f"Mean Appare-DYN (% of all kernel funcs.): {mean_dyn:.2f}%")
    print(f"Mean Appare      (% of all kernel funcs.): {mean_mid:.2f}%")
    print(f"Mean Appare-STA  (% of all kernel funcs.): {mean_sta:.2f}%")
    
    print(f"Max Appare-DYN (% of all kernel funcs.): {max_dyn:.2f}%")
    print(f"Max Appare      (% of all kernel funcs.): {max_mid:.2f}%")
    print(f"Max Appare-STA  (% of all kernel funcs.): {max_sta:.2f}%")

    # ----------------------------------------------------------------------
    # 4. 开始画图
    # ----------------------------------------------------------------------
    x = np.arange(len(names))  # 0, 1, 2, ..., N-1，保证均匀分布

    fig, ax = plt.subplots(figsize=(20, 6))

    # 三层柱子的宽度
    width_sta = 0.4   # Appare-STA（最外层）
    width_mid = 0.25  # Appare   （中层）
    width_dyn = 0.1   # Appare-DYN（最内层）

    # 三种深浅不同的蓝色，方便重叠时区分
    color_sta = "#B0C4DE"  # LightSteelBlue（最浅，放最外层）
    color_mid = "#4682B4"  # SteelBlue      （中等颜色）
    color_dyn = "#0B3D91"  # MidnightBlue   （最深，放最内层）

    # 4.1 绘制 Appare-STA（最外层）——zorder 最小
    bars_sta = ax.bar(
        x,
        nodes_sta,
        width_sta,
        color=color_sta,
        edgecolor="black",
        linewidth=0.8,
        label="Appare-sta",
        zorder=1
    )

    # 4.2 绘制 Appare（中层）
    bars_mid = ax.bar(
        x,
        nodes_mid,
        width_mid,
        color=color_mid,
        edgecolor="black",
        linewidth=0.8,
        label="Appare",
        zorder=2
    )

    # 4.3 绘制 Appare-DYN（最内层）——zorder 最大
    bars_dyn = ax.bar(
        x,
        nodes_dyn,
        width_dyn,
        color=color_dyn,
        edgecolor="black",
        linewidth=0.8,
        label="Appare-dyn",
        zorder=3
    )

    # ----------------------------------------------------------------------
    # 5. 配置坐标轴与图例
    # ----------------------------------------------------------------------
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=90, ha="center", va="top")

    ax.set_ylabel("Reference funcs.\n(% of all kernel funcs.)")

    # 添加横向网格线
    ax.grid(axis="y", linestyle="dotted", linewidth=0.6, zorder=0)

    # 将图例放到右下角
    ax.legend(ncol=3, frameon=True)

    # **关键：让 x 轴从 -0.5 开始，到 (N-1)+0.5 结束，以保证左右留白**
    ax.set_xlim(-0.5, len(names) - 0.5)

    # 自动紧凑排版，防止标签被裁剪
    plt.tight_layout()
    plt.savefig("kernel_controlflow_analysis-crop.pdf", bbox_inches="tight")
    plt.show()
