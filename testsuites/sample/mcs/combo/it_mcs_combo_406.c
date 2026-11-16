/* 用例简要描述: ItMcsCombo406 CAN 回环收发 × RPC PING 并发（驱动 IRQ/收割与 rpmsg 共享单核，T6） */
#include "it_mcs.h"
#include "can.h"
#ifdef LOSCFG_MCS_CAN

#define C6_CAN_N  200
#define C6_PING_N 100

static volatile UINT32 g_c6PingOk;
static volatile UINT32 g_c6CanOk;
static volatile UINT32 g_c6CanFails;
static canDevice *g_c6Dev;

static VOID C6CanTask(UINT32 arg)
{
    canFrame f, rx;
    UINT32 i, j;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.len = 8;
    for (i = 0; i < C6_CAN_N; i++) {
        f.id = 0x200 + (i & 0xff);
        for (j = 0; j < 8; j++) {
            f.data[j] = (uint8_t)(i + j);
        }
        ret = CanSend(g_c6Dev, &f, 100000);
        if (ret != 0) {
            dprintf("[Combo06] can#%u send=%d\n", i, ret);
            fails++;
            continue;
        }
        for (j = 0; j < 300; j++) {
            if (CanRecv(g_c6Dev, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(5);
        }
        if (j >= 300 || rx.id != f.id || rx.len != f.len || memcmp(rx.data, f.data, 8) != 0) {
            dprintf("[Combo06] can#%u recv-mismatch j=%u\n", i, j);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    g_c6CanOk = i;
    g_c6CanFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canStats st;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c6p";
    uint8_t resp[8];
    uint32_t rl;

    g_c6PingOk = 0;
    g_c6CanOk = 0;
    g_c6CanFails = 0xFFFFFFFF;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    ret = CanInit(&cfg, &g_c6Dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(g_c6Dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_DRV);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C6CanTask;
    p.pcName = "c6can";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV);

    /* 本任务: PING 流 */
    for (i = 0; i < C6_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c6PingOk++;
        } else {
            dprintf("[Combo06] ping#%u ret=%d\n", i, ret);
        }
    }
    for (i = 0; i < 6000; i++) {
        if (g_c6CanFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    ret = CanGetStats(g_c6Dev, &st);
    dprintf("[Combo06] pingOk=%u/%d canOk=%u canFails=%u tx=%u rx=%u dup=%u\n",
            g_c6PingOk, C6_PING_N, g_c6CanOk, g_c6CanFails,
            st.txAccepted, st.rxDelivered, st.rxDupSuppressed);
    (VOID)LOS_TaskDelete(tid);
    (VOID)CanStop(g_c6Dev, 100000);
    g_c6Dev = NULL;
    ICUNIT_GOTO_EQUAL(g_c6PingOk, C6_PING_N, (int)g_c6PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c6CanFails, 0, (int)g_c6CanFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c6CanOk, C6_CAN_N, (int)g_c6CanOk, EXIT);
    return LOS_OK;
EXIT_DRV:
    (VOID)CanStop(g_c6Dev, 100000);
    g_c6Dev = NULL;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo406(VOID)
{
    TEST_ADD_CASE("ItMcsCombo406", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_MCS_CAN */