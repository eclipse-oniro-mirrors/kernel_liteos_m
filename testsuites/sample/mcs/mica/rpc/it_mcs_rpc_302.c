/* 用例简要描述: ItMcsRpc302 双任务交替 call 2×500 次（契约内排队，无死锁） */
#include "it_mcs.h"

static volatile UINT32 g_srpcN[2];
static volatile UINT32 g_srpcTurn;

static VOID SrpcTask(UINT32 arg)
{
    uint8_t req[4] = "ab";
    uint8_t resp[8];
    uint32_t respLen = 0;
    int i, ret;

    for (i = 0; i < 500; i++) {
        /* 交替令牌（契约内串行化：同一时刻仅一方 call） */
        while (g_srpcTurn != arg) {
            LOS_TaskDelay(1);
        }
        respLen = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 8000);
        if (ret == 0) {
            g_srpcN[arg]++;
        }
        g_srpcTurn = (arg == 0) ? 1 : 0;
    }
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;

    g_srpcN[0] = 0;
    g_srpcN[1] = 0;
    g_srpcTurn = 0;

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)SrpcTask;
    p.pcName = "srpcB";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    p.uwArg = 1;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 本任务当 A 方 */
    for (i = 0; i < 500; i++) {
        while (g_srpcTurn != 0) {
            LOS_TaskDelay(1);
        }
        {
            uint8_t req[4] = "cd";
            uint8_t resp[8];
            uint32_t respLen = 0;

            ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 8000);
            if (ret == 0) {
                g_srpcN[0]++;
            }
        }
        g_srpcTurn = 1;
    }
    /* 等 B 方收尾 */
    for (i = 0; i < 3000; i++) {
        if (g_srpcN[1] == 500) {
            break;
        }
        LOS_TaskDelay(10);
    }
    dprintf("[StressRpc02] A=%u B=%u\n", g_srpcN[0], g_srpcN[1]);
    ICUNIT_GOTO_EQUAL(g_srpcN[0], 500, (int)g_srpcN[0], EXIT);
    ICUNIT_GOTO_EQUAL(g_srpcN[1], 500, (int)g_srpcN[1], EXIT);
    (VOID)LOS_TaskDelete(tid);
    return LOS_OK;
EXIT:
    (VOID)LOS_TaskDelete(tid);
    return LOS_NOK;
}

VOID ItMcsRpc302(VOID)
{
    TEST_ADD_CASE("ItMcsRpc302", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
