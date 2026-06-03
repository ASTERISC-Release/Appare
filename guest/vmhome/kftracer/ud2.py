#!/usr/bin/env python3
"""
Generate UD2 injection maps and list of executed kernel functions per syscall,
correctly handling functions that appear at multiple addresses (duplicate symbols),
ignoring pure text holes, and skipping pages before .rodata.

Usage:
    python generate_UD2.py -p <program> -i <program.txt> [--use-llm]

Options:
    -p         Specify the name of the <program>.
    -i         Specify the input <file_name> for executed syscalls.
    --use-llm  If set, read profile data from syscall_procs/<sys_id>:<sys_name>/<file_name>
               instead of syscall_profiles.

Reads executed syscalls from:
    syscall_profiles/executed_syscalls/<program.txt>

Outputs to out_UD2/<program>/:
 - whole_page_code.txt
 - ud2_sections.txt
 - <program>-syscall-kfuncs.txt (with header)
"""

import sys
import os
import re
from collections import defaultdict
import argparse

PAGE_SIZE = 0x1000

# Default functions to include in kfuncs output but ignore in UD2/page logic
DEFAULT_FUNCS = [
    'do_syscall_x64','do_syscall_64','__find_get_block','entry_SYSCALL_64',
    'asm_exc_page_fault','log_fn','log_sub','check_is_logging','check_is_tracked_proc',
    'set_current_proc','deeplog_alloc','deeplog_free','addValueToArray','get_time',
    '__memset','__memcpy','__fput','____fput','fpregs_assert_state_consistent',
    'memset_erms','memset_orig','memcpy_erms','memcpy_orig','__memmove','exit_sc',
    'exit_to_user_mode_prepare','exit_to_user_mode_loop','task_work_run','strstr',
    'strlen','strcmp','strncmp','jbd2__journal_start','__iowrite64_copy',
    '__x64_sys_io_submit','copy_user_enhanced_fast_string','error_entry','error_return',
    'syscall_exit_to_user_mode','nf_hook_slow','__crc32c_le_base','__get_user_4',
    '__put_user_4','__put_user_8','__put_user_nocheck_4','__audit_reusename',
    '__audit_getname','audit_alloc_name','kmalloc_trace','__audit_inode','audit_comparator',
    'audit_copy_inode','audit_tree_lookup','audit_tree_match','security_inode_getsecid',
    'get_vfs_caps_from_disk','selinux_inode_getsecid'
]
CRITICAL_FUNCS = [
    'do_sync_core','do_idle','arch_cpu_idle_enter','arch_cpu_idle_dead','arch_cpu_idle',
    'stop_this_cpu','select_idle_routine',
]
IGNORE_FUNCS = set(DEFAULT_FUNCS + CRITICAL_FUNCS)

def __do_filter_functions(k_func_syms):
    """
    Filter symbols by given substring patterns using regular expressions.
    Currently filters symbols matching pattern "idle".
    """
    patterns = [
        re.compile("idle"),
        re.compile("irq"),
        re.compile("lock"),
        re.compile("rcu"),
        re.compile("kcompactd"),
        re.compile("kthread"),
        ]
    return [sym for sym in k_func_syms if any(p.search(sym) for p in patterns)]

def usage():
    print(f"Usage: {sys.argv[0]} <program>", file=sys.stderr)
    sys.exit(1)

class Page:
    """
    Represents a 4KB page: collects code segments and computes UD2 gaps.
    """
    def __init__(self, base):
        self.base = base
        self.segments = []  # list of (start, end)

    def add_segment(self, start, end):
        s = max(start, self.base)
        e = min(end, self.base + PAGE_SIZE)
        if s < e:
            self.segments.append((s, e))

    def finalize(self):
        if not self.segments:
            return []
        segs = sorted(self.segments)
        merged = []
        cs, ce = segs[0]
        for s, e in segs[1:]:
            if s <= ce:
                ce = max(ce, e)
            else:
                merged.append((cs, ce))
                cs, ce = s, e
        merged.append((cs, ce))

        gaps = []
        start = self.base
        for s, e in merged:
            if start < s:
                gaps.append((start, s))
            start = e
        if start < self.base + PAGE_SIZE:
            gaps.append((start, self.base + PAGE_SIZE))
        return gaps


def generateSyscallFunction(num):
    csv_path = os.path.join('syscall_profiles', 'syscalls.csv')
    with open(csv_path) as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2 and int(parts[0]) == num:
                return '__x64_' + parts[1]
    print(f"MISSING SYSCALL: {num}", file=sys.stderr)
    sys.exit(1)


def addIgnoreFunctions(funcs, num, syms):
    syms_funcs = set(fn for _, fn in syms)
    filter_funcs = __do_filter_functions(syms_funcs)
    entry = generateSyscallFunction(num)
    
    ignores = list(IGNORE_FUNCS) + [entry] + filter_funcs
    for fn in ignores:
        if fn not in funcs:
            funcs.append(fn)
    return funcs

def addNonFentryFunctions(funcs, non_fentry_set, syms):
    syms_funcs = set(fn for _, fn in syms)
    intersection = non_fentry_set & syms_funcs
    for fn in intersection:
        if fn not in funcs:
            funcs.append(fn)
    return funcs

def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate UD2 injection maps and list of executed kernel functions per syscall"
    )
    parser.add_argument('-p', required=True, help="Name of the program (e.g., myprog)")
    parser.add_argument('-i', required=True, help="Input file name for executed syscalls (e.g., myprog.txt)")
    parser.add_argument('--use-llm', action='store_true',
                        help="If set, use profile data from syscall_procs/<sys_id>:<sys_name>/<program.txt> "
                             "instead of syscall_profiles/<sys_id>:<sys_name>/<program>-all.txt")
    return parser.parse_args()
    
def main():
    args = parse_args()
    prog = args.p
    input_fname = args.i

    base_dir = 'syscall_profiles'
    
    # Paths for executed syscalls (always under syscall_profiles/executed_syscalls)
    exec_file = os.path.join('syscall_profiles', 'executed_syscalls', f"{prog}.txt")
    if not os.path.isfile(exec_file):
        print(f"Error: executed syscalls file not found: {exec_file}", file=sys.stderr)
        sys.exit(1)
    
    kall_path = 'kallsyms'
    kobjdump_path = 'kobjdump'
    if not os.path.isfile(exec_file) or not os.path.isfile(kall_path) or not os.path.isfile(kobjdump_path):
        print('Error: missing input files.', file=sys.stderr)
        sys.exit(1)

    # 1) Parse kobjdump: identify fentry-capable and non-fentry symbols
    fentry_set = set()
    non_fentry_set = set()
    current = None
    has_fentry = False
    header_re = re.compile(r'^[0-9a-f]+ <([^>]+)>:$')
    with open(kobjdump_path, 'r') as kd:
        print(f'Parsing {kobjdump_path}...')
        for ln in kd:
            line = ln.rstrip()
            m = header_re.match(line)
            if m:
                # flush previous function
                if current is not None:
                    if has_fentry:
                        fentry_set.add(current)
                    else:
                        non_fentry_set.add(current)
                current = m.group(1)
                has_fentry = False
            else:
                if current and '__fentry__' in line:
                    has_fentry = True
        # flush last function
        if current is not None:
            if has_fentry:
                fentry_set.add(current)
            else:
                non_fentry_set.add(current)

    # 2) Load kallsymbols until __start_rodata, track __static_call_text_end
    text_end = None
    static_end = None
    syms = []  # (addr,name)
    with open(kall_path) as ks:
        for ln in ks:
            parts = ln.split()
            if len(parts) < 3:
                continue
            addr = int(parts[0], 16)
            typ = parts[1]
            name = parts[2]
            if name == '__static_call_text_end':
                static_end = addr
            if name == '__start_rodata':
                text_end = addr
                break
            if typ in ('T', 't'):
                syms.append((addr, name))
    if text_end is None or static_end is None:
        print('Error: required symbols not found', file=sys.stderr)
        sys.exit(1)

    # Build func_range mapping name->list of (start,end)
    syms = [(a, n) for a, n in syms if a < text_end]
    syms.sort()
    func_range = defaultdict(list)
    for i, (addr, name) in enumerate(syms):
        end = syms[i+1][0] if i+1 < len(syms) else text_end
        func_range[name].append((addr, end))

    # Load executed syscalls
    exec_sys = {}
    max_id = 0
    with open(exec_file) as ef:
        for ln in ef:
            tok = ln.strip()
            if not tok:
                continue
            sid_str, sname = tok.split(':', 1)
            sid = int(sid_str)
            exec_sys[sid] = sname
            max_id = max(max_id, sid)

    # Determine page range for code region
    text_start = (syms[0][0] // PAGE_SIZE) * PAGE_SIZE if syms else 0
    static_page = (static_end // PAGE_SIZE) * PAGE_SIZE
    code_region_end = static_page + PAGE_SIZE

    # Prepare output buffers
    whole_pages = [''] * (max_id + 1)
    ud2_sects  = [''] * (max_id + 1)
    kfuncs_out = [''] * (max_id + 1)

    # Process each syscall
    if args.use_llm:
        print(f"Using LLM profiles under syscall_procs/sys<id:name>/{input_fname}")
    else:
        print(f"Using profiles under syscall_profiles/sys<id:name>/{input_fname}")
    
    for sid in sorted(exec_sys):
        sname = exec_sys[sid]
        
        if args.use_llm:
            func_file = os.path.join('syscall_procs', f'{sid}:{sname}', input_fname)
        else:
            func_file = os.path.join('syscall_profiles', f'{sid}:{sname}', input_fname)
        if not os.path.isfile(func_file):
            print(f"Warning: profile file not found for syscall {sid} ({sname}), skipping", file=sys.stderr)
            continue

        # get profiled functions
        prof_all = [l.strip() for l in open(func_file) if l.strip()]
        
        # then update profiled functions (ignore non-fentry ones)
        prof_all = addIgnoreFunctions(prof_all, sid, syms)
        prof_all = addNonFentryFunctions(prof_all, non_fentry_set, syms)
        
        # generate kfuncs output first
        kfuncs_out[sid] = '|'.join(sorted(prof_all))
        
        # Map profiled funcs onto pages
        pages = defaultdict(lambda: Page(0))
        for fn in prof_all:
            for (st, ed) in func_range.get(fn, []):
                p_start = (st // PAGE_SIZE) * PAGE_SIZE
                p_end   = ((ed - 1) // PAGE_SIZE) * PAGE_SIZE
                p = p_start
                while p <= p_end:
                    pg = pages[p]
                    pg.base = p
                    pg.add_segment(st, ed)
                    p += PAGE_SIZE

        # Determine symbol pages (ignore pure holes)
        symbol_pages = set((addr // PAGE_SIZE) * PAGE_SIZE for addr, _ in syms)

        # Finalize each page in code region
        wp = []
        ud = []
        for base_addr in range(text_start, code_region_end, PAGE_SIZE):
            if base_addr not in symbol_pages:
                continue
            pg = pages.get(base_addr, Page(base_addr))
            # if pg.base == 0xffffffff8102b000:
            #     import IPython; IPython.embed()
            #     break
            if not pg.segments:
                wp.append(hex(base_addr))
            else:
                for s, e in pg.finalize():
                    # not a even range? (UD2 occupies 2 bytes)
                    # if (e - s) % 2 != 0:
                    #     print(f"[Warn] (syscall={sid}) Odd UD2 range: {hex(s)}-{hex(e)}", file=sys.stderr)
                    ud.extend([hex(s), hex(e)])

        whole_pages[sid] = ','.join(wp)
        ud2_sects[sid]  = ','.join(ud)

    # write __fentry__ set
    with open("fentry-set-raw.txt", "w") as f:
        for fn in fentry_set:
            f.write(f"{fn}\n")

    # Write outputs
    out_dir = os.path.join('out_UD2', prog)
    os.makedirs(out_dir, exist_ok=True)

    with open(os.path.join(out_dir, 'whole_page_code.txt'), 'w') as f:
        f.write('\n'.join(whole_pages) + '\n')

    with open(os.path.join(out_dir, 'ud2_sections.txt'), 'w') as f:
        f.write('\n'.join(ud2_sects) + '\n')

    # kfuncs with header
    kf_path = os.path.join(out_dir, f'{prog}-syscall-kfuncs.txt')
    with open(kf_path, 'w') as f:
        f.write('syscall_number,functions\n')
        for sid, funcs in enumerate(kfuncs_out):
            f.write(f"{sid},{funcs}\n")

    print(f"Done: outputs in {out_dir}")

if __name__ == '__main__':
    main()