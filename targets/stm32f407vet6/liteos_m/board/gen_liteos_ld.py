#!/usr/bin/env python3
"""gen_liteos_ld.py - expand the single linker-script template.

GNU ld does not run cpp on linker scripts: #ifdef in a .ld file is dead text
(both MEMORY blocks used to land, the later one winning - caught via
"redeclaration of memory region" warnings). So the conditional lives in
liteos.ld.in and this script expands it at build time into the plain .ld
the linker consumes. Supports the minimal cpp subset the template uses:
#ifdef / #ifndef / #else / #endif with one condition name, nesting allowed.

Stack entries are (cond_value, currently_emitting):
  cond_value          - whether this block's condition (or its else) is true
  currently_emitting  - whether lines inside the current branch are emitted

Usage: gen_liteos_ld.py <input.ld.in> <output.ld> [--cov]
  --cov  define LOSCFG_TEST_KERNEL_COVERAGE (pick the #ifdef arm)
"""

import sys


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit("usage: gen_liteos_ld.py <in> <out> [--cov]")
    src, dst = args[0], args[1]
    defines = set()
    if "--cov" in args[2:]:
        defines.add("LOSCFG_TEST_KERNEL_COVERAGE")

    def outer_cond_true(stack):
        return all(cond for cond, _ in stack)

    stack = []

    out = []
    with open(src, "r") as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith("#ifdef"):
                name = stripped[len("#ifdef"):].strip()
                take = name in defines and outer_cond_true(stack)
                stack.append((take, take))
                continue
            if stripped.startswith("#ifndef"):
                name = stripped[len("#ifndef"):].strip()
                take = name not in defines and outer_cond_true(stack)
                stack.append((take, take))
                continue
            if stripped.startswith("#else"):
                cond, _ = stack.pop()
                emit = (not cond) and outer_cond_true(stack)
                stack.append((cond, emit))
                continue
            if stripped.startswith("#endif"):
                stack.pop()
                continue
            if all(emit for _, emit in stack):
                out.append(line)

    with open(dst, "w") as f:
        f.writelines(out)


if __name__ == "__main__":
    main()
