#!/usr/bin/env python3
"""ItStackusage001: 基础设施与结构测试 (W0)."""
from os_test import *

def TestCase():
    checks = []
    for rel in ("stackusage.py", "src/asm_common.py", "src/arm_read_asm.py",
                "src/riscv_read_asm.py", "README.md", "README_zh.md"):
        checks.append((f"tools/stackusage/{rel}", "在位", "在位" if os.path.isfile(f"{SU}/{rel}") else "缺失",
                       os.path.isfile(f"{SU}/{rel}")))
    n = len(funcs({"x": {}})) if False else 0
    import glob
    py = glob.glob(f"{SU}/src/*.py") + [f"{SU}/stackusage.py"]
    import subprocess as sp
    r = sp.run([sys.executable, "-c",
                "import ast,sys\n"
                "mods=set()\n"
                "for p in sys.argv[1:]:\n"
                "    for nd in ast.walk(ast.parse(open(p).read())):\n"
                "        if isinstance(nd, ast.Import): mods|={a.name.split('.')[0] for a in nd.names}\n"
                "        elif isinstance(nd, ast.ImportFrom) and nd.module: mods.add(nd.module.split('.')[0])\n"
                "print('\\n'.join(sorted(mods)))"] + py,
               capture_output=True, text=True)
    import sys as _s
    stdlib = set(getattr(_s, "stdlib_module_names", ())) | {"ast", "sys"}
    bad = [m for m in r.stdout.split() if m not in stdlib and m not in
           ("asm_common", "arm_read_asm", "riscv_read_asm", "src")]
    checks.append(("零第三方依赖(AST 审计)", "仅标准库+自有模块", f"越界模块: {bad or '无'}", not bad))
    record("ItStackusage001", checks)


def ItStackusage001():
    TEST_ADD_CASE("ItStackusage001", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
