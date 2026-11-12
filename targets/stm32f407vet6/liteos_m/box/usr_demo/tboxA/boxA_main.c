#include "stdio.h"
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "syscalls.h"
#ifdef LOSCFG_SHELL
#include "los_shell.h"
#endif

LOS_TASK_STACK_ALIGN UINT8 g_boxaStack[LOS_BOXA_STACK_SIZE]
    __attribute__((section(".boxA.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxaTaskCB[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxaKerStack[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxaMuxCB[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaMuxEntity[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosQueueCB    g_boxaQueCB[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaQueEntity[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxaSIdPool[LOSCFG_BOXA_SID_LIMIT];

#ifdef LOSCFG_SHELL
STATIC UINT32 OsShellCmdTest(UINT32 argc, const CHAR **argv)
{
    PRINTK("OsShellCmdTest: boxA\n");
    return 0;
}
#endif

/* Multi-level fault chain: every level is noinline and disable_tail_calls
 * (LTO turned the inter-level calls into tail calls when the result went
 * unused - which flattens the stack frames the backtrace needs), and the
 * return value is consumed by the caller so post-call work stays live.
 * Level3 holds local buffers as stack noise to exercise the filtering. */
static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel4(volatile UINT32 *p, UINT32 tag)
{
    LOS_PrintStr("boxA: L4, deref NULL now\n");
    UINT32 val = *p; /* MemManage DACCVIOL, MMAR=0 */
    return val ^ tag;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel3(volatile UINT32 *p, UINT32 tag)
{
    UINT32 buf[4];
    buf[0] = tag;
    buf[1] = tag + 1U;
    buf[2] = tag + 2U;
    buf[3] = tag + 3U;
    LOS_PrintStr("boxA: L3\n");
    UINT32 r = BoxAFaultLevel4(p, buf[0] + buf[1] + buf[2] + buf[3]);
    return r + 1U;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel2(volatile UINT32 *p, UINT32 tag)
{
    LOS_PrintStr("boxA: L2\n");
    UINT32 r = BoxAFaultLevel3(p, tag * 2U + 1U);
    return r + 2U;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel1(VOID)
{
    /* Anti-optimization: addr is a volatile local (its value 0 stays
     * opaque to the optimizer, no constant folding), and the pointee
     * is volatile too, so the load in Level4 can never be eliminated
     * under -O2/LTO. boxA runs unprivileged: 0x0 is outside every
     * MPU region, so the load raises MemManage (DACCVIOL, MMAR=0x0). */
    volatile UINTPTR addr = 0;
    volatile UINT32 *nullPtr = (volatile UINT32 *)addr;
    LOS_PrintStr("boxA: L1\n");
    UINT32 r = BoxAFaultLevel2(nullPtr, 0xA5U);
    return r + 3U;
}

VOID boxAEntry(VOID)
{
    /* Kernel side: box heap is initialized in OsBoxEnvBuild. */
    while (1) {
        LOS_PrintStr("Hello boxA!\n");
        UINT32 r = LOS_OffsetTest(1, 2, 3, 4, 5, 6, 7, 8);
        if (r == 0x12345678U) {
            LOS_PrintStr("OffsetTest 8-arg OK\n");
        } else {
            LOS_PrintStr("OffsetTest FAIL\n");
        }

        /* The chain result must be consumed: an unused return value lets
         * LTO dead-code the post-call arithmetic and re-create tail calls
         * even with disable_tail_calls on the callees. */
        LOS_PrintStr("boxA: entering 4-level fault chain\n");
        UINT32 chain = BoxAFaultLevel1();
        if (chain == 0x12345678U) { /* unreachable: L4 faults first */
            LOS_PrintStr("boxA: chain returned, unexpected!\n");
        }

        LOS_TaskDelay(1000);
    }
}
