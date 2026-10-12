/*
 * Copyright (c) 2026 morpheus
 * AArch64 CPU identification. Ported from hs-fbb arch_lite/arm64/src/cpu.c.
 */

#include "los_typedef.h"
#include "los_compiler.h"
#include "los_arch_interrupt.h"

typedef struct {
    const UINT32 partNo;
    const CHAR *cpuName;
} CpuVendor;

STATIC CpuVendor g_cpuTable[] = {
    { 0xd03, "Cortex-A53" }, /* armv8-a  */
    { 0xd04, "Cortex-A35" }, /* armv8-a  */
    { 0xd05, "Cortex-A55" }, /* armv8.2-a */
    { 0xd07, "Cortex-A57" }, /* armv8-a  */
    { 0xd08, "Cortex-A72" }, /* armv8-a  */
};

/*
 * OHOS clang's aarch64 builtins (cpu_model.c) query AT_HWCAP at init to
 * pick LSE atomics. On the bare-metal kernel there is no auxv: return 0
 * so the builtins fall back to the baseline LL/SC implementations.
 */
unsigned long getauxval(unsigned long type)
{
    (VOID)type;
    return 0;
}

const CHAR *ArchCpuInfo(VOID)
{
    UINT64 midr;
    UINT32 partNo;

    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    /* [15:4] is the primary part number */
    partNo = (UINT32)((midr & 0xFFF0) >> 0x4);

    for (UINT32 i = 0; i < sizeof(g_cpuTable) / sizeof(CpuVendor); i++) {
        if (partNo == g_cpuTable[i].partNo) {
            return g_cpuTable[i].cpuName;
        }
    }

    return "unknown";
}
