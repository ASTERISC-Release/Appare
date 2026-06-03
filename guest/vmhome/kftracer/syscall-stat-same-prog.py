import sys
import os

def usage():
    print(f"Usage: {sys.argv[0]} <program>", file=sys.stderr)
    sys.exit(1)

if len(sys.argv) != 2:
    usage()

program = sys.argv[1]
base_dir = 'syscall_profiles'
exec_file = os.path.join(base_dir, 'executed_syscalls', f"{program}.txt")

if not os.path.isfile(exec_file):
    print(f"Error: executed syscalls file '{exec_file}' not found", file=sys.stderr)
    sys.exit(1)

# read executed syscalls
syscalls = []  # list of "id:name"
with open(exec_file) as f:
    for line in f:
        tok = line.strip()
        if tok:
            syscalls.append(tok)

syscalls.append('common')

# for each syscall, collect per-run function sets
all_funcs = {}   # key -> list of sets
for sc in syscalls:
    sc_dir = os.path.join(base_dir, sc)
    if not os.path.isdir(sc_dir):
        print(f"[{program}] Warning: directory '{sc_dir}' not found, skipping", file=sys.stderr)
        continue
    sets = []
    for fname in os.listdir(sc_dir):
        if (fname.startswith(f"{program}-") and 
            not fname.startswith(f"{program}-all") and
            fname.endswith('.txt')):
            path = os.path.join(sc_dir, fname)
            with open(path) as sf:
                funcs = set(line.strip() for line in sf if line.strip())
                sets.append((fname, funcs))
    if sets:
        all_funcs[sc] = sets

# compute diffs and write union outputs
for sc, runs in all_funcs.items():
    # gather union and intersection
    names = [fname for fname, _ in runs]
    union = set().union(*(s for _, s in runs))
    inter = set.intersection(*(s for _, s in runs))
    diff = union - inter
    if diff:
        print(f"Differences (total {len(diff)} functions) in {sc} across {len(runs)} runs:")
        for fn in sorted(diff):
            present = [fname for fname, s in runs if fn in s]
            missing = [fname for fname, s in runs if fn not in s]
            print(f"  {fn}: present ({len(present)}/{len(names)} times) in {present}, missing in {missing}")
        print()
    else:
        print(f"No differences in {sc} across {len(runs)} runs")
        print()
        
    # write merged (union) functions to <program>-all.txt
    sc_dir = os.path.join(base_dir, sc)
    out_all = os.path.join(sc_dir, f"{program}-all.txt")
    with open(out_all, 'w') as of:
        for fn in sorted(union):
            of.write(fn + '\n')
    print(f"Wrote merged union functions to {out_all}")
