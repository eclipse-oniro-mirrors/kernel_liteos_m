/*
 * mcs_test_service.c — 从核测试调度服务（2026-11-09 新架构）
 *
 * 形态：标准 mica 服务（照 rpc_service 模式）——独立 rpmsg 端点
 * （rpmsg-mcs-test）+ receiver 线程回调收命令 + 服务线程（调度执行）。
 * 注册：mica_create_all_services 挂载（LOSCFG_TEST_MCS_SUITE 守卫）。
 *
 * 调度：命令查 g_mcs_modules 二级表，数组元素=用例包装函数
 * （ItMcsXxx：TEST_ADD_CASE->ICunitAddCase 边登记边执行，串口打
 * Passed/Failed）。RUN_MODULE/RUN_ALL 逐例循环，每例间查 STOP 标志。
 * 应答：受理 ACK；本轮完成 DONE（带 total/failed 汇总）。
 * 不与本文件之外的用例执行路径并发（调度模式下 mcs 用例不随 boot 跑批）。
 */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <openamp/rpmsg.h>
#include <mica/mica.h>
#include <mica/service.h>
#include "mcs_test_proto.h"
#include <string.h>
#include "mcs_test_proto.h"
#include "los_typedef.h"
#include "iCunit.h"

/* ── rpmsg 端点（receiver 线程回调分发）────────────────────── */
static struct rpmsg_endpoint g_test_ept;
static volatile int g_test_ept_ready;
static volatile int g_test_service_up;

/* ── 命令邮箱（rx 回调 -> 调度线程）───────────────────────── */
static volatile uint32_t g_cmd_seq;
static volatile uint8_t g_cmd;            /* 待执行命令（0=空） */
static char g_cmd_module[12];
static volatile uint32_t g_cmd_index;
static volatile int g_stop_req;           /* STOP：当前用例后停 */
static volatile int g_state;              /* MCS_TEST_ST_* */

/* iCunit 汇总计数（testsuites/src/iCunit.c 全局，extern 读本轮增量） */
extern iUINT32 iCunit_Case_Cnt;
extern iUINT32 iCunit_Case_FailedCnt;

/* 发应答帧（阻塞发到 host 端点） */
static int test_rsp_send(struct rpmsg_endpoint *ept, uint8_t rsp_cmd,
                         uint32_t seq, int32_t status,
                         uint32_t total, uint32_t failed)
{
    struct mcs_test_msg m;
    memset(&m, 0, sizeof(m));
    m.magic = MCS_TEST_MAGIC;
    m.cmd = (uint8_t)(rsp_cmd | MCS_TEST_RSP);
    m.ver = 1;
    m.seq = seq;
    m.status = status;
    m.total = total;
    m.failed = failed;
    return rpmsg_send(ept, &m, sizeof(m));
}

/* 执行一批用例（from..to 为模块内序号或跨表 NULL 起） */
static void sched_exec(struct mcs_test_msg *cmd)
{
    const mcs_module_t *mod = NULL;
    int i, begin = 0, end = -1;

    if (cmd->cmd == MCS_TEST_CMD_RUN_CASE || cmd->cmd == MCS_TEST_CMD_RUN_MODULE) {
        mod = mcs_find_module(cmd->module);
        if (mod == NULL) {
            test_rsp_send(&g_test_ept, MCS_TEST_RSP_ACK(cmd->cmd), cmd->seq, -ENOENT, 0, 0);
            return;
        }
        if (cmd->cmd == MCS_TEST_CMD_RUN_CASE) {
            if (cmd->index == 0 || cmd->index > (uint32_t)mod->count) {
                test_rsp_send(&g_test_ept, MCS_TEST_RSP_ACK(cmd->cmd), cmd->seq, -EINVAL, 0, 0);
                return;
            }
            begin = (int)(cmd->index - 1);
            end = begin;
        } else {
            begin = 0;
            end = mod->count - 1;
        }
    } else if (cmd->cmd == MCS_TEST_CMD_RUN_ALL) {
        begin = 0;
        end = -1; /* 跨模块循环，mod 每轮查表 */
    } else {
        return;
    }

    iUINT32 fail0 = iCunit_Case_FailedCnt;
    iUINT32 cnt0 = iCunit_Case_Cnt;
    uint32_t total = 0;

    g_state = MCS_TEST_ST_RUNNING;
    if (cmd->cmd == MCS_TEST_CMD_RUN_ALL) {
        /* 2026-11-14: 逐模块独立作用域（模拟 sequential dispatch 调用栈形态
         * ——单循环直跑实测后段 master TX 耗尽；让拍/kick/时长均无效，
         * 唯函数调用栈形态差异。变量 j 隔离每模块迭代域。） */
        int mi;
        for (mi = 0; mi < g_mcs_module_count && !g_stop_req; mi++) {
            const mcs_module_t *m = g_mcs_modules[mi];
            int j;
            for (j = 0; j < m->count && !g_stop_req; j++) {
                dprintf("[sched] #%u module=%s case=%d\n", total, m->name, j + 1);
                m->cases[j]();
                total++;
            }
            LOS_TaskDelay(20);
        }
    } else {
        for (i = begin; i <= end && !g_stop_req; i++) {
            dprintf("[sched] #%u module=%s case=%d\n", total, mod->name, i + 1);
            mod->cases[i]();
            total++;
        }
    }
    g_stop_req = 0;
    g_state = MCS_TEST_ST_IDLE;

    uint32_t failed = (uint32_t)(iCunit_Case_FailedCnt - fail0);
    (void)cnt0;
    test_rsp_send(&g_test_ept, MCS_TEST_RSP_ACK(cmd->cmd), cmd->seq, 0, total, failed);
}

/* ── receiver 线程回调（收命令帧）──────────────────────────── */
static int test_rx_callback(struct rpmsg_endpoint *ept, void *data,
                            size_t len, uint32_t src, void *priv)
{
    struct mcs_test_msg *m = data;

    (void)priv;
    if (len < sizeof(*m) || m->magic != MCS_TEST_MAGIC) {
        return 0;
    }
    if (m->cmd & MCS_TEST_RSP) {
        return 0; /* 从核只收命令，应答帧忽略 */
    }

    switch (m->cmd) {
    case MCS_TEST_CMD_PING:
        test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, 0, 0, 0);
        break;
    case MCS_TEST_CMD_STAT:
        test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, g_state, 0, 0);
        break;
    case MCS_TEST_CMD_STOP:
        if (g_state == MCS_TEST_ST_RUNNING) {
            g_stop_req = 1;
            g_state = MCS_TEST_ST_STOPPING;
        }
        test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, 0, 0, 0);
        break;
    case MCS_TEST_CMD_RUN_CASE:
    case MCS_TEST_CMD_RUN_MODULE:
    case MCS_TEST_CMD_RUN_ALL:
        if (g_state != MCS_TEST_ST_IDLE) {
            test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, -EBUSY, 0, 0);
            break;
        }
        g_cmd_seq = m->seq;
        g_cmd = m->cmd;
        memset((void *)g_cmd_module, 0, sizeof(g_cmd_module));
        memcpy((void *)g_cmd_module, m->module, sizeof(m->module));
        g_cmd_index = m->index;
        test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, 0, 0, 0);
        break;
    default:
        test_rsp_send(ept, MCS_TEST_RSP_ACK(m->cmd), m->seq, -ENOSYS, 0, 0);
        break;
    }
    return 0;
}

/* ── 服务线程（端点 + 调度循环）────────────────────────────── */
static void *test_service_thread(void *arg)
{
    struct rpmsg_device *rdev;
    int ret;

    (void)arg;
    rdev = mica_get_rpdev();
    if (rdev == NULL) {
        dprintf("mcs_test: rpmsg device not initialized\n");
        pthread_exit(NULL);
    }
    ret = rpmsg_create_ept(&g_test_ept, rdev, MCS_TEST_EPT_NAME,
                           RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                           test_rx_callback, NULL);
    if (ret != 0) {
        dprintf("mcs_test: create ept failed %d\n", ret);
        pthread_exit(NULL);
    }
    g_test_ept_ready = 1;
    g_test_service_up = 1;
    dprintf("[mcstest] service up on %s\n", MCS_TEST_EPT_NAME);

    /* 调度循环：等命令邮箱 -> 执行 -> （应答在 sched_exec 内发） */
    for (;;) {
        LOS_TaskDelay(500); /* 1s 让拍（20 tick 实测不够） */ /* 40ms 轮询邮箱（轻量；无需中断驱动） */
        if (g_cmd == 0) {
            continue;
        }
        struct mcs_test_msg cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.magic = MCS_TEST_MAGIC;
        cmd.cmd = g_cmd;
        cmd.seq = g_cmd_seq;
        cmd.index = g_cmd_index;
        memcpy(cmd.module, (const void *)g_cmd_module, sizeof(cmd.module));
        g_cmd = 0;
        sched_exec(&cmd);
    }
    return NULL;
}

int mcs_test_service_is_ready(void)
{
    return g_test_service_up;
}

int mcs_test_init_service(void)
{
    pthread_t thread;
    pthread_attr_t attr;
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        return ret;
    }
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (ret != 0) {
        pthread_attr_destroy(&attr);
        return ret;
    }
    ret = pthread_create(&thread, &attr, test_service_thread, NULL);
    pthread_attr_destroy(&attr);
    return ret;
}

/* ── 模块表（原 mcs_modules.c 并入，2026-11-10）── */
/*
 * mcs_modules.c — 从核调度模块表（纯引用：模块结构在各 <mod>_cases.c 自拼）
 * 加用例只改模块文件（extern+数组行）；新增模块目录才需在此加 2 行。
 */



#ifdef LOSCFG_MCS_RPC
extern const mcs_module_t mcs_module_tty;
extern const mcs_module_t mcs_module_umt;
extern const mcs_module_t mcs_module_rpc;
extern const mcs_module_t mcs_module_np;
extern const mcs_module_t mcs_module_pfs;
extern const mcs_module_t mcs_module_system;
extern const mcs_module_t mcs_module_combo;

const mcs_module_t *const g_mcs_modules[] = {
    &mcs_module_tty,
    &mcs_module_umt,
    &mcs_module_rpc,
    &mcs_module_np,
    &mcs_module_pfs,
    &mcs_module_system,
    &mcs_module_combo,
};
#else
/* 宏关时占位：find 恒 NULL，服务安全空转 */
static const mcs_module_t mcs_module_none = { NULL, NULL, 0 };
const mcs_module_t *const g_mcs_modules[] = { &mcs_module_none };
#endif

const int g_mcs_module_count = sizeof(g_mcs_modules) / sizeof(g_mcs_modules[0]);

const mcs_module_t *mcs_find_module(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < g_mcs_module_count; i++) {
        if (strcmp(g_mcs_modules[i]->name, name) == 0) {
            return g_mcs_modules[i];
        }
    }
    return NULL;
}
