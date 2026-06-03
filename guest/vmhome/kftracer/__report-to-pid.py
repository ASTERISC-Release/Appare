#!/usr/bin/env python3
import sys
import os

def usage():
    print(f"Usage: {sys.argv[0]} <program>", file=sys.stderr)
    sys.exit(1)

if len(sys.argv) != 2:
    usage()

program = sys.argv[1]
input_file = 'report.txt'

if not os.path.isfile(input_file):
    print(f"Error: '{input_file}' not found", file=sys.stderr)
    sys.exit(1)

# We'll stream lines to per-thread output files to avoid OOM
file_handles = {}   # tid -> file object
line_counts = {}    # tid -> count

with open(input_file) as f:
    for line in f:
        line = line.rstrip()
        if not line:
            continue
        tok = line.split()[0]
        parts = tok.rsplit('-', 1)
        if len(parts) != 2:
            continue
        proc, tid = parts
        if proc != program:
            continue
        # open file handle if not already
        if tid not in file_handles:
            out_name = f"report-cpu{tid}.txt"
            print(f"Creating: {out_name}")
            fh = open(out_name, 'w')
            file_handles[tid] = fh
            line_counts[tid] = 0
        # write line
        fh = file_handles[tid]
        fh.write(line + '\n')
        line_counts[tid] += 1

# close all handles and report counts
for tid, fh in file_handles.items():
    fh.close()
    out_name = f"report-cpu{tid}.txt"
    print(f"Wrote {line_counts[tid]} lines to {out_name}")