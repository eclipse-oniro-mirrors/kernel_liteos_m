/*
 * host_slave.c — host 侧从核测试调度实现（2026-11-09 新架构）
 * 以 mcs_e2e.c 客户端骨架为底（NS match/bind + ept rx 回调 + 生命周期守卫：
 * start 失败的 client 不可 mica_stop（rsc_table NULL 崩溃），只能 remove）。
 */
#include "host_slave.h"
#include "host_passive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

static bool test_match(struct rpmsg_device *rdev, const char *name,
                       uint32_t addr, uint32_t dest, void *priv)
{
    (void)rdev; (void)addr; (void)dest; (void)priv;
    return name && strncmp(name, MCS_TEST_EPT_NAME, RPMSG_NAME_SIZE) == 0;
}

static int rx_count; /* 0: 期待 ACK；1: 期待 DONE（由 slave_cmd_wait_ack 重置） */

static int slave_rx_cb2(struct rpmsg_endpoint *ept, void *data,
                        size_t len, uint32_t src, void *priv)
{
    struct slave_ctx *s = priv;
    struct mcs_test_msg *m = data;

    (void)ept; (void)src;
    if (len < sizeof(*m) || m->magic != MCS_TEST_MAGIC) {
        return 0;
    }
    if ((m->cmd & MCS_TEST_RSP) == 0) {
        return 0;
    }
    if (rx_count == 0) {
        s->ack = *m;
        rx_count = 1;
        sem_post(&s->ack_ready);
    } else {
        s->done = *m;
        rx_count = 0;
        sem_post(&s->done_ready);
    }
    return 0;
}

/* bind 回调需要 ctx —— 包装：全局唯一 slave_ctx（测试线程互斥保证单实例） */
static struct slave_ctx *g_ctx;

static void test_bind2(struct rpmsg_device *rdev, const char *name,
                       uint32_t remote_addr, uint32_t remote_dest, void *priv)
{
    (void)name; (void)priv;
    struct slave_ctx *s = g_ctx;
    printf("    [mcstest] bind: peer=%u\n",
           (remote_addr != RPMSG_ADDR_ANY) ? remote_addr : remote_dest);
    if (rpmsg_create_ept(&s->ept, rdev, MCS_TEST_EPT_NAME, remote_dest,
                         remote_addr, slave_rx_cb2, NULL) == 0) {
        /* RK 定制 openAMP：create_ept 第 7 参是 ns_unbind_cb 而非 priv
         * （标准版是 priv）——priv 须挂到 ept 字段，rx 派发用 ept->priv */
        s->ept.priv = s;
        s->ept_ok = 1;
        sem_post(&s->ept_ready);
    }
}

static int ms_timeout_wait(sem_t *sem, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return sem_timedwait(sem, &ts);
}

struct slave_ctx *slave_create_start(const char *fw_path, int wait_sec)
{
    struct slave_ctx *s = calloc(1, sizeof(*s));
    struct mica_service svc;
    struct mica_client *c;
    int ret;

    if (!s) {
        return NULL;
    }
    sem_init(&s->ept_ready, 0, 0);
    sem_init(&s->ack_ready, 0, 0);
    sem_init(&s->done_ready, 0, 0);
    rx_count = 0;
    g_ctx = s;

    c = calloc(1, sizeof(*c));
    if (!c) {
        free(s);
        return NULL;
    }
    snprintf(c->path, sizeof(c->path), "%s", fw_path);
    c->ped = BARE_METAL;
    snprintf(c->ped_setup.name, sizeof(c->ped_setup.name), "%s", "mcs-host-srv");
    snprintf(c->ped_setup.cpu_str, sizeof(c->ped_setup.cpu_str), "%s", "3");
    s->cli = c;

    ret = mica_create(c);
    if (ret != 0) {
        printf("    [slave] mica_create=%d\n", ret);
        goto err_free;
    }
    ret = mica_start(c);
    if (ret != 0) {
        printf("    [slave] mica_start=%d\n", ret);
        goto err_remove; /* start 失败：只能 remove，不可 stop */
    }
    s->started = 1;

    memset(&svc, 0, sizeof(svc));
    snprintf(svc.name, sizeof(svc.name), "%s", "mcs-test-host-svc");
    svc.rpmsg_ns_match = test_match;
    svc.rpmsg_ns_bind_cb = test_bind2;
    ret = mica_register_service(c, &svc);
    if (ret != 0) {
        printf("    [slave] register_service=%d\n", ret);
        goto err_stop;
    }
    /* 被动服务端点（rpmsg-rpc/tty/umt）挂同一 client：从核 announce 后
     * passive_bind 建端点，np/pfs/echo/E2E 配合服务就位 */
    ret = host_passive_attach(c);
    if (ret != 0) {
        printf("    [slave] passive attach=%d（np/pfs 配合不可用）\n", ret);
        /* 不致命：调度端点已可用，被动域用例会 -EAGAIN */
    }
    if (!s->ept_ok && ms_timeout_wait(&s->ept_ready, wait_sec * 1000) != 0) {
        printf("    [slave] bind timeout (%ds)\n", wait_sec);
        goto err_stop;
    }
    return s;

err_stop:
    mica_stop(c);
    mica_remove(c);
    free(c);
    free(s);
    return NULL;
err_remove:
    mica_remove(c);
    free(c);
    free(s);
    return NULL;
err_free:
    free(c);
    free(s);
    return NULL;
}

void slave_stop_delete(struct slave_ctx *s)
{
    int ret;
    if (!s) {
        return;
    }
    host_passive_detach(); /* 先解被动端点：防残留帧派发/响应到将销毁的 rdev */
    if (s->started) {
        ret = mica_stop(s->cli);
        if (ret != 0) {
            printf("    [teardown] mica_stop=%d (CPU3 可能残留 RUNNING，需 reboot!)\n", ret);
        }
    }
    mica_remove(s->cli);
    free(s->cli);
    sem_destroy(&s->ept_ready);
    sem_destroy(&s->ack_ready);
    sem_destroy(&s->done_ready);
    free(s);
    if (g_ctx == s) {
        g_ctx = NULL;
    }
}

int slave_cmd_wait_ack(struct slave_ctx *s, struct mcs_test_msg *msg, int timeout_ms)
{
    int ret;
    pthread_mutex_t *txl = host_passive_tx_lock();
    msg->magic = MCS_TEST_MAGIC;
    msg->ver = 1;
    pthread_mutex_lock(txl); /* 与 passive worker 响应帧共用 vring，须互斥 */
    ret = rpmsg_send(&s->ept, msg, sizeof(*msg));
    pthread_mutex_unlock(txl);
    if (ret < 0) { /* rpmsg_send 成功返回字节数（>0），负数才是错误 */
        return ret;
    }
    if (ms_timeout_wait(&s->ack_ready, timeout_ms) != 0) {
        return -ETIMEDOUT;
    }
    *msg = s->ack;
    return 0;
}

int slave_wait_done(struct slave_ctx *s, struct mcs_test_msg *out, int timeout_ms)
{
    if (timeout_ms < 0) {
        while (sem_wait(&s->done_ready) == -1 && errno == EINTR) {
        }
    } else if (ms_timeout_wait(&s->done_ready, timeout_ms) != 0) {
        return -ETIMEDOUT;
    }
    *out = s->done;
    return 0;
}

int slave_send_stop(struct slave_ctx *s)
{
    struct mcs_test_msg m;
    pthread_mutex_t *txl = host_passive_tx_lock();
    int ret;
    memset(&m, 0, sizeof(m));
    m.magic = MCS_TEST_MAGIC;
    m.ver = 1;
    m.cmd = MCS_TEST_CMD_STOP;
    pthread_mutex_lock(txl);
    ret = rpmsg_send(&s->ept, &m, sizeof(m));
    pthread_mutex_unlock(txl);
    return ret;
}
