/*
 * copyright (c) 2026 huawei device co., ltd.
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 *
 * mica rpc service probe + socket proxy demo (slave side).
 * two halves:
 *  1. service probe - this core registers an RPC server handler
 *     (RPC_FUNC_PING echo) so the MASTER can verify the slave service
 *     framework; the shell command "rpctest [len]" plays the client and
 *     round-trips a payload through the master (mica_rpc_call).
 *  2. socket proxy  - the np_* API below forwards BSD socket calls to
 *     the master netproxy worker over the same rpc channel (the master
 *     executes the real socket()/connect()/send()/recv() on its linux
 *     stack). shell command "nptest <ip> <port>" runs a UDP and a TCP
 *     probe against <ip> and prints each step's result.
 * address arguments are raw sockaddr byte arrays (addrlen bytes) so the
 * proxy stays independent of any socket header on this side; nptest
 * builds the 16-byte sockaddr_in itself (family/port/addr big-endian).
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "los_compiler.h"
#include "los_task.h"
#include "los_debug.h"
#ifdef LOSCFG_SHELL
#include "shell.h"
#include "shmsg.h"
#endif
#include <mica/rpc.h>
#include <mica/rpc_proto.h>
#ifdef LOSCFG_SHELL
#include <mica/service.h> /* mica_tty_printf: shell command results also go
                             back to the interactive terminal on the master */
#endif

#define NP_SOCKADDR_IN_LEN 16

/* --------------------------------------------------------------------------
 * log forwarder: surface slave-side diagnostics on the master console via
 * RPC_FUNC_LOG (used by the auto self-test, which must not depend on the
 * slave UART - the serial console may be wedged or owned by the master).
 * ------------------------------------------------------------------------ */

STATIC VOID NpRpcLog(const char *fmt, ...)
{
    char text[128];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (len <= 0) {
        return;
    }
    /* fire-and-forget: the master prints the line on its console */
    (VOID)mica_rpc_call(RPC_FUNC_LOG, text, (UINT32)len + 1,
                        NULL, 0, NULL, 3000);
}

/* --------------------------------------------------------------------------
 * np_* socket proxy API (mica_rpc_call wrappers; blocking until the master
 * worker answers - the master always answers, poll timeouts come back as
 * -EAGAIN). fd values are master-side fds and stay opaque here.
 * ------------------------------------------------------------------------ */

int np_socket(int domain, int type, int protocol)
{
    struct rpc_socket_args args = { domain, type, protocol };
    struct rpc_socket_resp resp;

    return mica_rpc_call(RPC_FUNC_SOCKET, &args, sizeof(args),
                         &resp, sizeof(resp), NULL, -1) < 0 ? -1 : resp.fd;
}

int np_connect(int fd, const uint8_t *addr, uint32_t addrLen)
{
    uint8_t msg[sizeof(struct rpc_sockaddr_args) + NP_SOCKADDR_IN_LEN];
    struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)msg;

    if (addrLen == 0 || addrLen > NP_SOCKADDR_IN_LEN) {
        return -1;
    }
    a->fd = fd;
    a->addrlen = addrLen;
    memcpy(msg + sizeof(*a), addr, addrLen);
    return mica_rpc_call(RPC_FUNC_CONNECT, msg, sizeof(*a) + addrLen,
                         NULL, 0, NULL, -1);
}

int np_send(int fd, const void *buf, uint32_t len, int flags)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t total = 0;
    int ret = 0;

    while (len > 0) {
        /* one RPC message per slice (header + data fit one rpmsg buffer) */
        uint8_t msg[RPC_MAX_PAYLOAD];
        struct rpc_send_args *a = (struct rpc_send_args *)msg;
        uint32_t chunk = RPC_MAX_PAYLOAD - sizeof(*a);
        if (chunk > len) {
            chunk = len;
        }
        a->fd = fd;
        a->flags = flags;
        a->len = chunk;
        memcpy(msg + sizeof(*a), p, chunk);
        ret = mica_rpc_call(RPC_FUNC_SEND, msg, sizeof(*a) + chunk,
                            NULL, 0, NULL, -1);
        if (ret < 0) {
            return (total > 0) ? (int)total : ret;
        }
        total += (uint32_t)ret;
        p += chunk;
        len -= chunk;
    }
    return (int)total;
}

int np_recv(int fd, void *buf, uint32_t cap, int flags, int timeoutMs)
{
    struct rpc_recv_args args = { fd, flags, timeoutMs };
    uint32_t got = 0;
    int ret;

    ret = mica_rpc_call(RPC_FUNC_RECV, &args, sizeof(args),
                        buf, cap, &got, -1);
    if (ret < 0) {
        return ret;
    }
    return (int)got;
}

int np_sendto(int fd, const void *buf, uint32_t len, int flags,
              const uint8_t *dest, uint32_t addrLen)
{
    /* UDP semantics: the datagram must fit one rpc message */
    uint8_t msg[RPC_MAX_PAYLOAD];
    struct rpc_sendto_args *a = (struct rpc_sendto_args *)msg;
    uint32_t maxData = RPC_MAX_PAYLOAD - sizeof(*a) - NP_SOCKADDR_IN_LEN;
    uint32_t hdrLen;

    if (addrLen == 0 || addrLen > NP_SOCKADDR_IN_LEN) {
        return -1;
    }
    if (len > maxData) {
        return -1; /* keep datagram atomic: no fragmentation for UDP */
    }
    a->fd = fd;
    a->flags = flags;
    a->addrlen = addrLen;
    a->len = len;
    hdrLen = sizeof(*a);
    memcpy(msg + hdrLen, dest, addrLen);
    memcpy(msg + hdrLen + addrLen, buf, len);
    return mica_rpc_call(RPC_FUNC_SENDTO, msg, hdrLen + addrLen + len,
                         NULL, 0, NULL, -1);
}

int np_recvfrom(int fd, void *buf, uint32_t cap, int flags,
                int timeoutMs, uint8_t *from, uint32_t *fromLen)
{
    /* response payload: [addrlen u32][sockaddr bytes][data bytes];
     * the master copies the datagram in one rpc response (datagrams are
     * small); the fragment reassembly in rpc_service covers larger ones */
    uint8_t tmp[RPC_MAX_PAYLOAD];
    struct rpc_recvfrom_args args = { fd, flags, timeoutMs };
    uint32_t got = 0;
    int ret = mica_rpc_call(RPC_FUNC_RECVFROM, &args, sizeof(args),
                            tmp, sizeof(tmp), &got, -1);

    if (ret < 0) {
        return ret;
    }
    if (got < sizeof(uint32_t)) {
        return -1;
    }
    {
        uint32_t alen = *(const uint32_t *)tmp;
        uint32_t copyLen;
        if (alen > got - sizeof(uint32_t)) {
            return -1;
        }
        copyLen = got - sizeof(uint32_t) - alen;
        if (copyLen > cap) {
            copyLen = cap;
        }
        if (from != NULL && fromLen != NULL) {
            uint32_t c = (alen < *fromLen) ? alen : *fromLen;
            memcpy(from, tmp + sizeof(uint32_t), c);
            *fromLen = c;
        }
        memcpy(buf, tmp + sizeof(uint32_t) + alen, copyLen);
        return (int)copyLen;
    }
}

int np_close(int fd)
{
    struct rpc_close_args args = { fd };

    return mica_rpc_call(RPC_FUNC_CLOSE, &args, sizeof(args),
                         NULL, 0, NULL, -1);
}

/* --------------------------------------------------------------------------
 * RPC server handler (master -> slave service probe)
 * ------------------------------------------------------------------------ */

STATIC volatile UINT32 g_masterSeen = 0;

STATIC INT32 NpRpcHandler(UINT32 funcId, const void *req, UINT32 reqLen,
                          void *resp, UINT32 respCap)
{
    g_masterSeen++;
    switch (funcId) {
        case RPC_FUNC_PING:
            /* echo the payload back verbatim */
            if (reqLen > respCap) {
                reqLen = respCap;
            }
            if (reqLen > 0) {
                memcpy(resp, req, reqLen);
            }
            return (INT32)reqLen;
        case RPC_FUNC_STAT: {
            /* diagnostics snapshot for the master liveness pinger */
            struct rpc_stat_resp *s = (struct rpc_stat_resp *)resp;
            struct mica_rpc_diag d;

            if (respCap < sizeof(*s)) {
                return -EINVAL;
            }
            (VOID)memset(s, 0, sizeof(*s));
            s->magic = 0x53544154; /* 'STAT' */
            s->master_seen = g_masterSeen;
            s->tick = (UINT32)LOS_TickCountGet();
            mica_rpc_get_diag(&d);
            s->rx_cnt = d.rx_cnt;
            s->rx_req_cnt = d.rx_req_cnt;
            s->tx_cnt = d.tx_cnt;
            s->tx_fail = d.tx_fail;
            s->rx_short_cnt = d.rx_short_cnt;
            s->drop_cnt = d.drop_cnt;
            s->timeout_cnt = d.timeout_cnt;
            s->last_rx_func = d.last_rx_func;
            s->last_rx_flags = d.last_rx_flags;
            s->last_rx_id = d.last_rx_id;
            return (INT32)sizeof(*s);
        }
        default:
            return -EOPNOTSUPP;
    }
}

/* --------------------------------------------------------------------------
 * shell commands
 * ------------------------------------------------------------------------ */

#ifdef LOSCFG_SHELL

#define NP_PAYLOAD_MAX 256

STATIC INT32 NpPingOnce(UINT32 len)
{
    UINT8 req[NP_PAYLOAD_MAX];
    UINT8 resp[NP_PAYLOAD_MAX];
    UINT32 respLen = 0;
    INT32 ret;
    UINT32 i;
    UINT32 ticksBefore;
    UINT32 ticksAfter;

    if (len > NP_PAYLOAD_MAX) {
        len = NP_PAYLOAD_MAX;
    }
    for (i = 0; i < len; i++) {
        req[i] = (UINT8)(i & 0xff);
    }
    ticksBefore = (UINT32)LOS_TickCountGet();
    ret = mica_rpc_call(RPC_FUNC_PING, req, len, resp, sizeof(resp),
                        &respLen, -1);
    ticksAfter = (UINT32)LOS_TickCountGet();
    if (ret < 0) {
        return ret;
    }
    if (respLen != len || memcmp(req, resp, len) != 0) {
        return -1; /* mismatch */
    }
    return (INT32)(ticksAfter - ticksBefore); /* >=0: round-trip ticks */
}

STATIC VOID NpPingCmd(UINT32 argc, char **argv)
{
    UINT32 len = 16;
    INT32 ret;

    /* CMD_TYPE_EX convention on liteos-m: argv[0] is the FIRST user
     * argument (the command name is not passed - see osCmdParseOneToken:
     * index 0 is skipped for non-CMD_TYPE_STD commands). */
    if (argc >= 1) {
        len = (UINT32)strtoul(argv[0], NULL, 0);
    }
    ret = NpPingOnce(len);
    if (ret < 0) {
        PRINTK("[np] rpctest(%u) failed: %d\n", len, ret);
        (VOID)mica_tty_printf("[np] rpctest(%u) failed: %d\n", len, ret);
    } else {
        PRINTK("[np] rpctest(%u) ok: %d ticks round-trip\n", len, ret);
        (VOID)mica_tty_printf("[np] rpctest(%u) ok: %d ticks round-trip\n", len, ret);
    }
}

STATIC UINT32 NpParseIp(const char *s, UINT32 *ipBe)
{
    UINT32 v[4];
    UINT32 i;
    UINT32 val = 0;

    for (i = 0; i < 4; i++) {
        v[i] = 0;
        if (s == NULL) {
            return (UINT32)-1;
        }
        while (*s >= '0' && *s <= '9') {
            v[i] = v[i] * 10 + (UINT32)(*s - '0');
            s++;
        }
        if (v[i] > 0xff || (i < 3 && *s != '.')) {
            return (UINT32)-1;
        }
        if (i < 3) {
            s++;
        }
    }
    for (i = 0; i < 4; i++) {
        val |= v[i] << (8 * i); /* network byte order on little-endian */
    }
    *ipBe = val;
    return 0;
}

STATIC VOID NpBuildSockaddrIn(UINT8 *out, UINT32 ipBe, UINT16 port)
{
    memset(out, 0, NP_SOCKADDR_IN_LEN);
    out[0] = 0x02;                    /* sin_family = AF_INET (2) */
    out[2] = (UINT8)(port >> 8);      /* sin_port, network order */
    out[3] = (UINT8)(port & 0xff);
    out[4] = (UINT8)(ipBe & 0xff);    /* sin_addr, network order */
    out[5] = (UINT8)((ipBe >> 8) & 0xff);
    out[6] = (UINT8)((ipBe >> 16) & 0xff);
    out[7] = (UINT8)((ipBe >> 24) & 0xff);
}

#define NP_UDP_PORT 7000
/* end-to-end target: PC-side echo server (UDP 9000 / TCP 9001) attached to
 * the board's eth0 peer; the packets cross the real wire through the master
 * linux stack, not the in-master loopback echo of NP_UDP_PORT. */
#define NP_EXT_IP "192.168.100.1"
#define NP_EXT_PORT 9000
/* error-path targets on the same PC: 9101 accepts but never echoes
 * (exercises the RECV poll-timeout -> EAGAIN path over the real wire),
 * 9102 has no listener at all (connect -> RST -> ECONNREFUSED). */
#define NP_SILENT_PORT 9101
#define NP_REFUSED_PORT 9102
#define NP_TCP_PORT 7001

/* silent probe (no UART output): returns per-step results via out-params so
 * the caller can log them through whichever channel is alive */
struct npProbeResult {
    INT32 udpFd;
    INT32 udpSend;
    INT32 udpRecv;
    INT32 tcpFd;
    INT32 tcpConn;
    INT32 tcpSend;
    INT32 tcpRecv;
    INT32 fails;
};

STATIC INT32 NpProbe(const char *ipStr, UINT16 port, struct npProbeResult *r)
{
    UINT8 addr[NP_SOCKADDR_IN_LEN];
    UINT32 ipBe;
    const char *payload = "hello-from-liteos-slave";
    UINT8 rxBuf[128];
    INT32 fd;

    (VOID)memset(r, 0, sizeof(*r));
    if (NpParseIp(ipStr, &ipBe) != 0) {
        return -1;
    }

    /* ---- UDP probe ---- */
    fd = np_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
    r->udpFd = fd;
    if (fd < 0) {
        r->fails++;
        return r->fails;
    }
    NpBuildSockaddrIn(addr, ipBe, port);
    r->udpSend = np_sendto(fd, payload, strlen(payload), 0, addr, sizeof(addr));
    if (r->udpSend > 0) {
        uint8_t from[NP_SOCKADDR_IN_LEN];
        uint32_t fromLen = sizeof(from);
        r->udpRecv = np_recvfrom(fd, rxBuf, sizeof(rxBuf), 0, 3000,
                                 from, &fromLen);
        if (r->udpRecv < 0) {
            r->fails++;
        }
    } else {
        r->fails++;
    }
    (VOID)np_close(fd);

    /* ---- TCP probe ---- */
    fd = np_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
    r->tcpFd = fd;
    if (fd < 0) {
        r->fails++;
        return r->fails;
    }
    NpBuildSockaddrIn(addr, ipBe, (UINT16)(port + 1));
    r->tcpConn = np_connect(fd, addr, sizeof(addr));
    if (r->tcpConn == 0) {
        r->tcpSend = np_send(fd, payload, strlen(payload), 0);
        if (r->tcpSend > 0) {
            r->tcpRecv = np_recv(fd, rxBuf, sizeof(rxBuf), 0, 3000);
            if (r->tcpRecv < 0) {
                r->fails++;
            }
        } else {
            r->fails++;
        }
    } else {
        r->fails++;
    }
    (VOID)np_close(fd);
    return r->fails;
}

STATIC VOID NpTestCmd(UINT32 argc, char **argv)
{
    UINT16 port = NP_UDP_PORT;
    struct npProbeResult r;

    /* same CMD_TYPE_EX convention as NpPingCmd: argv[0] = first arg */
    if (argc < 1) {
        PRINTK("[np] usage: nptest <ip> [port]\n");
        (VOID)mica_tty_printf("[np] usage: nptest <ip> [port]\n");
        return;
    }
    if (argc > 1) {
        port = (UINT16)strtoul(argv[1], NULL, 0);
    }
    (VOID)NpProbe(argv[0], port, &r);
    PRINTK("[np] udp: fd=%d send=%d recv=%d\n", r.udpFd, r.udpSend, r.udpRecv);
    (VOID)mica_tty_printf("[np] udp: fd=%d send=%d recv=%d\n",
                          r.udpFd, r.udpSend, r.udpRecv);
    PRINTK("[np] tcp: fd=%d conn=%d send=%d recv=%d\n",
           r.tcpFd, r.tcpConn, r.tcpSend, r.tcpRecv);
    (VOID)mica_tty_printf("[np] tcp: fd=%d conn=%d send=%d recv=%d\n",
                          r.tcpFd, r.tcpConn, r.tcpSend, r.tcpRecv);
    PRINTK("[np] nptest done: %d fails\n", r.fails);
    (VOID)mica_tty_printf("[np] nptest done: %d fails\n", r.fails);
}

UINT32 NpShellCmdRegister(VOID)
{
    UINT32 ret1 = osCmdReg(CMD_TYPE_EX, "rpctest", XARGS,
                           (CmdCallBackFunc)NpPingCmd);
    UINT32 ret2 = osCmdReg(CMD_TYPE_EX, "nptest", XARGS,
                           (CmdCallBackFunc)NpTestCmd);
    return (ret1 == LOS_OK && ret2 == LOS_OK) ? LOS_OK : LOS_NOK;
}

#endif /* LOSCFG_SHELL */

/* --------------------------------------------------------------------------
 * auto self-test: slave -> master direction (rpc client + socket proxy).
 * triggered once the master rpc client has pinged us at least once (its
 * worker + local echo servers are up by then), so no shell input is needed
 * on this side (the serial console is owned by the master).
 * ------------------------------------------------------------------------ */

/* settle in slices with a heartbeat log per slice: if np_demo dies or the
 * channel wedges, the last printed slice localizes the failure in the
 * master console timeline (liveness pinger interleaves there too) */
#define NP_AUTO_SETTLE_SLICES      8
#define NP_AUTO_SETTLE_SLICE_TICKS 500 /* 8 x 500 = 4000 ticks total */

/* bigmsg round-trip over the in-master TCP echo: the tx side leaves as
 * multiple np_send slices and the echoed segments exceed one rpmsg
 * payload (424B usable), exercising the RPC_F_MORE response
 * fragmentation + reassembly path. size <= 4096. */
#define NP_BIGMSG_MAX 4096

STATIC INT32 NpBigMsgOnce(UINT32 size)
{
    static UINT8 txBuf[NP_BIGMSG_MAX];
    static UINT8 rxBuf[NP_BIGMSG_MAX];
    UINT8 addr[NP_SOCKADDR_IN_LEN];
    UINT32 ipBe;
    UINT32 i;
    INT32 fd, sent = 0, got = 0, fails = 0;

    if (size == 0 || size > NP_BIGMSG_MAX || NpParseIp("192.168.100.20", &ipBe) != 0) {
        return 1;
    }
    fd = np_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0);
    if (fd < 0) {
        NpRpcLog("auto: bigmsg %uB socket fail %d", size, fd);
        return 1;
    }
    NpBuildSockaddrIn(addr, ipBe, (UINT16)(NP_UDP_PORT + 1));
    if (np_connect(fd, addr, sizeof(addr)) < 0) {
        fails++;
    } else {
        for (i = 0; i < size; i++) {
            txBuf[i] = (UINT8)(i ^ 0xA5);
        }
        sent = np_send(fd, txBuf, size, 0);
        if (sent != (INT32)size) {
            fails++;
        }
        while (got < (INT32)size) {
            INT32 ret = np_recv(fd, rxBuf + got, size - got, 0, 3000);

            if (ret <= 0) {
                fails++;
                break;
            }
            got += ret;
        }
        if (got == (INT32)size && memcmp(txBuf, rxBuf, size) != 0) {
            fails++; /* full length but content mismatch */
        }
    }
    (VOID)np_close(fd);
    NpRpcLog("auto: bigmsg %uB sent=%d recv=%d fails=%d", size, sent, got, fails);
    return fails;
}

/* error paths over the real wire to the PC:
 *  1. RECV timeout: connect to the silent server, send, then recv with a
 *     3 s timeout - the master worker polls its socket, gets nothing and
 *     must answer ret=-1/err=EAGAIN (-11) instead of hanging the call;
 *  2. connect refused: nothing listens on 9102 - the RST must surface as
 *     ECONNREFUSED (-111) through the RPC response. */
STATIC VOID NpErrPathsTest(VOID)
{
    UINT8 addr[NP_SOCKADDR_IN_LEN];
    UINT8 rxBuf[64];
    UINT32 ipBe;
    UINT32 t0, elapsed;
    INT32 fd, ret;

    if (NpParseIp(NP_EXT_IP, &ipBe) != 0) {
        return;
    }
    fd = np_socket(2, 1, 0);
    if (fd < 0) {
        NpRpcLog("auto: errpath socket fail %d", fd);
        return;
    }
    NpBuildSockaddrIn(addr, ipBe, NP_SILENT_PORT);
    ret = np_connect(fd, addr, sizeof(addr));
    if (ret == 0) {
        (VOID)np_send(fd, "x", 1, 0);
        t0 = (UINT32)LOS_TickCountGet();
        ret = np_recv(fd, rxBuf, sizeof(rxBuf), 0, 3000);
        elapsed = (UINT32)LOS_TickCountGet() - t0; /* tick = 1 ms */
        /* expect ret == -11 (EAGAIN) after ~3000 ms */
        NpRpcLog("auto: errpath recv-timeout ret=%d dt=%ums", ret, elapsed);
    } else {
        NpRpcLog("auto: errpath recv-timeout conn fail %d", ret);
    }
    (VOID)np_close(fd);

    fd = np_socket(2, 1, 0);
    NpBuildSockaddrIn(addr, ipBe, NP_REFUSED_PORT);
    ret = np_connect(fd, addr, sizeof(addr));
    /* expect ret == -111 (ECONNREFUSED) */
    NpRpcLog("auto: errpath conn-refused ret=%d", ret);
    (VOID)np_close(fd);
}

/* concurrent callers: two tasks hammering np_* at the same time. all RPC
 * calls serialize on the rpc_service call semaphore, so this checks that
 * queuing neither deadlocks nor crosses responses between the two flows
 * (task a: bigmsg loops, task b: small UDP/TCP probes). */
static volatile INT32 g_npCcDone[2];
static volatile INT32 g_npCcFails[2];

STATIC VOID *NpCcTaskA(UINT32 arg)
{
    UINT32 i;

    (VOID)arg;
    for (i = 0; i < 3; i++) {
        g_npCcFails[0] += NpBigMsgOnce(1024);
    }
    g_npCcDone[0] = 1;
    return NULL;
}

STATIC VOID *NpCcTaskB(UINT32 arg)
{
    struct npProbeResult probeRes;
    UINT32 i;

    (VOID)arg;
    for (i = 0; i < 5; i++) {
        g_npCcFails[1] += NpProbe("192.168.100.20", NP_UDP_PORT, &probeRes);
    }
    g_npCcDone[1] = 1;
    return NULL;
}

STATIC VOID NpConcurrentTest(VOID)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 taskIdA = 0;
    UINT32 taskIdB = 0;
    UINT32 waited = 0;

    g_npCcDone[0] = 0;
    g_npCcDone[1] = 0;
    g_npCcFails[0] = 0;
    g_npCcFails[1] = 0;

    param.usTaskPrio = 22;
    param.uwStackSize = 0x1800;
    param.pcName = "np_cc_a";
    param.pfnTaskEntry = (TSK_ENTRY_FUNC)NpCcTaskA;
    if (LOS_TaskCreate(&taskIdA, &param) != LOS_OK) {
        NpRpcLog("auto: concurrent taskA create failed");
        return;
    }
    param.usTaskPrio = 23;
    param.pcName = "np_cc_b";
    param.pfnTaskEntry = (TSK_ENTRY_FUNC)NpCcTaskB;
    if (LOS_TaskCreate(&taskIdB, &param) != LOS_OK) {
        NpRpcLog("auto: concurrent taskB create failed");
        g_npCcDone[0] = 1; /* let the wait below finish */
        return;
    }
    /* generous bound: worst case one queued connect() costs the master
     * worker NP_CONNECT_TIMEOUT_MS before other requests get served */
    while ((g_npCcDone[0] == 0 || g_npCcDone[1] == 0) && waited < 6000) {
        (VOID)LOS_TaskDelay(10); /* 10 ms */
        waited++;
    }
    NpRpcLog("auto: concurrent aFails=%d bFails=%d done=%d/%d waited=%ums",
             g_npCcFails[0], g_npCcFails[1], g_npCcDone[0], g_npCcDone[1],
             waited * 10);
}

STATIC VOID NpAutoTest(VOID)
{
    INT32 r16, r64, r256;
    struct npProbeResult probeRes;
    UINT32 i;

    NpRpcLog("auto: enter (masterSeen=%u)", g_masterSeen);
    for (i = 1; i <= NP_AUTO_SETTLE_SLICES; i++) {
        (VOID)LOS_TaskDelay(NP_AUTO_SETTLE_SLICE_TICKS);
        NpRpcLog("auto: settle %u/%u (seen=%u)",
                 i, (UINT32)NP_AUTO_SETTLE_SLICES, g_masterSeen);
    }

    /* NOTE: no PRINTK here on purpose - the slave UART may be wedged; every
     * line goes to the master console over RPC_FUNC_LOG instead */
    r16 = NpPingOnce(16);
    NpRpcLog("auto: rpctest 16 -> %d", r16);
    r64 = NpPingOnce(64);
    NpRpcLog("auto: rpctest 64 -> %d", r64);
    r256 = NpPingOnce(256);
    NpRpcLog("auto: rpctest 256 -> %d", r256);
    (VOID)NpProbe("192.168.100.20", NP_UDP_PORT, &probeRes);
    NpRpcLog("auto: nptest udp fd=%d send=%d recv=%d",
             probeRes.udpFd, probeRes.udpSend, probeRes.udpRecv);
    NpRpcLog("auto: nptest tcp fd=%d conn=%d send=%d recv=%d fails=%d",
             probeRes.tcpFd, probeRes.tcpConn, probeRes.tcpSend, probeRes.tcpRecv, probeRes.fails);
    (VOID)NpBigMsgOnce(1024);
    (VOID)NpBigMsgOnce(4096);
    /* real-wire end-to-end: from-core -> rpmsg-rpc -> master linux stack ->
     * eth0 -> PC echo server and back. */
    (VOID)NpProbe(NP_EXT_IP, NP_EXT_PORT, &probeRes);
    NpRpcLog("auto: ext udp fd=%d send=%d recv=%d",
             probeRes.udpFd, probeRes.udpSend, probeRes.udpRecv);
    NpRpcLog("auto: ext tcp fd=%d conn=%d send=%d recv=%d fails=%d",
             probeRes.tcpFd, probeRes.tcpConn, probeRes.tcpSend, probeRes.tcpRecv, probeRes.fails);
    /* concurrent first: errpath's connect() to a dropped target burns
     * NP_CONNECT_TIMEOUT_MS on the master worker and would skew the
     * concurrent-task timing if it ran before this. */
    NpConcurrentTest();
    NpErrPathsTest();
    NpRpcLog("auto: done (masterSeen=%u)", g_masterSeen);
}

/* --------------------------------------------------------------------------
 * demo task: wait for the rpc service, register the probe handler and the
 * shell commands (task context: osCmdReg takes the shell cmd mutex), then
 * run the automatic self-test.
 * ------------------------------------------------------------------------ */

STATIC VOID *NpDemoTask(UINT32 arg)
{
    UINT32 waited = 0;

    (VOID)arg;
    /* mica services come up asynchronously (mica task); wait for rpc */
    while (!mica_rpc_is_ready_pub()) {
        (VOID)LOS_TaskDelay(10);
        if (++waited > 600) { /* ~60 s at 100 hz */
            return NULL;
        }
    }
    /* NOTE: no PRINTK from here on - if the slave UART is wedged, PRINTK
     * would hang this task before the self-test runs. all diagnostics go
     * to the master console over RPC_FUNC_LOG. */
    NpRpcLog("np_demo: task alive, rpc ready");
    if (mica_rpc_register_handler(NpRpcHandler) != 0) {
        NpRpcLog("np_demo: handler register FAILED");
        return NULL;
    }
    NpRpcLog("np_demo: handler registered");
    NpAutoTest();
    NpRpcLog("np_demo: autotest finished");
    /* shell registration comes last: osCmdReg takes the shell mutex and the
     * shell task may be blocked forever on UART input if the serial console
     * is wedged (owned by the master) */
    NpShellCmdRegister();
    return NULL;
}

UINT32 NetProxyTestTaskInit(VOID)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 taskId;

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)NpDemoTask;
    param.usTaskPrio = 21;
    param.uwStackSize = 0x2000; /* self-test uses sizeable stack buffers */
    param.pcName = "np_demo";
    return LOS_TaskCreate(&taskId, &param);
}
