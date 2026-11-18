#!/usr/bin/env python3
"""ItPreset003: 三挡位 managed 符号 §7.2 值一致性测试 (preset_全量用例.md 1003/FT-02).

内联 §7.2 表比对(原 check_preset_values.py 承载体已随 run_20261111 删除,
语义保留: 逐 config PASS/DEV/FAIL 计数 + 已登记偏差注册表).
"""
from os_test import *

T72 = [  # (符号, TINY, SMALL, STANDARD)  tri: 0/2; int: 数值; None: 该挡无 preset default
    ("KERNEL_BACKTRACE", 0, 0, 2), ("KERNEL_CPUP", 0, 0, 2), ("PLATFORM_EXC", 0, 0, 0),
    ("BASE_CORE_TSK_LIMIT", 1, 6, 16), ("BASE_CORE_TSK_MIN_STACK_SIZE", 128, 256, 512),
    ("BASE_CORE_TSK_IDLE_STACK_SIZE", 128, 512, 512), ("BASE_CORE_TSK_SWTMR_STACK_SIZE", 0, 512, 512),
    ("BASE_CORE_SWTMR", 0, 2, 2), ("BASE_CORE_SWTMR_LIMIT", 2, 4, 16), ("BASE_CORE_SWTMR_IN_ISR", 2, 0, 0),
    ("BASE_IPC_EVENT", 0, 2, 2), ("BASE_IPC_SEM", 0, 2, 2), ("BASE_IPC_SEM_LIMIT", 4, 8, 16),
    ("BASE_IPC_MUX", 0, 2, 2), ("BASE_IPC_MUX_LIMIT", None, 8, 16), ("BASE_IPC_QUEUE", 0, 0, 2),
    ("BASE_IPC_QUEUE_LIMIT", None, None, 16),
    ("KERNEL_SIGNAL", 0, 0, 2), ("FS_VFS", 0, 0, 0),
    ("POSIX_THREAD_API", 0, 0, 2), ("POSIX_MALLOC_API", 0, 0, 2),
    ("POSIX_SEM_API", 0, 0, 2), ("POSIX_CLOCK_API", 0, 0, 2),
    ("POSIX_MQUEUE_API", 0, 0, 2), ("POSIX_PIPE_API", 0, 0, 2),
    ("SHELL", 0, 0, 0), ("BASE_CORE_SYS_RES_CHECK", 0, 2, 2),
    ("PLATFORM_HWI_LIMIT", 24, 48, 72), ("HWI_STACK_SIZE", 512, 512, 1024),
    ("EXC_STACK_SIZE", 0, 0, 512), ("NMI_STACK_SIZE", 0, 0, 0), ("IRQ_STACK_SIZE", 512, 512, 1024),
    ("KERNEL_MEM_SLAB_EXTENTION", 0, 0, 2), ("MEM_TASK_STAT", 0, 0, 2),
]
TIER_IDX = {"tiny": 1, "small": 2, "standard": 3}
MEM_MEMBER = {"tiny": "KERNEL_MEM_BESTFIT_LITTLE", "small": "KERNEL_MEM_BESTFIT_LITTLE", "standard": "KERNEL_MEM_TLSF"}
WAIT_MEMBER = {"small": "MUTEX_WAITMODE_FIFO", "standard": "MUTEX_WAITMODE_PRIO"}

CHILD_PARENT = {"BASE_CORE_SWTMR_LIMIT": "BASE_CORE_SWTMR", "BASE_CORE_SWTMR_IN_ISR": "BASE_CORE_SWTMR",
                "BASE_IPC_SEM_LIMIT": "BASE_IPC_SEM", "BASE_IPC_MUX_LIMIT": "BASE_IPC_MUX",
                "BASE_IPC_QUEUE_LIMIT": "BASE_IPC_QUEUE"}
SYM_EXP = {s: (t, v, st) for s, t, v, st in T72}

def TestCase():
    d = mkcase("ItPreset003")
    lines = []
    total_pass = total_dev = total_fail = 0
    for board in ("qemu", "ws63"):
        for tier in ("tiny", "small", "standard"):
            f = f"{OUT}/itp002/{board}_{tier}.config"
            k = kload(f)
            npass = ndev = nfail = 0
            def dev(sym, reason):
                nonlocal ndev
                ndev += 1
                lines.append(f"  DEV  {sym}({reason})")
            def is_dev(sym, board, tier, actual):
                if sym in ("KERNEL_MEM_SLAB_EXTENTION", "MEM_TASK_STAT") and tier == "standard":
                    return "§8.2 MEM choice=TLSF 致不可见"
                if sym == "KERNEL_BACKTRACE" and board == "ws63" and tier in ("tiny", "small"):
                    return "MEM_LEAKCHECK select 穿透偏差"
                if sym == "KERNEL_SIGNAL" and board == "ws63":
                    return "ARCH_ARM_CORTEX_M 架构门控, riscv 板不可见"
                return None
            for sym, tv, sv, stv in T72:
                exp = (tv, sv, stv)[TIER_IDX[tier] - 1]
                if exp is None:
                    continue
                parent = CHILD_PARENT.get(sym)
                if parent is not None and SYM_EXP[parent][TIER_IDX[tier] - 1] == 0:
                    continue
                s = k.syms.get(sym)
                raw = s.str_value if s is not None else None
                actual = {"y": 2, "n": 0, "m": 1}.get(raw, raw)
                try:
                    actual = int(actual)
                except (TypeError, ValueError):
                    pass
                if actual == exp:
                    npass += 1
                    continue
                reason = is_dev(sym, board, tier, actual)
                if reason:
                    dev(sym, f"{reason}, 期望{exp} 实际{actual}")
                else:
                    nfail += 1
                    lines.append(f"  FAIL {sym}: expect={exp} actual={actual}")
            for name, expm in (("MEM choice", MEM_MEMBER[tier]),):
                act = tri(k, expm)
                others = [m for m in ("KERNEL_MEM_TLSF", "KERNEL_MEM_BESTFIT", "KERNEL_MEM_BESTFIT_LITTLE")
                          if tri(k, m) == 2]
                if act == 2:
                    npass += 1
                elif (board == "qemu" and tier == "standard" and tri(k, "KERNEL_MEM_TLSF") == 2):
                    dev("MEM choice", "§8.2 实证 tlsf(期望口径待确认)")
                elif board == "ws63" and tri(k, "KERNEL_MEM_BESTFIT") == 2:
                    dev("MEM choice", "vendor 显式 LOSCFG_KERNEL_MEM_BESTFIT=y 经 D10 保留")
                else:
                    nfail += 1
                    lines.append(f"  FAIL MEM choice: expect={expm} actual={others}")
            if tier in WAIT_MEMBER:
                expm = WAIT_MEMBER[tier]
                if tri(k, expm) == 2:
                    npass += 1
                elif board == "ws63" and tri(k, "MUTEX_WAITMODE_PRIO") == 2:
                    dev("MUTEX_WAITMODE", "vendor 显式 PRIO 经 D10 保留, 期望 FIFO")
                else:
                    nfail += 1
            lines.append(f"[{board}_{tier}] PASS={npass} DEV={ndev} FAIL={nfail}")
            total_pass += npass; total_dev += ndev; total_fail += nfail
    open(f"{d}/raw_output.txt", "w").write("\n".join(lines) + "\n")
    checks = [("六配置执行", "FAIL=0", f"FAIL={total_fail}", total_fail == 0),
              ("登记偏差项数(缺陷①/穿透/§8.2/D10)", ">0", f"{total_dev} 项", total_dev > 0)]
    record("ItPreset003", checks)


def ItPreset003():
    TEST_ADD_CASE("ItPreset003", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
