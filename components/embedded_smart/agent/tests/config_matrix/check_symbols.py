#!/usr/bin/env python3
import argparse
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact")
    parser.add_argument("--reject", action="append", default=[])
    args = parser.parse_args()
    symbols = subprocess.run(
        ["nm", "-C", args.artifact], check=True, text=True, capture_output=True
    ).stdout
    found = [symbol for symbol in args.reject if symbol in symbols]
    if found:
        raise SystemExit("rejected symbols present: " + ", ".join(found))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
