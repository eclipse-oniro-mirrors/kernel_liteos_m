/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * host_passive.c — mcs host 被动服务（2026-11-10，自 mcs_test_server.c +
 * netproxy_demo.c 合流移植进新架构 host_server）
 *
 * 服务面（从核用例期望的主核配合，rpmsg-rpc 端点）：
 *   np  socket 代理：RPC_FUNC_SOCKET/BIND/LISTEN/ACCEPT/CONNECT/SEND/SENDTO/
 *                   RECV/RECVFROM/CLOSE/SETSOCKOPT/GETSOCKOPT/GETSOCKNAME/
 *                   GETPEERNAME/SHUTDOWN/SELECT/SYSTEM（0x100-0x130）
 *   pfs 代理文件系统：RPC_FUNC_PF_*（0x300-0x310）——open/close/read/write/
 *                   lseek/fcntl/unlink/stat/fstat/rename/remove/mkdir/rmdir/
 *                   chdir/getcwd/access/getdents
 *   echo 本地回显：UDP :7000 / TCP :7001（从核 NpSrvEcho01 直打）
 *   connect-back ：127.0.0.1:7005 周期连接（从核 np server 相位配套）
 *   E2E 配合：E2E_TRIGGER（反向 call 从核 0x320）/ E2E_UMT_DOWN（umt 按需
 *             下行）/ E2E_TTY_EXEC（向 rpmsg-tty 注入长命令）
 *   辅端点：rpmsg-tty（打印从核 shell 输出）/ rpmsg-umt（copy-mem init +
 *           数据面 rx 计数）
 *
 * 生命周期：常驻线程随 host_server 进程；rpmsg 端点随测试线程的 mica_client
 * 每轮 attach/detach（见 host_passive.h）。tx 全局锁与 host_slave 共用。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <dirent.h>

#include <openamp/rpmsg.h>
#include <mica/mica.h>
#include <user_msg/user_msg.h>

#include "rpc_proto.h"
#include "host_passive.h"

#define RPC_EPT_NAME        "rpmsg-rpc"
#define TTY_EPT_NAME        "rpmsg-tty"
#define UMT_EPT_NAME        "rpmsg-umt"
#define ECHO_UDP_PORT       7000   /* 板内回环 echo（np_202 打 127.0.0.1——代理路径用例）；
                                     * np echo 族（IT_NP_ECHO_*）目标在对端 PC，不在此 */
#define ECHO_TCP_PORT       7001
#define ECHO_BUF_MAX        2048
#define CB_PORT             7005
#define REQQ_DEPTH          256  /* 2026-11-14: 64→256——sched all 后段 combo 并发把队列打满丢帧（实测 14 drop）*/
#define NP_CONNECT_TIMEOUT_MS 10000
#define NP_ACCEPT_TIMEOUT_MS  10000

/* ── 端点（静态，每轮 bind 重建 / detach 销毁）────────────── */
static struct rpmsg_endpoint g_rpc_ept;
static struct rpmsg_endpoint g_tty_ept;
static struct rpmsg_endpoint g_umt_ept;
static uint32_t g_peer_addr;
static uint32_t g_tty_peer_addr;
static uint32_t g_umt_peer_addr;
static volatile sig_atomic_t g_rpc_ok;
static volatile sig_atomic_t g_tty_ok;
static volatile sig_atomic_t g_umt_ok;

/* tx serialization: openamp 无内部锁——worker 响应、host_slave 的 RUN/STOP
 * 帧、E2E 反向 call 都落在同一 vring，必须互斥（老 server 实证：并发
 * rpmsg_sendto 导致主核 virtqueue 损坏 SIGSEGV） */
static pthread_mutex_t g_tx_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── E2E 反向 call 等待（唯一的主核 client 角色）──────────── */
static sem_t g_back_sem;
static volatile uint32_t g_back_id;
static volatile int g_back_done;
static uint8_t g_back_payload[448];
static volatile int g_back_payload_len;

/* ── UMT copy-mem 上下文（E2E_UMT_DOWN 数据面写侧）────────── */
static struct core_msg_mem_info g_umt;
static unsigned g_umt_rx_cnt;

/* ── 请求队列（rx cb → worker）───────────────────────────── */
struct reqq_slot {
    uint32_t len;
    uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
    bool used;
};
static struct reqq_slot g_reqq[REQQ_DEPTH];
static pthread_mutex_t g_reqq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_reqq_cond = PTHREAD_COND_INITIALIZER;
static uint32_t g_reqq_dropped;

/* ── 常驻线程启动（幂等）─────────────────────────────────── */
static volatile int g_started;

pthread_mutex_t *host_passive_tx_lock(void)
{
    return &g_tx_lock;
}

/* ---------- NS match/bind ---------- */
static int passive_rpc_rx(struct rpmsg_endpoint *ept, void *data,
                          size_t len, uint32_t src, void *priv);

static bool passive_match(struct rpmsg_device *rdev, const char *name,
                          uint32_t addr, uint32_t dest, void *priv)
{
    (void)rdev; (void)addr; (void)dest; (void)priv;
    if (!name)
        return false;
    return strncmp(name, RPC_EPT_NAME, RPMSG_NAME_SIZE) == 0 ||
           strncmp(name, TTY_EPT_NAME, RPMSG_NAME_SIZE) == 0 ||
           strncmp(name, UMT_EPT_NAME, RPMSG_NAME_SIZE) == 0;
}

/* tty rx: slave shell output + app tick-alive lines */
static int tty_rx_cb(struct rpmsg_endpoint *ept, void *data,
                     size_t len, uint32_t src, void *priv)
{
    char line[512];
    size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;

    (void)ept; (void)src; (void)priv;
    memcpy(line, data, n);
    line[n] = '\0';
    printf("[tty] %s\n", line);
    return 0;
}

static int umt_rx_cb(struct rpmsg_endpoint *ept, void *data,
                     size_t len, uint32_t src, void *priv)
{
    struct {
        uint64_t phy_addr;
        uint32_t data_len;
    } *m = data;

    (void)ept; (void)src; (void)priv;
    g_umt_rx_cnt++;
    if (len >= sizeof(*m))
        printf("[umt] rx #%u: phy=0x%llx len=%u\n", g_umt_rx_cnt,
               (unsigned long long)m->phy_addr, m->data_len);
    return 0;
}

/* /dev/mcs IOC_GET_COPY_MSG_MEM + /dev/mem mmap（含 region 错配 fallback） */
static int umt_copy_mem_init(void)
{
    int fd, ped = 0 /* MCS_KM_PED_BAREMETAL */;
    unsigned long page = sysconf(_SC_PAGE_SIZE);
    unsigned long abase;
    size_t asize;

    fd = open("/dev/mcs", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[np] umt: open /dev/mcs failed\n");
        return -1;
    }
    if (ioctl(fd, _IOW('A', 5, int), &ped) != 0) {
        fprintf(stderr, "[np] umt: IOC_SET_PED_TYPE failed\n");
        close(fd);
        return -1;
    }
    g_umt.instance_id = 0;
    if (ioctl(fd, _IOWR('A', 4, struct core_msg_mem_info), &g_umt) != 0) {
        /* 已知配套错配（问题记录 20261023_15xxxx）: dts patch 的 region[0]
         * 仅 1MB(vring)，mcs_km 按 baremetal_mem[0]+13M 算 copy_mem 越界
         * EINVAL。fallback: 从核保留区 region[1] 内安全段 0x75000000。 */
        fprintf(stderr, "[np] umt: IOC_GET_COPY_MSG_MEM failed(errno=%d), "
                        "fallback phy=0x75000000 size=2M\n", errno);
        g_umt.phy_addr = 0x75000000;
        g_umt.size = 0x200000;
    }
    /* mmap 经 /dev/mcs（copy-msg 区对 /dev/mem 是 STRICT_DEVMEM 保留区，
     * 直写 SIGBUS——实测 2026-11-14 尸检 sig=7；mcs_km.mmap 对该区
     * 有 remap_pfn_range 通路）。同一 fd：IOC 探区 + mmap。 */
    if (g_umt.phy_addr == 0) {
        close(fd);
        return -1;
    }
    abase = g_umt.phy_addr & ~(page - 1);
    asize = ((g_umt.phy_addr - abase) + g_umt.size + page - 1) & ~(page - 1);
    g_umt.vir_addr = mmap(NULL, asize, PROT_READ | PROT_WRITE, MAP_SHARED,
                          fd, abase);
    close(fd);
    if (g_umt.vir_addr == MAP_FAILED) {
        g_umt.vir_addr = NULL;
        fprintf(stderr, "[np] umt: mmap via /dev/mcs failed\n");
        return -1;
    }
    g_umt.vir_addr += (g_umt.phy_addr - abase);
    printf("[np] umt: copy-mem phy=0x%llx size=0x%x\n",
           (unsigned long long)g_umt.phy_addr, g_umt.size);
    return 0;
}

static void passive_bind(struct rpmsg_device *rdev, const char *name,
                         uint32_t remote_addr, uint32_t remote_dest, void *priv)
{
    int ret;
    uint32_t peer = (remote_addr != RPMSG_ADDR_ANY) ? remote_addr : remote_dest;

    (void)priv;

    if (strncmp(name, TTY_EPT_NAME, RPMSG_NAME_SIZE) == 0) {
        g_tty_peer_addr = peer;
        printf("[np] NS bind: %s peer=%u\n", TTY_EPT_NAME, peer);
        ret = rpmsg_create_ept(&g_tty_ept, rdev, TTY_EPT_NAME,
                               remote_dest, remote_addr, tty_rx_cb, NULL);
        if (ret) {
            fprintf(stderr, "[np] tty create_ept failed: %d\n", ret);
            return;
        }
        g_tty_ok = 1;
        /* 激活探测（同 rpc/umt 模式）：从核 tty ept 以 dest=ANY 创建，
         * is_rpmsg_ept_ready 要求先收主核一帧学习 dest_addr——不发探测则
         * 从核 tty 永不就绪（Combo401 实测：单域 tty 偶过是调度时序运气） */
        {
            static const char probe[] = "\r\n";
            pthread_mutex_lock(&g_tx_lock);
            (void)rpmsg_sendto(&g_tty_ept, probe, sizeof(probe) - 1, peer);
            pthread_mutex_unlock(&g_tx_lock);
            printf("[np] tty activate-probe sent\n");
        }
        return;
    }

    if (strncmp(name, UMT_EPT_NAME, RPMSG_NAME_SIZE) == 0) {
        struct {
            uint64_t phy_addr;
            uint32_t data_len;
        } probe;

        g_umt_peer_addr = peer;
        printf("[np] NS bind: %s peer=%u\n", UMT_EPT_NAME, peer);
        ret = rpmsg_create_ept(&g_umt_ept, rdev, UMT_EPT_NAME,
                               remote_dest, remote_addr, umt_rx_cb, NULL);
        if (ret) {
            fprintf(stderr, "[np] umt create_ept failed: %d\n", ret);
            return;
        }
        /* 从核 is_rpmsg_ept_ready 要求 dest_addr != ANY（首条消息设置）；
         * 首帧 phy_addr 被从核记录为 send_buffer 基址（+1MB），必须是合法
         * copy-mem 物理地址（IOC_GET_COPY_MSG_MEM 协议）。 */
        if (g_umt.phy_addr == 0)
            umt_copy_mem_init();
        if (g_umt.phy_addr) {
            probe.phy_addr = g_umt.phy_addr;
            probe.data_len = 0;
            ret = rpmsg_sendto(&g_umt_ept, &probe, sizeof(probe), peer);
            printf("[np] umt addr-probe sent (phy=0x%llx): %d\n",
                   (unsigned long long)g_umt.phy_addr, ret);
            g_umt_ok = 1;
        }
        return;
    }

    g_peer_addr = peer;
    printf("[np] NS bind: %s peer=%u\n", RPC_EPT_NAME, peer);
    ret = rpmsg_create_ept(&g_rpc_ept, rdev, RPC_EPT_NAME,
                           remote_dest, remote_addr, passive_rpc_rx, NULL);
    if (ret) {
        fprintf(stderr, "[np] rpc create_ept failed: %d\n", ret);
        return;
    }
    g_rpc_ok = 1;
    /* 激活探测（同 umt addr-probe 模式）：从核 rpc ept 以 dest=ANY 创建，
     * is_rpmsg_ept_ready 要求先收到主核一帧学习 dest_addr，之后
     * mica_rpc_call 才放行（老 server 靠 liveness ping 承担此步） */
    {
        struct rpc_hdr probe;
        int pret;

        memset(&probe, 0, sizeof(probe));
        probe.func_id = RPC_FUNC_PING;
        probe.req_id = 0;
        probe.flags = RPC_F_REQ;
        probe.payload_len = 0;
        pthread_mutex_lock(&g_tx_lock);
        pret = rpmsg_sendto(&g_rpc_ept, &probe, sizeof(probe), peer);
        pthread_mutex_unlock(&g_tx_lock);
        printf("[np] rpc activate-probe sent: %d\n", pret);
    }
}

/* ---------- rpc rx: RESP -> E2E waiter, REQ -> queue ---------- */
static int passive_rpc_rx(struct rpmsg_endpoint *ept, void *data,
                          size_t len, uint32_t src, void *priv)
{
    const struct rpc_hdr *hdr = data;

    (void)ept; (void)src; (void)priv;
    if (len < sizeof(*hdr))
        return 0;

    if (hdr->flags & RPC_F_RESP) {
        uint32_t plen = (len > sizeof(*hdr)) ? len - sizeof(*hdr) : 0;

        if (hdr->func_id == RPC_FUNC_E2E_HANDLER &&
            hdr->req_id == g_back_id && !g_back_done) {
            if (plen > sizeof(g_back_payload))
                plen = sizeof(g_back_payload);
            if (plen > 0)
                memcpy(g_back_payload, hdr + 1, plen);
            g_back_payload_len = plen;
            g_back_done = 1;
            sem_post(&g_back_sem);
        }
        return 0;
    }

    if (hdr->flags & RPC_F_REQ) {
        /* PING 快路径：rx 回调直答（纯 echo 零阻塞）——不入 worker 队列，
         * 防 RECV/CONNECT 慢操作堵队时 PING 被连坐丢弃（sched all 实测坑） */
        if (hdr->func_id == RPC_FUNC_PING) {
            uint8_t rmsg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
            struct rpc_hdr *rh = (struct rpc_hdr *)rmsg;
            uint32_t plen = (len > sizeof(*hdr)) ? len - sizeof(*hdr) : 0;
            if (plen > RPC_MAX_PAYLOAD) plen = RPC_MAX_PAYLOAD;
            rh->func_id = hdr->func_id;
            rh->req_id = hdr->req_id;
            rh->flags = RPC_F_RESP;
            rh->payload_len = (uint16_t)plen;
            rh->ret = 0;
            rh->err = 0;
            if (plen > 0) memcpy(rh + 1, hdr + 1, plen);
            pthread_mutex_lock(&g_tx_lock);
            (void)rpmsg_sendto(&g_rpc_ept, rmsg, sizeof(*rh) + plen, g_peer_addr);
            pthread_mutex_unlock(&g_tx_lock);
            return 0;
        }
        pthread_mutex_lock(&g_reqq_lock);
        for (int i = 0; i < REQQ_DEPTH; i++) {
            if (!g_reqq[i].used) {
                size_t n = len;

                if (n > sizeof(g_reqq[i].msg))
                    n = sizeof(g_reqq[i].msg);
                memcpy(g_reqq[i].msg, data, n);
                g_reqq[i].len = (uint32_t)n;
                g_reqq[i].used = true;
                pthread_cond_signal(&g_reqq_cond);
                pthread_mutex_unlock(&g_reqq_lock);
                return 0;
            }
        }
        g_reqq_dropped++;
        pthread_mutex_unlock(&g_reqq_lock);
        fprintf(stderr, "[np] req queue full, dropped (%u total)\n",
                g_reqq_dropped);
    }
    return 0;
}

/* ---------- helpers ---------- */
static const char *path_from_payload(const uint8_t *p, uint32_t plen,
                                     char *buf, uint32_t bufsz)
{
    uint32_t i;

    if (plen == 0 || plen >= bufsz)
        return NULL;
    for (i = 0; i < plen; i++) {
        if (p[i] == '\0') {
            memcpy(buf, p, i + 1);
            return buf;
        }
    }
    return NULL;
}

static int send_resp_frag(uint32_t func_id, uint32_t req_id,
                          int32_t ret, int32_t err,
                          const uint8_t *payload, uint32_t total)
{
    uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
    struct rpc_hdr *h = (struct rpc_hdr *)msg;
    uint32_t off = 0;

    if (!g_rpc_ok)
        return -ENODEV; /* 端点已随本轮 client 销毁：丢弃 */
    do {
        uint32_t chunk = total - off;

        if (chunk > RPC_MAX_PAYLOAD)
            chunk = RPC_MAX_PAYLOAD;
        h->func_id = func_id;
        h->req_id = req_id;
        h->flags = RPC_F_RESP | ((off + chunk < total) ? RPC_F_MORE : 0);
        h->payload_len = (uint16_t)chunk;
        h->ret = (off == 0) ? ret : 0;
        h->err = (off == 0) ? err : 0;
        if (chunk)
            memcpy(h + 1, payload + off, chunk);
        pthread_mutex_lock(&g_tx_lock);
        if (rpmsg_sendto(&g_rpc_ept, msg, sizeof(*h) + chunk,
                         g_peer_addr) < 0) {
            pthread_mutex_unlock(&g_tx_lock);
            fprintf(stderr, "[np] send_resp_frag FAIL func=0x%x id=%u (rpc ept ok=%d peer=%u)\n",
                    func_id, req_id, g_rpc_ok, g_peer_addr);
            return -EIO;
        }
        pthread_mutex_unlock(&g_tx_lock);
        off += chunk;
    } while (off < total);
    return 0;
}

/* E2E umt 按需下行: rpc 0x322 触发发一帧（周期发送会因从核 passive 模式
 * hold rx buffer 不归还而耗尽 vring 池——库级缺陷，按需规避） */
static void umt_send_one_frame(void)
{
    char msg[32];
    struct {
        uint64_t phy_addr;
        uint32_t data_len;
    } frame;
    static unsigned seq;
    int n = snprintf(msg, sizeof(msg), "E2E-UMT-DOWN-%u", seq++);

    if (n <= 0 || !g_umt.phy_addr || !g_umt.vir_addr)
        return;
    memcpy(g_umt.vir_addr, msg, n);
    frame.phy_addr = g_umt.phy_addr;
    frame.data_len = n;
    {
        int sret;

        pthread_mutex_lock(&g_tx_lock);
        sret = rpmsg_sendto(&g_umt_ept, &frame, sizeof(frame), g_umt_peer_addr);
        pthread_mutex_unlock(&g_tx_lock);
        printf("[ts][umt] on-demand down #%u: %d\n", seq, sret);
    }
}

/* ---------- worker: serve slave proxy requests ---------- */
static void handle_req(struct reqq_slot *slot)
{
    struct rpc_hdr *hdr = (struct rpc_hdr *)slot->msg;
    uint8_t *payload = slot->msg + sizeof(*hdr);
    uint32_t plen = slot->len - sizeof(*hdr);
    static uint8_t bigbuf[4096]; /* 大 PF_READ 结果缓冲（103500 修复：>2048 读不再截断） */
    uint8_t *resp = bigbuf;      /* 缺省复用大缓冲（与旧 2048 语义兼容） */
    uint32_t resp_cap = sizeof(bigbuf);
    int32_t ret = 0, err = 0;
    uint32_t resp_len = 0;

    printf("[np] req func=0x%x id=%u plen=%u\n", hdr->func_id, hdr->req_id, plen);

    switch (hdr->func_id) {
    case RPC_FUNC_PING: { /* echo back */
        resp_len = (plen < resp_cap) ? plen : resp_cap;
        memcpy(resp, payload, resp_len);
        break;
    }
    case RPC_FUNC_GET_TICK: { /* slave Rpc005: 8-byte tick */
        struct timespec ts;
        uint64_t t;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        t = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000;
        memcpy(resp, &t, sizeof(t));
        resp_len = sizeof(t);
        break;
    }
    case RPC_FUNC_LOG: { /* slave diagnostics: print the line */
        if (plen > 0 && plen <= RPC_MAX_PAYLOAD) {
            char line[RPC_MAX_PAYLOAD + 1];

            memcpy(line, payload, plen);
            line[plen] = '\0';
            printf("[np][slave] %s\n", line);
        }
        break;
    }
    case RPC_FUNC_E2E_TRIGGER: { /* slave Rpc008/009: 反向 call 从核 0x320 */
        struct e2e_trig_resp {
            uint32_t handler_called;
            uint32_t echo_len;
            char echo[48];
        } *r = (struct e2e_trig_resp *)resp;
        struct {
            struct rpc_hdr h;
            uint8_t p[RPC_MAX_PAYLOAD];
        } frame;
        struct timespec wt;
        uint32_t n = (plen < RPC_MAX_PAYLOAD) ? plen : RPC_MAX_PAYLOAD;
        uint32_t elen;
        int sret;

        memset(&frame, 0, sizeof(frame));
        frame.h.func_id = RPC_FUNC_E2E_HANDLER;
        frame.h.req_id = ++g_back_id;
        frame.h.flags = RPC_F_REQ;
        frame.h.payload_len = n;
        if (n > 0)
            memcpy(frame.p, payload, n);
        g_back_done = 0;
        g_back_payload_len = 0;

        pthread_mutex_lock(&g_tx_lock);
        sret = rpmsg_sendto(&g_rpc_ept, &frame, sizeof(frame.h) + n, g_peer_addr);
        pthread_mutex_unlock(&g_tx_lock);
        if (sret < 0) {
            ret = -1; err = -EIO;
            break;
        }
        clock_gettime(CLOCK_REALTIME, &wt);
        wt.tv_sec += 4;
        if (sem_timedwait(&g_back_sem, &wt) != 0 || !g_back_done) {
            printf("[ts][e2e] back-call timeout (id=%u)\n", g_back_id);
            ret = -1; err = -ETIMEDOUT;
            break;
        }
        elen = (uint32_t)g_back_payload_len;
        if (elen > sizeof(r->echo))
            elen = sizeof(r->echo);
        r->handler_called = 1;
        r->echo_len = elen;
        if (elen > 0)
            memcpy(r->echo, g_back_payload, elen);
        resp_len = sizeof(*r);
        printf("[ts][e2e] back-call ok (id=%u echo=%uB)\n", g_back_id, elen);
        break;
    }
    case RPC_FUNC_E2E_UMT_DOWN: { /* slave Svc013: 按需一帧 umt 下行 */
        umt_send_one_frame();
        break;
    }
    case RPC_FUNC_E2E_TTY_EXEC: { /* slave Combo04 (D14): 注入长 tty 命令 */
        static const char *cmd = "help\r";
        int sret;

        pthread_mutex_lock(&g_tx_lock);
        if (g_tty_ept.rdev != NULL) {
            sret = rpmsg_sendto(&g_tty_ept, cmd, strlen(cmd), g_tty_peer_addr);
        } else {
            sret = -ENODEV;
        }
        pthread_mutex_unlock(&g_tx_lock);
        if (sret < 0) {
            printf("[ts][combo] tty inject failed: %d\n", sret);
            ret = -1; err = -EIO;
        } else {
            printf("[ts][combo] tty injected 'help' (len=%zu)\n", strlen(cmd));
        }
        break;
    }

    /* ---- np socket proxy ---- */
    case RPC_FUNC_SOCKET: {
        struct rpc_socket_args *a = (struct rpc_socket_args *)payload;
        struct rpc_socket_resp *r = (struct rpc_socket_resp *)resp;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = socket(a->domain, a->type, a->protocol);
        if (ret < 0) {
            err = -errno;
        } else {
            r->fd = ret;
            resp_len = sizeof(*r);
            ret = 0;
        }
        break;
    }
    case RPC_FUNC_BIND: {
        struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)payload;

        if (plen < sizeof(*a) || plen < sizeof(*a) + a->addrlen) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = bind(a->fd, (const struct sockaddr *)(payload + sizeof(*a)),
                   a->addrlen);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_LISTEN: {
        struct rpc_listen_args *a = (struct rpc_listen_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = listen(a->fd, a->backlog);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_ACCEPT: {
        struct rpc_accept_args *a = (struct rpc_accept_args *)payload;
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        struct rpc_accept_resp *r = (struct rpc_accept_resp *)resp;
        struct pollfd pfd;
        int fl, nfd, pret;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        fl = fcntl(a->fd, F_GETFL, 0);
        if (fl >= 0)
            (void)fcntl(a->fd, F_SETFL, fl | O_NONBLOCK);
        pfd.fd = a->fd;
        pfd.events = POLLIN;
        pret = poll(&pfd, 1, NP_ACCEPT_TIMEOUT_MS);
        if (pret == 0) {
            ret = -1; err = -EAGAIN;
        } else if (pret < 0) {
            ret = -1; err = -errno;
        } else {
            nfd = accept(a->fd, (struct sockaddr *)&ss, &slen);
            if (nfd < 0) {
                ret = -1; err = -errno;
            } else {
                uint32_t alen = (slen > 128) ? 128 : slen;

                r->fd = nfd;
                r->addrlen = alen;
                memcpy(resp + sizeof(*r), &ss, alen);
                resp_len = sizeof(*r) + alen;
                ret = 0;
                printf("[np] accept fd=%d -> %d\n", a->fd, nfd);
            }
        }
        if (fl >= 0)
            (void)fcntl(a->fd, F_SETFL, fl);
        break;
    }
    case RPC_FUNC_CONNECT: {
        struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)payload;
        struct pollfd pfd;
        socklen_t slen;
        int fl, soerr;

        if (plen < sizeof(*a) || plen < sizeof(*a) + a->addrlen) {
            ret = -1; err = -EINVAL;
            break;
        }
        fl = fcntl(a->fd, F_GETFL, 0);
        if (fl >= 0)
            (void)fcntl(a->fd, F_SETFL, fl | O_NONBLOCK);
        ret = connect(a->fd, (const struct sockaddr *)(payload + sizeof(*a)),
                      a->addrlen);
        if (ret < 0 && errno == EINPROGRESS) {
            pfd.fd = a->fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            ret = poll(&pfd, 1, NP_CONNECT_TIMEOUT_MS);
            if (ret == 0) {
                ret = -1;
                err = -ETIMEDOUT;
            } else if (ret > 0) {
                soerr = 0;
                slen = sizeof(soerr);
                (void)getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
                if (soerr == 0) {
                    ret = 0;
                } else {
                    ret = -1;
                    err = -soerr; /* e.g. ECONNREFUSED */
                }
            } else {
                ret = -1;
                err = -errno;
            }
        } else if (ret < 0) {
            err = -errno;
        }
        if (fl >= 0)
            (void)fcntl(a->fd, F_SETFL, fl);
        break;
    }
    case RPC_FUNC_SEND: {
        struct rpc_send_args *a = (struct rpc_send_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = send(a->fd, payload + sizeof(*a), plen - sizeof(*a),
                   a->flags & 0xffff);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_SENDTO: {
        struct rpc_sendto_args *a = (struct rpc_sendto_args *)payload;
        uint32_t hdrsz = sizeof(*a) + a->addrlen;

        if (plen < hdrsz) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = sendto(a->fd, payload + hdrsz, plen - hdrsz,
                     a->flags & 0xffff,
                     (const struct sockaddr *)(payload + sizeof(*a)),
                     a->addrlen);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_RECV: {
        struct rpc_recv_args *a = (struct rpc_recv_args *)payload;
        struct pollfd pfd;
        int pret;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        pfd.fd = a->fd;
        pfd.events = POLLIN;
        pret = poll(&pfd, 1, a->timeout_ms);
        if (pret == 0) {
            ret = -1; err = -EAGAIN;
            break;
        }
        if (pret < 0) {
            ret = -1; err = -errno;
            break;
        }
        ret = recv(a->fd, resp, resp_cap, a->flags & 0xffff);
        if (ret < 0) {
            ret = -1; err = -errno;
        } else {
            resp_len = (uint32_t)ret;
            printf("[np] recv fd=%d: %u bytes\n", a->fd, resp_len);
        }
        break;
    }
    case RPC_FUNC_RECVFROM: {
        struct rpc_recvfrom_args *a = (struct rpc_recvfrom_args *)payload;
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        struct pollfd pfd;
        uint8_t data[RPC_MAX_PAYLOAD - 16];
        int pret;
        ssize_t n;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        pfd.fd = a->fd;
        pfd.events = POLLIN;
        pret = poll(&pfd, 1, a->timeout_ms);
        if (pret == 0) {
            ret = -1; err = -EAGAIN;
            break;
        }
        if (pret < 0) {
            ret = -1; err = -errno;
            break;
        }
        n = recvfrom(a->fd, data, sizeof(data), a->flags & 0xffff,
                     (struct sockaddr *)&ss, &slen);
        if (n < 0) {
            ret = -1; err = -errno;
            break;
        }
        { /* resp payload: [addrlen u32][sockaddr][data] */
            uint32_t alen = (slen > 128) ? 128 : slen;

            *(uint32_t *)resp = alen;
            memcpy(resp + 4, &ss, alen);
            memcpy(resp + 4 + alen, data, n);
            resp_len = 4 + alen + (uint32_t)n;
            ret = (int32_t)n;
            printf("[np] recvfrom fd=%d: %zd bytes\n", a->fd, n);
        }
        break;
    }
    case RPC_FUNC_CLOSE: {
        struct rpc_close_args *a = (struct rpc_close_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = close(a->fd);
        if (ret < 0)
            err = -errno;
        else
            printf("[np] close fd=%d\n", a->fd);
        break;
    }
    case RPC_FUNC_SETSOCKOPT: {
        struct rpc_setsockopt_args *a = (struct rpc_setsockopt_args *)payload;

        if (plen < sizeof(*a) || plen < sizeof(*a) + a->optlen) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = setsockopt(a->fd, a->level, a->optname,
                         payload + sizeof(*a), a->optlen);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_GETSOCKOPT: {
        struct rpc_getsockopt_args *a = (struct rpc_getsockopt_args *)payload;
        socklen_t ol;
        uint32_t cap;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        cap = a->optlen;
        if (cap > resp_cap - 4)
            cap = resp_cap - 4;
        ol = cap;
        ret = getsockopt(a->fd, a->level, a->optname, resp + 4, &ol);
        if (ret < 0) {
            err = -errno;
        } else {
            *(uint32_t *)resp = (uint32_t)ol;
            resp_len = 4 + (uint32_t)ol;
        }
        break;
    }
    case RPC_FUNC_GETSOCKNAME:
    case RPC_FUNC_GETPEERNAME: {
        struct rpc_getname_args *a = (struct rpc_getname_args *)payload;
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        if (hdr->func_id == RPC_FUNC_GETSOCKNAME)
            ret = getsockname(a->fd, (struct sockaddr *)&ss, &slen);
        else
            ret = getpeername(a->fd, (struct sockaddr *)&ss, &slen);
        if (ret < 0) {
            err = -errno;
        } else {
            uint32_t alen = (slen > 128) ? 128 : slen;

            *(uint32_t *)resp = alen;
            memcpy(resp + 4, &ss, alen);
            resp_len = 4 + alen;
        }
        break;
    }
    case RPC_FUNC_SHUTDOWN: {
        struct rpc_shutdown_args *a = (struct rpc_shutdown_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = shutdown(a->fd, a->how);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_SELECT: {
        struct rpc_select_args *a = (struct rpc_select_args *)payload;
        fd_set r, w, e;
        struct timeval tv, *tvp;

        if (plen < sizeof(*a) || a->nfds < 0 || a->nfds > FD_SETSIZE) {
            ret = -1; err = -EINVAL;
            break;
        }
        memset(&r, 0, sizeof(r));
        memset(&w, 0, sizeof(w));
        memset(&e, 0, sizeof(e));
        memcpy(&r, a->rdset, RPC_SELECT_SET_BYTES);
        memcpy(&w, a->wrset, RPC_SELECT_SET_BYTES);
        memcpy(&e, a->exset, RPC_SELECT_SET_BYTES);
        if (a->timeout_ms < 0) {
            tvp = NULL;
        } else {
            tv.tv_sec = a->timeout_ms / 1000;
            tv.tv_usec = (a->timeout_ms % 1000) * 1000;
            tvp = &tv;
        }
        ret = select(a->nfds, &r, &w, &e, tvp);
        if (ret < 0) {
            err = -errno;
        } else {
            memcpy(resp, &r, RPC_SELECT_SET_BYTES);
            memcpy(resp + RPC_SELECT_SET_BYTES, &w, RPC_SELECT_SET_BYTES);
            memcpy(resp + 2 * RPC_SELECT_SET_BYTES, &e, RPC_SELECT_SET_BYTES);
            resp_len = 3 * RPC_SELECT_SET_BYTES;
        }
        break;
    }
    case RPC_FUNC_SYSTEM: {
        char cmd[RPC_MAX_PAYLOAD + 1];
        const char *s = path_from_payload(payload, plen, cmd, sizeof(cmd));

        if (s == NULL) {
            ret = -1; err = -EINVAL;
            break;
        }
        printf("[np] system: %s\n", s);
        fflush(stdout);
        ret = system(s);
        break;
    }

    /* ---- pfs proxy filesystem ---- */
    case RPC_FUNC_PF_OPEN: {
        struct rpc_pf_open_args *a = (struct rpc_pf_open_args *)payload;
        char path[RPC_MAX_PAYLOAD + 1];

        if (plen < sizeof(*a) ||
            path_from_payload(payload + sizeof(*a), plen - sizeof(*a),
                              path, sizeof(path)) == NULL) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = open(path, a->flags, a->mode);
        if (ret < 0)
            err = -errno;
        else
            printf("[np] pfs open %s -> fd=%d\n", path, ret);
        break;
    }
    case RPC_FUNC_PF_CLOSE: {
        struct rpc_pf_close_args *a = (struct rpc_pf_close_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = close(a->fd);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_READ: {
        struct rpc_pf_read_args *a = (struct rpc_pf_read_args *)payload;
        uint32_t want;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        want = a->count;
        if (want > resp_cap)
            want = resp_cap;
        ret = (int32_t)read(a->fd, resp, want);
        if (ret < 0) {
            err = -errno;
        } else {
            resp_len = (uint32_t)ret;
        }
        break;
    }
    case RPC_FUNC_PF_WRITE: {
        struct rpc_pf_write_args *a = (struct rpc_pf_write_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = (int32_t)write(a->fd, payload + sizeof(*a), plen - sizeof(*a));
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_LSEEK: {
        struct rpc_pf_lseek_args *a = (struct rpc_pf_lseek_args *)payload;
        off_t pos;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        pos = lseek(a->fd, (off_t)a->offset, a->whence);
        if (pos < 0) {
            ret = -1; err = -errno;
        } else {
            memcpy(resp, &pos, sizeof(pos));
            resp_len = sizeof(pos);
        }
        break;
    }
    case RPC_FUNC_PF_FCNTL: {
        struct rpc_pf_fcntl_args *a = (struct rpc_pf_fcntl_args *)payload;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        ret = fcntl(a->fd, a->cmd, a->arg);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_UNLINK:
    case RPC_FUNC_PF_REMOVE:
    case RPC_FUNC_PF_RMDIR:
    case RPC_FUNC_PF_CHDIR: {
        char path[RPC_MAX_PAYLOAD + 1];

        if (path_from_payload(payload, plen, path, sizeof(path)) == NULL) {
            ret = -1; err = -EINVAL;
            break;
        }
        switch (hdr->func_id) {
        case RPC_FUNC_PF_UNLINK:
            ret = unlink(path);
            break;
        case RPC_FUNC_PF_REMOVE:
            ret = remove(path);
            break;
        case RPC_FUNC_PF_RMDIR:
            ret = rmdir(path);
            break;
        default:
            ret = chdir(path);
            break;
        }
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_MKDIR:
    case RPC_FUNC_PF_ACCESS: {
        struct rpc_pf_mode_args *a = (struct rpc_pf_mode_args *)payload;
        char path[RPC_MAX_PAYLOAD + 1];

        if (plen < sizeof(*a) ||
            path_from_payload(payload + sizeof(*a), plen - sizeof(*a),
                              path, sizeof(path)) == NULL) {
            ret = -1; err = -EINVAL;
            break;
        }
        if (hdr->func_id == RPC_FUNC_PF_MKDIR)
            ret = mkdir(path, (mode_t)a->mode);
        else
            ret = access(path, (int)a->mode);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_STAT:
    case RPC_FUNC_PF_FSTAT: {
        struct rpc_pf_stat_resp *r = (struct rpc_pf_stat_resp *)resp;
        struct stat st;
        char path[RPC_MAX_PAYLOAD + 1];

        if (hdr->func_id == RPC_FUNC_PF_STAT) {
            if (path_from_payload(payload, plen, path, sizeof(path)) == NULL) {
                ret = -1; err = -EINVAL;
                break;
            }
            ret = stat(path, &st);
        } else {
            struct rpc_pf_fstat_args *a = (struct rpc_pf_fstat_args *)payload;

            if (plen < sizeof(*a)) {
                ret = -1; err = -EINVAL;
                break;
            }
            ret = fstat(a->fd, &st);
        }
        if (ret < 0) {
            err = -errno;
        } else {
            r->st_size = (uint64_t)st.st_size;
            r->st_mode = (uint32_t)st.st_mode;
            r->st_blksize = (uint32_t)st.st_blksize;
            r->pad = 0;
            resp_len = sizeof(*r);
        }
        break;
    }
    case RPC_FUNC_PF_RENAME: {
        char paths[2 * RPC_MAX_PAYLOAD + 2];
        char *oldp, *newp;
        uint32_t i, first = 0;

        if (plen == 0 || plen >= sizeof(paths)) {
            ret = -1; err = -EINVAL;
            break;
        }
        for (i = 0; i < plen; i++) {
            if (payload[i] == '\0') {
                first = i;
                break;
            }
        }
        if (i == plen || first + 1 >= plen ||
            memchr(payload + first + 1, '\0', plen - first - 1) == NULL) {
            ret = -1; err = -EINVAL;
            break;
        }
        memcpy(paths, payload, plen);
        oldp = paths;
        newp = paths + first + 1;
        ret = rename(oldp, newp);
        if (ret < 0)
            err = -errno;
        break;
    }
    case RPC_FUNC_PF_GETCWD: {
        struct rpc_pf_getcwd_args *a = (struct rpc_pf_getcwd_args *)payload;

        if (plen < sizeof(*a) || a->size == 0 || a->size > resp_cap) {
            ret = -1; err = -EINVAL;
            break;
        }
        if (getcwd((char *)resp, a->size) == NULL) {
            ret = -1; err = -errno;
        } else {
            resp_len = (uint32_t)strlen((char *)resp) + 1;
        }
        break;
    }
    case RPC_FUNC_PF_GETDENTS: {
        struct rpc_pf_getdents_args *a = (struct rpc_pf_getdents_args *)payload;
        uint32_t want;
        long n;

        if (plen < sizeof(*a)) {
            ret = -1; err = -EINVAL;
            break;
        }
        want = a->count;
        if (want > resp_cap)
            want = resp_cap;
        n = syscall(SYS_getdents64, a->fd, resp, want);
        if (n < 0) {
            ret = -1; err = -errno;
        } else {
            ret = (int32_t)n;
            resp_len = (uint32_t)n;
        }
        break;
    }
    default:
        ret = -1;
        err = -EOPNOTSUPP;
        break;
    }

    (void)send_resp_frag(hdr->func_id, hdr->req_id, ret, err, resp, resp_len);
}

static void *worker_thread(void *arg)
{
    (void)arg;
    for (;;) {
        struct reqq_slot slot;
        bool found = false;

        pthread_mutex_lock(&g_reqq_lock);
        for (;;) {
            int i;

            for (i = 0; i < REQQ_DEPTH; i++) {
                if (g_reqq[i].used) {
                    memcpy(&slot, &g_reqq[i], sizeof(slot));
                    g_reqq[i].used = false;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
            {
                struct timespec ts;

                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 1;
                pthread_cond_timedwait(&g_reqq_cond, &g_reqq_lock, &ts);
            }
        }
        pthread_mutex_unlock(&g_reqq_lock);
        if (found)
            handle_req(&slot);
    }
    return NULL;
}

/* ---------- local echo servers (slave NpSrvEcho01 target) ---------- */
static void *udp_echo_thread(void *arg)
{
    struct sockaddr_in addr;
    uint8_t buf[ECHO_BUF_MAX];
    int port = (int)(intptr_t)arg;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[np] udp echo bind: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    printf("[np] udp echo :%d up\n", port);
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &flen);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (sendto(fd, buf, n, 0, (struct sockaddr *)&from, flen) < 0)
            fprintf(stderr, "[np] udp echo sendto: %s\n", strerror(errno));
    }
    close(fd);
    return NULL;
}

static void *tcp_echo_thread(void *arg)
{
    struct sockaddr_in addr;
    int port = (int)(intptr_t)arg;
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return NULL;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 4) < 0) {
        fprintf(stderr, "[np] tcp echo bind/listen: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    printf("[np] tcp echo :%d up\n", port);
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int cfd = accept(fd, (struct sockaddr *)&from, &flen);

        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        /* Echo until peer closes: proxied sender may write several
         * RPC-framed chunks into one TCP connection; closing after the
         * first read would hang up on the remaining fragments. */
        for (;;) {
            uint8_t buf[ECHO_BUF_MAX];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);

            if (n <= 0)
                break;
            if (send(cfd, buf, n, 0) < 0)
                break;
        }
        close(cfd);
    }
    close(fd);
    return NULL;
}

/* ---------- connect-back helper (slave np server-phase partner) ----------
 * 从核 np_* server 相位在本进程 fd 表 bind 127.0.0.1:CB_PORT 并
 * select+accept 等待——本线程周期连接它、发 hello 并回显，板内闭环
 * bind/listen/select/accept 全链路（无需 PC 侧配合）。 */
static void *connectback_thread(void *arg)
{
    (void)arg;
    for (;;) {
        struct sockaddr_in sin;
        struct pollfd pfd;
        char buf[64];
        int fd;

        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            goto next;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(CB_PORT);
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
            goto next_close;
        if (send(fd, "master-connect-back", 19, 0) < 0)
            goto next_close;
        pfd.fd = fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 3000) > 0) {
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

            if (n > 0) {
                buf[n] = '\0';
                printf("[np] connectback reply: %s\n", buf);
            }
        }
next_close:
        close(fd);
next:
        sleep(3);
    }
    return NULL;
}

/* ---------- public API ---------- */
int host_passive_start(void)
{
    pthread_t tid;
    static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&start_lock);
    if (g_started) {
        pthread_mutex_unlock(&start_lock);
        return 0;
    }
    g_started = 1;
    pthread_mutex_unlock(&start_lock);

    sem_init(&g_back_sem, 0, 0);

    if (pthread_create(&tid, NULL, worker_thread, NULL) != 0 ||
        pthread_create(&tid, NULL, udp_echo_thread, (void *)(intptr_t)ECHO_UDP_PORT) != 0 ||
        pthread_create(&tid, NULL, tcp_echo_thread, (void *)(intptr_t)ECHO_TCP_PORT) != 0 ||
        pthread_create(&tid, NULL, connectback_thread, NULL) != 0) {
        fprintf(stderr, "[np] passive thread create failed: %s\n",
                strerror(errno));
        return -1;
    }
    printf("[host] passive service up (worker + loopback echo %d/%d + connectback)\n",
           ECHO_UDP_PORT, ECHO_TCP_PORT);
    return 0;
}

int host_passive_attach(struct mica_client *c)
{
    struct mica_service svc;
    int ret;

    memset(&svc, 0, sizeof(svc));
    snprintf(svc.name, sizeof(svc.name), "%s", "host-passive-svc");
    svc.rpmsg_ns_match = passive_match;
    svc.rpmsg_ns_bind_cb = passive_bind;
    ret = mica_register_service(c, &svc);
    if (ret != 0) {
        printf("    [passive] register_service=%d\n", ret);
        return ret;
    }
    return 0;
}

void host_passive_detach(void)
{
    pthread_mutex_lock(&g_tx_lock);
    if (g_rpc_ok) {
        rpmsg_destroy_ept(&g_rpc_ept);
        g_rpc_ok = 0;
    }
    if (g_tty_ok) {
        rpmsg_destroy_ept(&g_tty_ept);
        g_tty_ok = 0;
    }
    if (g_umt_ok) {
        rpmsg_destroy_ept(&g_umt_ept);
        g_umt_ok = 0;
    }
    /* umt copy-mem 态随轮作废：mica_stop/固件重载后旧 mmap 指向的物理区
         * 已失效（实测 SIGBUS sig=7：第二轮 run 复用脏映射写崩——尸检
         * addr 落 mmap 区实锤）。munmap + 全清，下轮 bind 重新
         * umt_copy_mem_init + addr-probe。 */
    if (g_umt.vir_addr) {
        munmap(g_umt.vir_addr, g_umt.size);
        g_umt.vir_addr = NULL;
    }
    g_umt.phy_addr = 0;
    g_umt.size = 0;
    pthread_mutex_unlock(&g_tx_lock);
}
