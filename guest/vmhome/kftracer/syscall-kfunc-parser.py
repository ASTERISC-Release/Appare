#!/usr/bin/env python3
import sys
import os
import glob

from __syscall_table import syscall_table

def usage():
    print(f"Usage: {sys.argv[0]} <program> <file_id>")
    print("Input: It will automatically process report-cpu*.txt files under the current directory.")
    print("Output: Output files (per-syscall functions) will be generated at syscall_profiles/sys_id:sys_name/<program>-<file-id>")
    sys.exit(1)

# align ftrace's __sys_enter_xxx and real syscall name
def sys_name_align(sys_name: str):
    if sys_name == "sendfile64":
        return "sendfile"
    if sys_name == "newstat":
        return "stat"
    if sys_name == "newfstat":
        return "fstat"
    if sys_name == "newlstat":
        return "lstat"
    if sys_name == "newuname":
        return "uname"
    if sys_name == "umount":
        return "umount2"
    return sys_name

# ── GLOBALS ─────────────────────────────────────────
# map {syscall_name → set(function names)}
syscalls = {}
# set(functions) seen outside any syscall
common_funcs = set()

def parse_file(program, filename):
    # use global data
    global syscalls, common_funcs
    
    in_syscall = False
    current_sys_name = None
    current_sys_id = None          # nr of the syscall we’re inside
    local_funcs = set()           # stash of syscall functions

    with open(filename) as f:
        # line and line_number
        for line_number, line in enumerate(f):
            
            # 0) FILTER: skip any line that is not a syscall trace
            line = line.rstrip()
            if not line:
                continue

            # 1) FILTER: skip any line not from our target program
            #    e.g. "<idle>-0" → prog="<idle>"
            #    have to keep redis-server-100 -> prog="redis-server"
            tok = line.split()[0]
            prog = tok.rsplit('-', 1)[0]
            if prog != program:
                continue

            # 2) EXTRACT the event name
            #    find the ':' after the timestamp bracket ']' to skip
            try:
                idx = line.index(':', line.index(']'))
            except ValueError:
                continue
            rest = line[idx+1:].strip()
            if not rest:
                print(f"skip line: '{line}'")
                continue
            ev_tok = rest.split(None, 1)[0].rstrip(':')  # e.g. "sys_enter_open"
            # if ev_tok == "function":
            #     import IPython; IPython.embed()          
            
            # 3) SYSCALL BOUNDARIES
            if ev_tok.startswith('sys_enter_'):
                name = ev_tok[len('sys_enter_'):]
                name = sys_name_align(name)
                try:
                    nr = syscall_table(name)
                except ValueError:
                    print(f"line_{line_number} Unknown syscall: {name}")
                    sys.exit(1)
                # noise: should not double-enter syscalls
                if in_syscall:
                    print(f"line_{line_number} syscall already entered: {current_sys_name} ({current_sys_id})")
                    print("Discard this noise syscall.")
                # begin new stash
                in_syscall = True
                current_sys_id = nr
                current_sys_name = name
                local_funcs = set()
                
            if ev_tok.startswith('sys_exit_'):
                # we could verify it's the same name, but just reset
                sys_name = ev_tok[len('sys_exit_'):]
                sys_name = sys_name_align(sys_name)
                
                if current_sys_name is None or current_sys_id is None:
                    print(f"[{filename}] line_{line_number} Found sys_exit_{sys_name} without previous sys_enter_")
                elif sys_name != current_sys_name:
                    print(f"line_{line_number} syscall mismatch: {current_sys_name} != {sys_name}")
                    print("Discard the noise syscall pair...")
                else:
                    # commit
                    __key = f"{current_sys_id}:{current_sys_name}"
                    syscalls.setdefault(__key, set()).update(local_funcs)
                in_syscall = False
                current_sys_id = None
                current_sys_name = None
                local_funcs = set()
                continue

            # 4) FUNCTION ENTRIES
            if ev_tok == 'funcgraph_entry' or ev_tok == 'function':
                
                if ev_tok == 'funcgraph_entry':
                    # everything after the '|' is the function call
                    if '|' not in line:
                        continue
                    func = line.split('|', 1)[1].strip()
                    # strip arguments/parentheses
                    name = func.split('(')[0].strip()
                else:
                    if ':' not in line:
                        continue
                    func = line.split(':')[-1].strip()
                    name = func 
                    
                if in_syscall and current_sys_id is not None and current_sys_name is not None:
                    local_funcs.add(name)
                else:
                    common_funcs.add(name)

def write_outputs(program: str, file_id: str):
    base_dir = "syscall_profiles"
    # 1) write common functions
    common_dir = os.path.join(base_dir, "common")
    os.makedirs(common_dir, exist_ok=True)
    common_path = os.path.join(common_dir, f"{program}-{file_id}.txt")
    with open(common_path, 'w') as outf:
        for fn in sorted(common_funcs):
            outf.write(fn + "\n")
    
    # 2) write per-syscall functions
    for key in sorted(syscalls.keys(), key=lambda k: int(k.split(':', 1)[0])):
        funcs = syscalls[key]
        dirpath = os.path.join(base_dir, key)
        os.makedirs(dirpath, exist_ok=True)
        outpath = os.path.join(dirpath, f"{program}-{file_id}.txt")
        with open(outpath, "w") as outf:
            for fn in sorted(funcs):
                outf.write(fn + "\n")
    
    # 3) write executed syscalls list (merge previous)
    exec_dir = os.path.join(base_dir, "executed_syscalls")
    os.makedirs(exec_dir, exist_ok=True)
    exec_path = os.path.join(exec_dir, f"{program}.txt")
    # load previous entries if exist
    prev = set()
    if os.path.exists(exec_path):
        with open(exec_path) as f:
            for line in f:
                prev.add(line.strip())
    # merge with current syscalls
    all_keys = prev.union(syscalls.keys())
    with open(exec_path, "w") as outf:
        for key in sorted(all_keys, key=lambda k: int(k.split(':', 1)[0])):
            outf.write(key + "\n")


def main():
    if len(sys.argv) != 3:
        usage()
    program = sys.argv[1]
    file_id = sys.argv[2]
    # filename = f"{program}-{file_id}.txt"

    output_filename = f"{program}-{file_id}.txt"

    # if not os.path.isfile(filename):
    #     print(f"Error: input file '{filename}' not found.", file=sys.stderr)
    #     sys.exit(1)
    # common, by_syscall = parse_file(program, filename)
    
    # iteratively process all report-cpu* (per-cpu ftrace) files
    for path in sorted(glob.glob('report-cpu*.txt')):
        if os.path.isfile(path):
            print(f"[{program}] processing {path}...")
            parse_file(program, path)

    # import IPython; IPython.embed()

    # OUTPUT
    write_outputs(program, file_id)

if __name__ == '__main__':
    main()
