/* 用例简要描述: ItMcsCombo407 Net(GMAC1) 发送 × RPC PING 并发（GE 数据面与框架并发，T6） */
#include "it_mcs.h"
#include "net.h"
#ifdef LOSCFG_MCS_NET

#define C7_NET_N  300
#define C7_PING_N 100

static volatile UINT32 g_c7PingOk;
static volatile UINT32 g_c7NetFails;
static volatile UINT32 g_c7NetBusy;
static netDevice *g_c7Dev;

static VOID C7NetTask(UINT32 arg)
{
    static uint8_t frame[64];
    UINT32 i;
    int ret, fails = 0;
    UINT32 busy = 0;

    (void)arg;
    (void)memset_s(frame, sizeof(frame), 0xEE, sizeof(frame));
    for (i = 0; i < C7_NET_N; i++) {
        ret = NetSend(g_c7Dev, frame, 64);
        if (ret == -11) {
            busy++;
            LOS_TaskDelay(2);
            continue;
        }
        if (ret != 0) {
            dprintf("[Combo07] net#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    g_c7NetFails = (UINT32)fails;
    g_c7NetBusy = busy;
}

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netStats st;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c7p";
    uint8_t resp[8];
    uint32_t rl;

    g_c7PingOk = 0;
    g_c7NetFails = 0xFFFFFFFF;
    g_c7NetBusy = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &g_c7Dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(g_c7Dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_DRV);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C7NetTask;
    p.pcName = "c7net";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV);

    /* 本任务: PING 流 */
    for (i = 0; i < C7_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c7PingOk++;
        } else {
            dprintf("[Combo07] ping#%u ret=%d\n", i, ret);
        }
    }
    for (i = 0; i < 6000; i++) {
        if (g_c7NetFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    ret = NetGetStats(g_c7Dev, &st);
    dprintf("[Combo07] pingOk=%u/%d netFails=%u busy=%u txAccepted=%u txErrors=%u\n",
            g_c7PingOk, C7_PING_N, g_c7NetFails, g_c7NetBusy,
            st.txAccepted, st.txErrors);
    (VOID)LOS_TaskDelete(tid);
    (VOID)NetStop(g_c7Dev, 100000);
    g_c7Dev = NULL;
    ICUNIT_GOTO_EQUAL(g_c7PingOk, C7_PING_N, (int)g_c7PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c7NetFails, 0, (int)g_c7NetFails, EXIT);
    ICUNIT_GOTO_EQUAL(st.txErrors, 0, (int)st.txErrors, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, (uint32_t)(C7_NET_N - g_c7NetBusy), (int)(st.txAccepted - (C7_NET_N - g_c7NetBusy)), EXIT);
    return LOS_OK;
EXIT_DRV:
    (VOID)NetStop(g_c7Dev, 100000);
    g_c7Dev = NULL;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo407(VOID)
{
    TEST_ADD_CASE("ItMcsCombo407", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_MCS_NET */