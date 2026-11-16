/* 用例简要描述: ItMcsCombo405 正向 PING × 反向 E2E_TRIGGER 交错并发（T5 双向 call 同线程分派） */
#include "it_mcs.h"

#define C5_PING_N  100
#define C5_TRIG_N  30

static volatile UINT32 g_c5PingOk;
static volatile UINT32 g_c5BackCalled;
static volatile UINT32 g_c5TrigFails;

static int C5Handler(uint32_t func_id, const void *req, uint32_t req_len,
                     void *resp, uint32_t resp_cap)
{
    uint32_t n;

    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95; /* EOPNOTSUPP */
    }
    g_c5BackCalled++;
    n = (req_len < resp_cap) ? req_len : resp_cap;
    if (n > 0) {
        (void)memcpy_s(resp, resp_cap, req, n);
    }
    return (int)n;
}

static VOID C5TrigTask(UINT32 arg)
{
    struct {
        uint32_t handler_called;
        uint32_t echo_len;
        char echo[48];
    } tr;
    uint8_t req[8] = "c5-fwd!!";
    uint32_t trLen;
    int i, ret, fails = 0;

    (void)arg;
    for (i = 0; i < C5_TRIG_N; i++) {
        trLen = 0;
        (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
        ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, req, 8, &tr, sizeof(tr), &trLen, 8000);
        if (ret != 0 || tr.handler_called != 1 || tr.echo_len != 8 ||
            memcmp(tr.echo, req, 8) != 0) {
            dprintf("[Combo05] trig#%d ret=%d called=%u elen=%u\n",
                    i, ret, tr.handler_called, tr.echo_len);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    g_c5TrigFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c5p";
    uint8_t resp[8];
    uint32_t rl;

    g_c5PingOk = 0;
    g_c5BackCalled = 0;
    g_c5TrigFails = 0xFFFFFFFF;

    ret = mica_rpc_register_handler(C5Handler);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C5TrigTask;
    p.pcName = "c5trig";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 本任务: 正向 PING，与反向 call 交错 */
    for (i = 0; i < C5_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c5PingOk++;
        } else {
            dprintf("[Combo05] ping#%u ret=%d\n", i, ret);
        }
    }
    for (i = 0; i < 3000; i++) {
        if (g_c5TrigFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    dprintf("[Combo05] pingOk=%u/%d trigFails=%u backCalled=%u/%d\n",
            g_c5PingOk, C5_PING_N, g_c5TrigFails, g_c5BackCalled, C5_TRIG_N);
    (VOID)LOS_TaskDelete(tid);
    ICUNIT_GOTO_EQUAL(g_c5PingOk, C5_PING_N, (int)g_c5PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c5TrigFails, 0, (int)g_c5TrigFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c5BackCalled, C5_TRIG_N, (int)g_c5BackCalled, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo405(VOID)
{
    TEST_ADD_CASE("ItMcsCombo405", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
