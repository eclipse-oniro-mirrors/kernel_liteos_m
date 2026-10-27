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
 *  2. socket proxy  - the np_* API (libmica net_service.c) forwards BSD
 *     socket calls to the master netproxy worker over the same rpc channel
 *     (the master executes the real socket()/connect()/send()/recv() on its
 *     linux stack). shell command "nptest <ip> <port>" runs a UDP and a TCP
 *     probe against <ip> and prints each step's result.
 *  3. pfs/system   - the pfs_* API (libmica fs_service.c) forwards file
 *     calls to the master filesystem; np_system runs a shell command on
 *     the master console. shell commands "fstest" and "systest" demo them.
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
#include <mica/net_proxy.h>
#include <mica/pfs.h>
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
 * np_* socket proxy API: provided by libmica services/net_service.c
 * (see include/mica/net_proxy.h). pfs_* / np_system: services/fs_service.c
 * (see include/mica/pfs.h).
 * ------------------------------------------------------------------------ */

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

/* forward decls: the shell commands below reuse the autotest bodies */
STATIC INT32 NpFsTest(VOID);
STATIC INT32 NpSystemTest(VOID);

STATIC VOID NpFsCmd(UINT32 argc, char **argv)
{
    (VOID)argc;
    (VOID)argv;
    {
        INT32 fails = NpFsTest();

        PRINTK("[np] fstest: %d fails\n", fails);
        (VOID)mica_tty_printf("[np] fstest: %d fails\n", fails);
    }
}

STATIC VOID NpSysCmd(UINT32 argc, char **argv)
{
    (VOID)argc;
    (VOID)argv;
    {
        INT32 fails = NpSystemTest();

        PRINTK("[np] systest: %d fails\n", fails);
        (VOID)mica_tty_printf("[np] systest: %d fails\n", fails);
    }
}

UINT32 NpShellCmdRegister(VOID)
{
    UINT32 ret1 = osCmdReg(CMD_TYPE_EX, "rpctest", XARGS,
                           (CmdCallBackFunc)NpPingCmd);
    UINT32 ret2 = osCmdReg(CMD_TYPE_EX, "nptest", XARGS,
                           (CmdCallBackFunc)NpTestCmd);
    UINT32 ret3 = osCmdReg(CMD_TYPE_EX, "fstest", XARGS,
                           (CmdCallBackFunc)NpFsCmd);
    UINT32 ret4 = osCmdReg(CMD_TYPE_EX, "systest", XARGS,
                           (CmdCallBackFunc)NpSysCmd);
#ifdef LOSCFG_MCS_TIMER
    {
        extern UINT32 TimerShellCmdRegister(VOID);
        (VOID)TimerShellCmdRegister();
    }
#endif
    return (ret1 == LOS_OK && ret2 == LOS_OK &&
            ret3 == LOS_OK && ret4 == LOS_OK) ? LOS_OK : LOS_NOK;
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

    if (size == 0 || size > NP_BIGMSG_MAX || NpParseIp("127.0.0.1", &ipBe) != 0) {
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
        g_npCcFails[1] += NpProbe("127.0.0.1", NP_UDP_PORT, &probeRes);
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

/* --------------------------------------------------------------------------
 * sockopt / name tests: setsockopt+getsockopt (SO_RCVBUF), getsockname on
 * a bound UDP socket, getpeername + shutdown on a connected TCP socket.
 * ------------------------------------------------------------------------ */

#define NP_SO_RCVBUF_DEF 8192

STATIC INT32 NpSockOptsTest(VOID)
{
    UINT8 addr[NP_SOCKADDR_IN_LEN];
    UINT32 ipBe;
    UINT32 optval = 0;
    UINT32 optlen = sizeof(optval);
    INT32 fails = 0;
    INT32 fd, ret;

    /* setsockopt/getsockopt: grow SO_RCVBUF and read it back */
    fd = np_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
    if (fd < 0) {
        return 1;
    }
    optval = NP_SO_RCVBUF_DEF;
    ret = np_setsockopt(fd, 1 /* SOL_SOCKET */, 8 /* SO_RCVBUF */,
                        &optval, sizeof(optval));
    if (ret != 0) {
        fails++;
    }
    optlen = sizeof(optval);
    optval = 0;
    ret = np_getsockopt(fd, 1, 8, &optval, &optlen);
    if (ret != 0 || optlen != sizeof(uint32_t) ||
        optval < NP_SO_RCVBUF_DEF) { /* linux doubles it, never shrinks */
        fails++;
    }
    NpRpcLog("auto: sockopt rcvbuf set=%d get=%u ret=%d", NP_SO_RCVBUF_DEF,
             optval, ret);

    /* getsockname: bind ANY:0, the master assigns an ephemeral port */
    if (NpParseIp("0.0.0.0", &ipBe) != 0) {
        ipBe = 0;
    }
    NpBuildSockaddrIn(addr, ipBe, 0);
    ret = np_bind(fd, addr, sizeof(addr));
    if (ret != 0) {
        fails++;
    } else {
        UINT8 local[NP_SOCKADDR_IN_LEN];
        UINT32 localLen = sizeof(local);

        (VOID)memset(local, 0, sizeof(local));
        ret = np_getsockname(fd, local, &localLen);
        if (ret != 0 || localLen < 8 || local[2] == 0) {
            fails++; /* family 2 + non-zero assigned port expected */
        } else {
            UINT16 portBe = (UINT16)((local[2] << 8) | local[3]);

            NpRpcLog("auto: getsockname port=%u ret=%d", portBe, ret);
        }
    }
    (VOID)np_close(fd);

    /* getpeername + shutdown on a connected TCP socket (in-master echo) */
    if (NpParseIp("127.0.0.1", &ipBe) != 0) {
        return fails + 1;
    }
    fd = np_socket(2, 1 /* SOCK_STREAM */, 0);
    if (fd < 0) {
        return fails + 1;
    }
    NpBuildSockaddrIn(addr, ipBe, (UINT16)(NP_UDP_PORT + 1));
    if (np_connect(fd, addr, sizeof(addr)) == 0) {
        UINT8 peer[NP_SOCKADDR_IN_LEN];
        UINT32 peerLen = sizeof(peer);

        (VOID)memset(peer, 0, sizeof(peer));
        ret = np_getpeername(fd, peer, &peerLen);
        if (ret != 0 || peerLen < 8 || peer[2] == 0) {
            fails++;
        }
        /* SHUT_WR(1): further sends are refused, the echo side sees EOF */
        ret = np_shutdown(fd, 1);
        if (ret != 0) {
            fails++;
        }
        NpRpcLog("auto: getpeername+shutdown ret=%d fails=%d", ret, fails);
    } else {
        fails++;
    }
    (VOID)np_close(fd);
    return fails;
}

/* --------------------------------------------------------------------------
 * server path: bind+listen+select+accept. the master demo runs a
 * connect-back thread (see netproxy_demo.c) that keeps trying
 * 127.0.0.1:NP_CB_PORT, so the accept here completes fully in-board.
 * ------------------------------------------------------------------------ */

#define NP_CB_PORT 7005

STATIC INT32 NpServerTest(VOID)
{
    UINT8 addr[NP_SOCKADDR_IN_LEN];
    UINT32 ipBe;
    UINT8 rxBuf[64];
    np_fd_set rdset;
    INT32 fails = 0;
    INT32 lfd, cfd, ret;
    INT32 nfds, waited = 0;

    if (NpParseIp("127.0.0.1", &ipBe) != 0) {
        return 1;
    }
    lfd = np_socket(2, 1 /* SOCK_STREAM */, 0);
    if (lfd < 0) {
        NpRpcLog("auto: server socket fail %d", lfd);
        return 1;
    }
    NpBuildSockaddrIn(addr, ipBe, NP_CB_PORT);
    ret = np_bind(lfd, addr, sizeof(addr));
    if (ret != 0) {
        NpRpcLog("auto: server bind fail %d", ret);
        (VOID)np_close(lfd);
        return 1;
    }
    ret = np_listen(lfd, 1);
    if (ret != 0) {
        NpRpcLog("auto: server listen fail %d", ret);
        (VOID)np_close(lfd);
        return 1;
    }

    /* select with a 1 s poll timeout until the connect-back lands (the
     * master retries every 3 s; allow up to ~20 s) */
    do {
        NP_FD_ZERO(&rdset);
        NP_FD_SET(lfd, &rdset);
        nfds = np_select(lfd + 1, &rdset, NULL, NULL, 1000);
        if (nfds < 0) {
            NpRpcLog("auto: server select fail %d", nfds);
            (VOID)np_close(lfd);
            return 1;
        }
        if (nfds > 0 && NP_FD_ISSET(lfd, &rdset)) {
            break;
        }
    } while (++waited < 20);

    cfd = np_accept(lfd, NULL, NULL);
    if (cfd < 0) {
        NpRpcLog("auto: server accept fail %d (waited %d)", cfd, waited);
        fails++;
    } else {
        const char *hello = "slave-server-ready";

        ret = np_recv(cfd, rxBuf, sizeof(rxBuf), 0, 3000);
        if (ret <= 0) {
            fails++;
        } else {
            ret = np_send(cfd, hello, (UINT32)strlen(hello), 0);
            if (ret != (INT32)strlen(hello)) {
                fails++;
            }
        }
        (VOID)np_shutdown(cfd, 2 /* SHUT_RDWR */);
        (VOID)np_close(cfd);
        NpRpcLog("auto: server accept fd=%d echo fails=%d", cfd, fails);
    }
    (VOID)np_close(lfd);
    return fails;
}

/* --------------------------------------------------------------------------
 * proxy filesystem + system: full pfs_* flow against the master /data.
 * ------------------------------------------------------------------------ */

#define NP_FS_DIR  "/data/np_fs_demo"
#define NP_FS_FILE NP_FS_DIR "/hello.txt"

/* Linux ABI values (musl-compatible; the local fcntl.h on liteos-m may
 * not exist on every build, so keep them literal and documented). */
#define NP_O_RDONLY       0
#define NP_O_WRONLY       1
#define NP_O_RDWR         2
#define NP_O_CREAT        0100
#define NP_O_TRUNC        01000
#define NP_SEEK_SET       0
#define NP_F_GETFL        3
#define NP_S_IFMT         0170000u
#define NP_S_IFREG        0100000u
#define NP_S_IFDIR        0040000u

STATIC INT32 NpFsTest(VOID)
{
    static const char content[] = "pfs hello from the liteos slave";
    char buf[128];
    char cwd[128];
    struct pfs_stat st;
    UINT8 dirents[512];
    INT32 fd, ret, fails = 0;
    int64_t pos;

    /* clean slate from a previous run, then mkdir */
    (VOID)pfs_rmdir(NP_FS_DIR);
    ret = pfs_mkdir(NP_FS_DIR, 0755);
    if (ret != 0) {
        NpRpcLog("auto: pfs mkdir fail %d", ret);
        return 1;
    }
    ret = pfs_stat(NP_FS_DIR, &st);
    if (ret != 0 || (st.mode & NP_S_IFMT) != NP_S_IFDIR) {
        fails++;
        NpRpcLog("auto: pfs stat(dir) ret=%d mode=%o", ret, st.mode);
    }

    /* write the file (fragmented path: > 424 B payload slices) */
    fd = pfs_open(NP_FS_FILE, NP_O_WRONLY | NP_O_CREAT | NP_O_TRUNC, 0644);
    if (fd < 0) {
        NpRpcLog("auto: pfs open(w) fail %d", fd);
        (VOID)pfs_rmdir(NP_FS_DIR);
        return 1;
    }
    ret = pfs_write(fd, content, (UINT32)sizeof(content) - 1);
    if (ret != (INT32)sizeof(content) - 1) {
        fails++;
        NpRpcLog("auto: pfs write ret=%d", ret);
    }
    ret = pfs_close(fd);
    if (ret != 0) {
        fails++;
    }

    /* stat + read-back + lseek */
    ret = pfs_stat(NP_FS_FILE, &st);
    if (ret != 0 || st.size != sizeof(content) - 1 ||
        (st.mode & NP_S_IFMT) != NP_S_IFREG) {
        fails++;
        NpRpcLog("auto: pfs stat(file) ret=%d size=%llu", ret,
                 (unsigned long long)st.size);
    }
    fd = pfs_open(NP_FS_FILE, NP_O_RDONLY, 0);
    if (fd < 0) {
        NpRpcLog("auto: pfs open(r) fail %d", fd);
        (VOID)pfs_unlink(NP_FS_FILE);
        (VOID)pfs_rmdir(NP_FS_DIR);
        return fails + 1;
    }
    ret = pfs_fcntl(fd, NP_F_GETFL, 0);
    if (ret < 0) {
        fails++;
    }
    ret = pfs_read(fd, buf, sizeof(buf));
    if (ret != (INT32)sizeof(content) - 1 ||
        memcmp(buf, content, sizeof(content) - 1) != 0) {
        fails++;
        NpRpcLog("auto: pfs read ret=%d", ret);
    }
    pos = pfs_lseek(fd, 0, NP_SEEK_SET);
    if (pos != 0) {
        fails++;
    }
    (VOID)pfs_close(fd);

    /* rename -> access -> unlink -> rmdir */
    ret = pfs_rename(NP_FS_FILE, NP_FS_DIR "/renamed.txt");
    if (ret != 0) {
        fails++;
    }
    ret = pfs_access(NP_FS_DIR "/renamed.txt", 0 /* F_OK */);
    if (ret != 0) {
        fails++;
    }
    ret = pfs_unlink(NP_FS_DIR "/renamed.txt");
    if (ret != 0) {
        fails++;
    }

    /* getdents: one more file so the directory is non-empty */
    fd = pfs_open(NP_FS_DIR "/keep.txt", NP_O_WRONLY | NP_O_CREAT, 0644);
    if (fd >= 0) {
        (VOID)pfs_write(fd, "keep", 4);
        (VOID)pfs_close(fd);
        fd = pfs_open(NP_FS_DIR, NP_O_RDONLY, 0);
        ret = pfs_getdents(fd, dirents, sizeof(dirents));
        if (ret <= 0) {
            fails++;
            NpRpcLog("auto: pfs getdents ret=%d", ret);
        } else {
            UINT32 off = 0;
            UINT32 n = 0;

            while (PfsGetdentsNext(dirents, (UINT32)ret, &off) != NULL) {
                n++;
            }
            /* '.' + '..' + keep.txt */
            if (n < 3) {
                fails++;
            }
            NpRpcLog("auto: pfs getdents bytes=%d entries=%u", ret, n);
        }
        (VOID)pfs_close(fd);
        (VOID)pfs_remove(NP_FS_DIR "/keep.txt");
    } else {
        fails++;
    }

    /* chdir + getcwd round-trip */
    ret = pfs_chdir(NP_FS_DIR);
    if (ret != 0) {
        fails++;
    } else {
        ret = pfs_getcwd(cwd, sizeof(cwd));
        if (ret < 0 || strcmp(cwd, NP_FS_DIR) != 0) {
            fails++;
            NpRpcLog("auto: pfs getcwd ret=%d cwd=%s", ret, cwd);
        }
        (VOID)pfs_chdir("/");
    }
    ret = pfs_rmdir(NP_FS_DIR);
    if (ret != 0) {
        fails++;
    }
    NpRpcLog("auto: pfs all steps fails=%d", fails);
    return fails;
}

STATIC INT32 NpSystemTest(VOID)
{
    char buf[64];
    INT32 fd, ret, fails = 0;

    /* run a master-side command whose output lands in a file we then
     * read back through the pfs proxy (two proxies in one flow) */
    (VOID)pfs_unlink("/data/np_system.txt");
    ret = np_system("echo np-system-ok > /data/np_system.txt");
    if (ret != 0) {
        NpRpcLog("auto: system ret=%d", ret);
        return 1;
    }
    fd = pfs_open("/data/np_system.txt", NP_O_RDONLY, 0);
    if (fd < 0) {
        return 1;
    }
    ret = pfs_read(fd, buf, sizeof(buf));
    (VOID)pfs_close(fd);
    (VOID)pfs_unlink("/data/np_system.txt");
    if (ret < 0 || strstr(buf, "np-system-ok") == NULL) {
        fails++;
    }
    NpRpcLog("auto: system+readback ret=%d fails=%d", ret, fails);
    return fails;
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
    (VOID)NpProbe("127.0.0.1", NP_UDP_PORT, &probeRes);
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
    /* proxy network completion: sockopts/names + server (bind/listen/
     * select/accept); proxy filesystem + proxy shell */
    NpRpcLog("auto: sockopts fails=%d", NpSockOptsTest());
    NpRpcLog("auto: server fails=%d", NpServerTest());
    NpRpcLog("auto: pfs fails=%d", NpFsTest());
    NpRpcLog("auto: system fails=%d", NpSystemTest());
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
        if (++waited > 12000) { /* ~240 s at 500 hz (20 ms per iter) */
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
