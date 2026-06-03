#!/usr/bin/env python3
"""
Compare syscall profile functions against a baseline.

Usage:
  python syscall-merge-diff-prog.py -b base_name -d prof1 prof2 ... -o result_name

This script reads `syscall_table`, iterates over directories in
`syscall_profiles/<sys_id>:<sys_name>`, and for each:
  - Loads baseline functions from `<base_name>` file.
  - For each provided diff profile `prof_name`, loads functions,
    computes how many are not in the baseline, and their percentage.
  - Writes a summary to `syscall_profiles/<sys_id>:<sys_name>/<result_name>`.

Output format:
<sys_id>:<sys_name>
    <prof-name>: total funcs: x; not profiled: z/x (X%) [fname1, fname2, ...]
"""
import argparse
import os
import sys
import re

from kfunc_filter import should_filter_function

def parse_args():
    parser = argparse.ArgumentParser(
        description="Compute differences between syscall profiles and a baseline"
    )
    parser.add_argument(
        '-b', '--base',
        required=True,
        help='Baseline profile filename'
    )
    parser.add_argument(
        '-d', '--diffs',
        nargs='+', required=True,
        help='List of profile filenames to compare'
    )
    parser.add_argument(
        '-o', '--output',
        required=True,
        help='Result filename to produce'
    )
    parser.add_argument(
        '--profiles-dir',
        default='syscall_profiles',
        help='Base directory containing syscall_profiles (default: syscall_profiles/)'
    )
    return parser.parse_args()

# def should_ign_func(name):
#     # syscall_ent / interrupts
#     hard_filter_set = ["syscall_exit_work", "raw_notifier_call_chain", "tick_sched_handle", "update_vsyscall", "update_wall_time",
#                        "tick_do_update_jiffies64", "timekeeping_advance", "trigger_load_balance",
#                        "timekeeping_update", "update_fast_timekeeper", "ntp_get_next_leap", "ntp_tick_length",
#                        "account_system_time", "account_system_index_time", "__acct_update_integrals",
#                        "__accumulate_pelt_segments", "__update_load_avg_cfs_rq", "__update_load_avg_se",
#                        "calc_global_load", "cpuacct_charge", "cpuacct_account_field",
#                         "cgroup_rstat_updated", "update_curr", "update_cfs_group"]
#     patterns = [
#             re.compile("idle"),
#             re.compile("irq"),
#             re.compile("lock"),
#             re.compile("mutex"),
#             re.compile("rcu"),
#             re.compile("kcompactd"),
#             re.compile("ktime"),
#             re.compile("timer"),
#             re.compile("tick"),
#             re.compile("apic"),
#             re.compile(r"account_.*time"),
#             re.compile("cputime"),
#             re.compile(r"acct_.*_.*time"),
#             re.compile(r"cpuacct_.*"),
#             re.compile(r"update.*_.*time.*"),
#             re.compile("audit"), # auditd
#         ]
    
#     if name in hard_filter_set:
#         return True

#     if any(p.search(name) for p in patterns):
#         return True
    
#     return False    

def load_functions(filepath, should_filter=True):
    """Read all function names from a file, one per line."""
    funcs = set()
    try:
        with open(filepath) as f:
            for line in f:
                name = line.strip()
                if name:
                    if should_filter and should_filter_function(name):
                        continue
                    funcs.add(name)
    except IOError:
        # File not found or unreadable
        return None
    return funcs


def main():
    args = parse_args()
    # syscall table entries dir
    entries = [] 
    for entry in os.listdir(args.profiles_dir):
        parts = entry.split(":")
        if len(parts) != 2:
            continue
        entries.append(entry)
    
    #sort entry by part[0]
    entries.sort(key=lambda x: int(x.split(':')[0]))

    # Collect all result lines
    results = []
    results.append(f"Baseline name: {args.base}")
    for entry in entries:
        dirpath = os.path.join(args.profiles_dir, entry)
        if not os.path.isdir(dirpath):
            continue

        # Load baseline
        base_path = os.path.join(dirpath, args.base)
        base_funcs = load_functions(base_path)
        if base_funcs is None:
            continue

        # Add header for this syscall
        results.append(entry)

        # Compare each diff profile
        for prof in args.diffs:
            prof_path = os.path.join(dirpath, prof)
            prof_funcs = load_functions(prof_path)
            if prof_funcs is None:
                results.append(
                    f"    {prof}: total funcs: NA; "
                )
                continue

            total = len(prof_funcs)
            diff = sorted(prof_funcs - base_funcs)
            missing = len(diff)
            pct = (missing / total * 100) if total else 0.0
            diff_list = ", ".join(diff)

            results.append(
                f"    {prof}: total funcs: {total}; "
                f"not profiled: {missing}/{total} ({pct:.1f}%) [{diff_list}]"
            )

    # Write all results to a single output file
    try:
        with open(args.output, 'w') as outf:
            outf.write("\n".join(results) + "\n")
        print(f"Wrote consolidated results to {args.output}")
    except IOError as e:
        print(f"Error writing output file {args.output}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
