#!/usr/bin/env python3
import argparse
import fnmatch
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--product", required=True)
    args = parser.parse_args()
    changed = subprocess.run(
        ["git", "diff", "--name-only", f"{args.base}..{args.head}"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()
    allowed = [
        f"defconfigs/{args.product}.defconfig",
        f"products/{args.product}/**",
        f"tests/products/{args.product}_smoke.yml",
    ]
    rejected = [
        path
        for path in changed
        if not any(fnmatch.fnmatch(path, pattern) for pattern in allowed)
    ]
    if rejected:
        raise SystemExit("zero-code Product guard rejected: " + ", ".join(rejected))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
