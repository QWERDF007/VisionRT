#!/usr/bin/env python3
"""
Archive test evidence from build output directory to documentation evidence directory.
Follows HS5-04 and HS5-06 rules: tests write only to build/evidence, and this tool
archives evidence to docs/evidence without polluting source during automated test runs.
"""

import argparse
import os
import shutil
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Archive test evidence to docs directory.")
    parser.add_argument("--src", type=str, default="build/evidence", help="Source evidence directory")
    parser.add_argument("--dst", type=str, default="docs/evidence", help="Destination evidence directory")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    src_dir = (repo_root / args.src).resolve() if not Path(args.src).is_absolute() else Path(args.src)
    dst_dir = (repo_root / args.dst).resolve() if not Path(args.dst).is_absolute() else Path(args.dst)

    if not src_dir.exists() or not src_dir.is_dir():
        print(f"Error: Source evidence directory does not exist: {src_dir}", file=sys.stderr)
        return 1

    dst_dir.mkdir(parents=True, exist_ok=True)
    copied = 0
    print(f"Archiving evidence from {src_dir} to {dst_dir}...")

    for item in src_dir.iterdir():
        if item.is_file():
            target = dst_dir / item.name
            shutil.copy2(item, target)
            size_bytes = target.stat().st_size
            print(f"  [COPIED] {item.name} ({size_bytes} bytes)")
            copied += 1

    print(f"Evidence archive complete: {copied} file(s) copied.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
