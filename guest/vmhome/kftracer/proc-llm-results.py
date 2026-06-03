import os
import re

base_dir = "syscall_procs"

dynamic_dir = "syscall_profiles"

res_nothink = "result_nothink.txt"
res_think = "result_think.txt"

out_nothink = "result_nothink"
out_think = "result_think"

def proc_llm_file(file_path):
    func_set = set()
    with open(file_path, 'r') as f:
        for line in f:
            # Process each line in the LLM result file
            stripped = line.strip()
            if stripped.startswith('- '):
                func_name = stripped[2:]
                func_name = re.sub(r'<profiled>', '', func_name).strip()
                func_set.add(func_name)
    print(f"Processed {len(func_set)} functions from {file_path}")
    return func_set

def proc_path(dir, full_path, dyn_dir):
    if "result_nothink.txt" in str(full_path):
        out_path =  os.path.join(dir, out_nothink)
    elif "result_think.txt" in str(full_path):
        out_path =  os.path.join(dir, out_think)
    else:
        raise ValueError(f"Unexpected file in {full_path}")

    func_set = proc_llm_file(full_path)
    
    dyn_file = os.path.join(dyn_dir, "nginx-ltp-redis")
    if os.path.isfile(dyn_file):
        with open(dyn_file, 'r') as dyn_f:
            for line in dyn_f:
                func_name = line.strip()
                if func_name:
                    func_set.add(func_name)
    
    with open(out_path, 'w') as out_file:
        for func in sorted(func_set):
            out_file.write(f"{func}\n")
    

for root, dirs, files in os.walk(base_dir):
    for dir_name in dirs:
        subdir_path = os.path.join(root, dir_name)
        print(f"Subdirectory: {subdir_path}")
        
        # dynamic profile dir
        dyn_dir = os.path.join(dynamic_dir, dir_name)
        print(f"Dynamic profile directory: {dyn_dir}")
        
        think_path = os.path.join(subdir_path, res_think)
        if os.path.isfile(think_path):
            proc_path(subdir_path, think_path, dyn_dir)

        nothink_path = os.path.join(subdir_path, res_nothink)
        if os.path.isfile(nothink_path):
            proc_path(subdir_path, nothink_path, dyn_dir)