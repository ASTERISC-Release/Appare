from __syscall_table import syscall_table

# iterate over syscall_profiles/{sys_id}:{sys_name}
import os
import json

syscall_profiles = os.path.join(os.path.dirname(__file__), "syscall_profiles")

syscall_table = {}
syscall_id_functions = {}

executed_syscalls = []

for entry in os.listdir(syscall_profiles):
    # Expecting filename to be in the format "{sys_id}:{sys_name}"
    parts = entry.split(":")
    if len(parts) != 2:
        continue
    sys_id, sys_name = parts
    syscall_table[int(sys_id)] = sys_name
    profile_path = os.path.join(syscall_profiles, entry)

with open("functions.txt", "r") as f:
    raw_lines = f.readlines()[1:]

    for line in raw_lines:
        sys_id, funcs = line.split(",")
        syscall_id_functions[int(sys_id)] = [f.strip() for f in funcs.split("|") if f.strip()]

for sys_id, funcs in syscall_id_functions.items():
    if sys_id not in syscall_table:
        print(f"Warning: syscall ID {sys_id} not found in syscall table.")
        continue

    sys_name = syscall_table[sys_id]
    prof_dir = os.path.join(syscall_profiles, f"{sys_id}:{sys_name}")
    if not os.path.exists(prof_dir):
        print(f"Warning: profile directory {prof_dir} does not exist.")
        continue
    
    if not funcs:
        print(f"Warning: No functions found for syscall ID {sys_id} ({sys_name}).")
        continue
    
    executed_syscalls.append(f"{sys_id}:{sys_name}")
    
    targ_file = os.path.join(prof_dir, "ltp-all.txt")
    print(f"Processing {targ_file} for syscall {sys_id} ({sys_name})")
    with open(targ_file, "w") as f:
        for func in funcs:
            f.write(f"{func}\n")
        
with open("syscall_profiles/executed_syscalls/ltp.txt", "w") as f:
    for syscall in (executed_syscalls):
        f.write(f"{syscall}\n")
        
import IPython; IPython.embed()