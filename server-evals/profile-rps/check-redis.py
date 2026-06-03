#!/usr/bin/env python3
import sys

def main(path):
    try:
        with open(path, 'r') as f:
            lines = [line.strip() for line in f if line.strip()]
    except Exception:
        return 1

    if len(lines) != 2:
        return 1

    if not lines[0].startswith('"SET"'):
        return 1
    if not lines[1].startswith('"GET"'):
        return 1

    return 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: check_csv.py <csv-file>", file=sys.stderr)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
