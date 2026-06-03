#!/usr/bin/env python3
import glob
import csv
import statistics
import re
import matplotlib.pyplot as plt

import matplotlib.font_manager as fm
from matplotlib.font_manager import FontProperties



# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
# 向 matplotlib 注册字体，并设置全局使用
fm.fontManager.addfont(font_path)
# 注意：这里的名称要与字体实际内部名称一致
plt.rcParams['font.family'] = 'Times New Roman'

plt.rcParams.update({'font.size': 20})

# Pipeline depth (only affects Redis latency calc)
PIPELINE = 32
clients = [1, 2, 4, 8, 16, 32, 64]

folders = {
    'redis-native': 'out-redis-native',
    'redis-profile': 'out-redis-profile',
    'redis-hwtrace': 'out-redis-hwtrace',
    'nginx-native': 'out-nginx-native',
    'nginx-profile': 'out-nginx-profile',
    'nginx-hwtrace': 'out-nginx-hwtrace'
}

pattern = re.compile(r'run(\d+)_idx(\d+)\.csv')

def load_redis(folder):
    files = glob.glob(f'./{folder}/run*_idx*.csv')
    grouped = {}
    for path in files:
        m = pattern.search(path)
        if not m:
            continue
        idx = int(m.group(2))
        grouped.setdefault(idx, []).append(path)

    results = {}
    for idx, c in enumerate(clients):
        paths = grouped.get(idx, [])
        if not paths:
            continue
        set_runs = []
        get_runs = []
        for p in paths:
            with open(p, newline='') as f:
                rows = list(csv.reader(f))
            set_runs.append(float(rows[0][1]))
            get_runs.append(float(rows[1][1]))
        mean_set = statistics.mean(set_runs)
        mean_get = statistics.mean(get_runs)
        results[idx] = {
            'set': (mean_set / 1000, c * PIPELINE / mean_set * 1000),
            'get': (mean_get / 1000, c * PIPELINE / mean_get * 1000)
        }
    return results

def load_nginx(folder):
    files = glob.glob(f'./{folder}/run*_idx*.csv')
    grouped = {}
    for path in files:
        m = pattern.search(path)
        if not m:
            continue
        idx = int(m.group(2))
        grouped.setdefault(idx, []).append(path)

    results = {}
    for idx, c in enumerate(clients):
        paths = grouped.get(idx, [])
        if not paths:
            continue
        rps_runs = []
        lat_runs = []
        for p in paths:
            with open(p, newline='') as f:
                row = next(csv.reader(f))
            rps_runs.append(float(row[1]))
            lat_runs.append(float(row[2]))
        results[idx] = (statistics.mean(rps_runs) / 1000,
                        statistics.mean(lat_runs))
    return results

def print_redis_stats(title, data):
    th_set = [v['set'][0] for v in data.values()]
    lat_set = [v['set'][1] for v in data.values()]
    th_get = [v['get'][0] for v in data.values()]
    lat_get = [v['get'][1] for v in data.values()]
    
    print(f"\n{title} - SET")
    print(f"  Throughput: mean={statistics.mean(th_set):.2f}, max={max(th_set):.2f}, min={min(th_set):.2f} (KQPS)")
    print(f"  Latency:    mean={statistics.mean(lat_set):.2f}, max={max(lat_set):.2f}, min={min(lat_set):.2f} (ms)")

    print(f"{title} - GET")
    print(f"  Throughput: mean={statistics.mean(th_get):.2f}, max={max(th_get):.2f}, min={min(th_get):.2f} (KQPS)")
    print(f"  Latency:    mean={statistics.mean(lat_get):.2f}, max={max(lat_get):.2f}, min={min(lat_get):.2f} (ms)")

def print_nginx_stats(title, data):
    th = [v[0] for v in data.values()]
    lat = [v[1] for v in data.values()]
    
    print(f"\n{title}")
    print(f"  Throughput: mean={statistics.mean(th):.2f}, max={max(th):.2f}, min={min(th):.2f} (KQPS)")
    print(f"  Latency:    mean={statistics.mean(lat):.2f}, max={max(lat):.2f}, min={min(lat):.2f} (ms)")

# Load all data
data_redis_native  = load_redis(folders['redis-native'])
data_redis_profile = load_redis(folders['redis-profile'])
data_redis_hwtrace = load_redis(folders['redis-hwtrace'])
data_nginx_native  = load_nginx(folders['nginx-native'])
data_nginx_profile = load_nginx(folders['nginx-profile'])
data_nginx_hwtrace = load_nginx(folders['nginx-hwtrace'])

# Print statistics
print_redis_stats("Redis Native", data_redis_native)
print_redis_stats("Redis SW-trace", data_redis_profile)
print_redis_stats("Redis HW-trace", data_redis_hwtrace)
print_nginx_stats("Nginx Native", data_nginx_native)
print_nginx_stats("Nginx SW-trace", data_nginx_profile)
print_nginx_stats("Nginx HW-trace", data_nginx_hwtrace)

# Create plots
fig, axes = plt.subplots(1, 2, figsize=(9, 4))
ax_r, ax_n = axes

# Redis subplot
for label, data, markers in [
    ("Native:SET", data_redis_native, ('o', 'set')),
    ("Native:GET", data_redis_native, ('s', 'get')),
    ("SWtrace:SET", data_redis_profile, ('^', 'set')),
    ("SWtrace:GET", data_redis_profile, ('d', 'get')),
    ("HWtrace:SET", data_redis_hwtrace, ('<', 'set')),
    ("HWtrace:GET", data_redis_hwtrace, ('>', 'get')),
]:
    items = sorted(data.items())
    th = [v[markers[1]][0] for _, v in items]
    lat = [v[markers[1]][1] for _, v in items]
    ax_r.plot(th, lat, marker=markers[0], label=label, linewidth=3)

ax_r.set_xlabel('Redis throughput (KQPS)')
ax_r.set_ylabel('Average latency (ms)')
ax_r.set_yticks([0, 4, 8])
ax_r.legend(ncols=2, fontsize=15, handletextpad=0.2, columnspacing=0.2)
ax_r.grid(False)

# Nginx subplot
for label, data, marker in [
    ("Native", data_nginx_native, 'v'),
    ("SW-trace", data_nginx_profile, 'x'),
    ("HW-trace", data_nginx_hwtrace, '*'),
]:
    items = sorted(data.items())
    th = [v[0] for _, v in items]
    lat = [v[1] for _, v in items]
    ax_n.plot(th, lat, marker=marker, label=label, linewidth=3)

ax_n.set_xlabel('Nginx throughput (KQPS)')
ax_n.legend(fontsize=16, handletextpad=0.2, columnspacing=0.2)
ax_n.grid(False)

plt.tight_layout(w_pad=0.2)
plt.savefig('traceall-slowdown.pdf', bbox_inches='tight')