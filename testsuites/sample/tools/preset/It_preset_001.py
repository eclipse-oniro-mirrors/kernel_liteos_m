#!/usr/bin/env python3
"""ItPreset001: preset 基础设施与接口清单在位性测试 (preset_全量用例.md 1001/FT-00)."""
from os_test import *

def TestCase():
    files = [
        ("Kconfig", f"{KLM}/Kconfig"), ("kernel/Kconfig", f"{KLM}/kernel/Kconfig"),
        ("base/mem/Kconfig", f"{KLM}/kernel/base/mem/Kconfig"),
        ("signal/Kconfig", f"{KLM}/kernel/extended/signal/Kconfig"),
        ("fs/vfs/Kconfig", f"{KLM}/fs/vfs/Kconfig"), ("posix/Kconfig", f"{KLM}/lib/posix/Kconfig"),
        ("shell/Kconfig", f"{KLM}/shell/Kconfig"), ("Makefile", f"{KLM}/Makefile"),
        ("tools/apply_preset.py", f"{KLM}/tools/apply_preset.py"),
        ("tools/allnoconfig.py", f"{KLM}/tools/allnoconfig.py"),
        ("los_config.h", f"{KLM}/kernel/include/los_config.h"),
        ("tcb_verify", f"{KLM}/testsuites/sample/kernel/tcb_verify"),
    ]
    checks = [("文件在位: " + n, "在位", "在位" if os.path.exists(p) else "缺失", os.path.exists(p)) for n, p in files]
    mk = open(f"{KLM}/Makefile").read()
    checks.append(("Makefile savemenuconfig 目标", "在位", "在位" if "savemenuconfig:" in mk else "缺失", "savemenuconfig:" in mk))
    checks.append(("Makefile allnoconfig 目标", "在位", "在位" if re.search(r"^allnoconfig:", mk, re.M) else "缺失", bool(re.search(r"^allnoconfig:", mk, re.M))))
    k = kload(QEMU_VENDOR)
    strscan = set()
    for sym in k.unique_defined_syms:
        for dv, cond in sym.defaults:
            if "KERNEL_RAM_PRESET" in str(cond):
                strscan.add(sym.name); break
    checks.append(("str(cond) 扫描 managed 数(缺陷②口径)", "61", str(len(strscan)), len(strscan) == 61))
    sym34 = ["KERNEL_BACKTRACE", "KERNEL_CPUP", "PLATFORM_EXC", "BASE_CORE_TSK_LIMIT",
             "BASE_CORE_TSK_MIN_STACK_SIZE", "BASE_CORE_TSK_IDLE_STACK_SIZE", "BASE_CORE_TSK_SWTMR_STACK_SIZE",
             "BASE_CORE_SWTMR", "BASE_CORE_SWTMR_LIMIT", "BASE_CORE_SWTMR_IN_ISR", "BASE_IPC_EVENT",
             "BASE_IPC_SEM", "BASE_IPC_SEM_LIMIT", "BASE_IPC_MUX", "BASE_IPC_MUX_LIMIT",
             "BASE_IPC_QUEUE", "BASE_IPC_QUEUE_LIMIT",
             "KERNEL_MEM_SLAB_EXTENTION", "MEM_TASK_STAT", "KERNEL_SIGNAL", "FS_VFS",
             "POSIX_THREAD_API", "POSIX_MALLOC_API",
             "POSIX_SEM_API", "POSIX_CLOCK_API", "POSIX_MQUEUE_API", "POSIX_PIPE_API",
             "SHELL", "BASE_CORE_SYS_RES_CHECK",
             "PLATFORM_HWI_LIMIT", "HWI_STACK_SIZE",
             "EXC_STACK_SIZE", "NMI_STACK_SIZE", "IRQ_STACK_SIZE"]
    ksrc = ""
    for root, _, fs in os.walk(KLM):
        for fn in fs:
            if fn == "Kconfig" and "/testsuites" not in root:
                ksrc += open(os.path.join(root, fn)).read()
    def has_tier_default(name):
        m = re.search(rf"config {name}\b.*?(?=\nconfig |\nchoice |\nmenuconfig |\nendchoice |\Z)", ksrc, re.S)
        return bool(m and re.search(r"default .* if KERNEL_RAM_PRESET", m.group(0)))
    nsym = sum(1 for n in sym34 if has_tier_default(n))
    ch_choice = len(re.findall(r"^\s*default (KERNEL_MEM_BESTFIT_LITTLE|MUTEX_WAITMODE_PRIO) if KERNEL_RAM_PRESET",
                                ksrc, re.M))
    checks.append(("符号级 managed(源文 tier 条件 default)", "34", str(nsym), nsym == 34))
    checks.append(("choice 级 managed(choice default if 挡位)", "2", str(ch_choice), ch_choice == 2))
    real_in_scan = sum(1 for n in sym34 if n in strscan)
    checks.append(("符号级真实项均在扫描集内", "34", str(real_in_scan), real_in_scan == 34))
    checks.append(("误匹配数(61-34)", "27", str(len(strscan) - real_in_scan), len(strscan) - real_in_scan == 27))
    record("ItPreset001", checks)


def ItPreset001():
    TEST_ADD_CASE("ItPreset001", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
