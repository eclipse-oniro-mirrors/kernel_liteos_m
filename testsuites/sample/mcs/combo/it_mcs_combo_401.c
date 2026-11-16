/* 用例简要描述: ItMcsCombo401 RPC PING × TTY send 并发（三服务共享 vring/接收线程，T1） */
#include "it_mcs.h"

#define C1_PING_N 200
#define C1_TTY_N  300

static volatile UINT32 g_c1PingOk;
static volatile UINT32 g_c1PingFail;

static VOID C1PingTask(UINT32 arg)
{
    uint8_t req[4] = "c1p";
    uint8_t resp[8];
    uint32_t rl;
    int i, ret;

    (void)arg;
    for (i = 0; i < C1_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c1PingOk++;
        } else {
            g_c1PingFail++;
            dprintf("[Combo01] ping#%d ret=%d\n", i, ret);
            if (g_c1PingFail >= 5) {
                break; /* rpc 断路熔断——硬跑 200×8s 只会拖垮等待侧 */
            }
        }
    }
}

static UINT32 g_c1Tid = LOS_ERRNO_TSK_ID_INVALID; /* EXIT 路径强制回收（防僵尸占 rpc 信号量） */

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag d0, d1;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret, fails = 0;
    uint8_t msg[64];

    for (i = 0; i < 64; i++) {
        msg[i] = 'a' + (i % 26);
    }
    /* PRECONDITION: tty 就绪（未就绪期 send 返 0=丢帧，与 tty 用例同修） */
    {
        int wait;
        for (wait = 0; wait < 100 && !mica_service_is_ready(MICA_SERVICE_TTY); wait++) {
            LOS_TaskDelay(10);
        }
    }
    g_c1PingOk = 0;
    g_c1PingFail = 0;
    mica_rpc_get_diag(&d0);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C1PingTask;
    p.pcName = "c1ping";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    g_c1Tid = tid;

    /* 本任务: tty 流量 */
    for (i = 0; i < C1_TTY_N; i++) {
        ret = mica_tty_send(msg, 64);
        if (ret != 64) {
            dprintf("[Combo01] tty#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    /* 等 ping 任务收尾（必须等任务函数自然退出后再删——强删一个
     * 正持有 rpc 单飞行锁的任务=锁永久丢失，下一例 np_connect/mica_rpc_call
     * 挂死在 sem_wait。等待上界=熔断后剩余帧数×单帧超时+余量） */
    {
        UINT32 done_target = C1_PING_N;
        for (i = 0; i < 6000; i++) { /* 最长 60s（含 5 败熔断+余量） */
            if (g_c1PingOk + g_c1PingFail >= done_target ||
                g_c1PingFail >= 5) {
                break;
            }
            LOS_TaskDelay(10);
        }
        LOS_TaskDelay(100); /* 收尾余量：最后一帧应答/任务函数 return 路径 */
    }
    mica_rpc_get_diag(&d1);
    dprintf("[Combo01] ttyFails=%d pingOk=%u/%d pingFail=%u dTx=%u dRx=%u dTo=%u dDrop=%u\n",
            fails, g_c1PingOk, C1_PING_N, g_c1PingFail,
            d1.tx_cnt - d0.tx_cnt, d1.rx_cnt - d0.rx_cnt,
            d1.timeout_cnt - d0.timeout_cnt, d1.drop_cnt - d0.drop_cnt);
    (VOID)LOS_TaskDelete(g_c1Tid);
    g_c1Tid = LOS_ERRNO_TSK_ID_INVALID;
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c1PingOk, C1_PING_N, (int)g_c1PingOk, EXIT);
    /* tx/rx 增量断言取下限：主核 liveness 线程每 2s 注入 3 帧（2 ping + 1 stat，
     * mcs_test_server.c liveness_thread）属环境噪声；timeout/drop 必须为 0 */
    ICUNIT_GOTO_EQUAL((d1.tx_cnt - d0.tx_cnt) >= C1_PING_N, 1, (int)(d1.tx_cnt - d0.tx_cnt), EXIT);
    ICUNIT_GOTO_EQUAL((d1.rx_cnt - d0.rx_cnt) >= C1_PING_N, 1, (int)(d1.rx_cnt - d0.rx_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.timeout_cnt - d0.timeout_cnt, 0, (int)(d1.timeout_cnt - d0.timeout_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.drop_cnt - d0.drop_cnt, 0, (int)(d1.drop_cnt - d0.drop_cnt), EXIT);
    return LOS_OK;
EXIT:
    /* 断言失败提前退出：等 ping 任务收尾（最多 8.5s——单次 rpc_call 超时上界）
     * 再删任务，保证 rpc 单飞行信号量已归还；防僵尸持锁挂死下一例（实测 3 轮
     * 换用例同位置挂的根因） */
    if (g_c1Tid != LOS_ERRNO_TSK_ID_INVALID) {
        UINT32 w;
        for (w = 0; w < 850; w++) {
            if (g_c1PingOk + g_c1PingFail >= C1_PING_N) {
                break;
            }
            LOS_TaskDelay(10);
        }
        (VOID)LOS_TaskDelete(g_c1Tid);
        g_c1Tid = LOS_ERRNO_TSK_ID_INVALID;
    }
    return LOS_NOK;
}

VOID ItMcsCombo401(VOID)
{
    TEST_ADD_CASE("ItMcsCombo401", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
