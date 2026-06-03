#!/usr/bin/env python3
"""
Merge kernel function lists for multiple programs across syscall profiles.

Usage:
  python syscall-merge-diff-prog.py -i progA progB ... -o merged.txt

This script reads `syscall_table`, iterates over directories under
`syscall_profiles/<sys_id>:<sys_name>`, and for each directory, looks for
`<prog>-all.txt` files. If any exist, it merges their contents (unique function names)
and writes the result to `syscall_profiles/<sys_id>:<sys_name>/<merge_name>`.
"""
import argparse
import os
import sys

def parse_args():
    parser = argparse.ArgumentParser(
        description="Merge syscall profile differences across programs"
    )
    parser.add_argument(
        '-i', '--inputs',
        nargs='+', required=True,
        help='List of program names to merge'
    )
    parser.add_argument(
        '-o', '--output',
        required=True,
        help='Output filename for merged functions'
    )
    parser.add_argument(
        '--profiles-dir',
        default='syscall_profiles',
        help='Base directory containing syscall_profiles (default: syscall_profiles/)'
    )
    return parser.parse_args()


def main():
    args = parse_args()

    # syscall table entries dir
    entries = [] 
    for entry in os.listdir(args.profiles_dir):
        parts = entry.split(":")
        if len(parts) != 2:
            continue
        entries.append(entry)

    # Process each syscall directory
    for entry in entries:
        dir_path = os.path.join(args.profiles_dir, entry)
        if not os.path.isdir(dir_path):
            print(f"Warning: {dir_path} is not a directory.")
            continue

        merged_funcs = set()
        # Collect functions from each program file
        for prog in args.inputs:
            prog_file = os.path.join(dir_path, f"{prog}-all.txt")
            if os.path.isfile(prog_file):
                with open(prog_file) as pf:
                    for line in pf:
                        func = line.strip()
                        if func:
                            merged_funcs.add(func)

        # If any functions found, write merged output
        if merged_funcs:
            out_path = os.path.join(dir_path, args.output)
            with open(out_path, 'w') as out_f:
                for func in sorted(merged_funcs):
                    out_f.write(func + '\n')
            print(f"Merged {len(merged_funcs)} functions into: {out_path}")

if __name__ == '__main__':
    main()
