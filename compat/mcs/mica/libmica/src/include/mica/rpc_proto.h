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
 *
 * rpmsg-rpc protocol shared between the slave (libmica rpc service) and
 * the Linux master (netproxy worker). Twin file: keep in sync with
 * device/board/hihope/rk3568_amp/example/rpc_proto.h on the master side.
 * Message layout on the "rpmsg-rpc" endpoint (single rpmsg buffer):
 *   [struct rpc_hdr][payload bytes]
 * Requests carry RPC_F_REQ; responses carry RPC_F_RESP and fill ret/err.
 * Payloads larger than RPC_MAX_PAYLOAD are fragmented: every fragment is
 * a standalone message whose payload is a contiguous slice of the whole
 * request/response buffer; all but the last fragment set RPC_F_MORE.
 * Reassembly: accumulate slices in arrival order until a fragment without
 * RPC_F_MORE arrives.
 */

#ifndef MICA_RPC_PROTO_H
#define MICA_RPC_PROTO_H

/* RPMSG_BUFFER_SIZE(512) - rpmsg header(16..) - sizeof(struct rpc_hdr):
 * 448 keeps the total safely inside one 512-byte vring buffer. */
#define RPC_MAX_PAYLOAD 448

#define RPC_F_REQ  0x0001U /* master->slave or slave->master request */
#define RPC_F_RESP 0x0002U /* response to a request */
#define RPC_F_MORE 0x0004U /* fragment: more fragments follow */

struct rpc_hdr {
    uint32_t func_id;      /* opcode, see RPC_FUNC_* below */
    uint32_t req_id;       /* request id echoed in the response */
    uint16_t flags;        /* RPC_F_* */
    uint16_t payload_len;  /* payload bytes in THIS message */
    int32_t ret;           /* response: primary return value (>=0 or -1) */
    int32_t err;           /* response: errno (0 when ret >= 0) */
    uint32_t reserved;     /* keep 0. Pads the hdr to 24B so the payload
                            * lands 8B-aligned inside the rpmsg buffer
                            * (buffer + 16 + 24): memcpy block-copy paths
                            * doing 8B loads on shared-window buffers never
                            * see a 4B-aligned source. */
};

_Static_assert(sizeof(struct rpc_hdr) == 24, "rpc_hdr must stay 24B (8B-aligned payload)");

/* ---- generic / service-probe functions (either direction) ---- */
#define RPC_FUNC_PING     1 /* payload echoed back verbatim (round-trip) */
#define RPC_FUNC_GET_TICK 2 /* slave -> master: no args; resp: uint64 ticks */
#define RPC_FUNC_LOG      0x200 /* slave -> master: log line (payload: text);
                                  * used to surface slave-side diagnostics in
                                  * the master console when the slave serial
                                  * console is unavailable */
#define RPC_FUNC_STAT     0x202 /* master -> slave: no args; the slave handler
                                  * answers with struct rpc_stat_resp so the
                                  * master can watch service liveness and
                                  * rpc-level counters over time */

/* RPC_FUNC_STAT resp: slave-side snapshot (one rpmsg buffer, no fragments) */
struct rpc_stat_resp {
    uint32_t magic;        /* 0x53544154 ('STAT') */
    uint32_t master_seen;  /* slave handler invocations (app-level counter) */
    uint32_t tick;         /* slave LOS_TickCountGet() snapshot */
    uint32_t rx_cnt;       /* rpc frames received (any direction/role) */
    uint32_t rx_req_cnt;   /* requests dispatched to the slave handler */
    uint32_t tx_cnt;       /* successful rpmsg sends from the rpc service */
    uint32_t tx_fail;      /* failed rpmsg sends */
    uint32_t rx_short_cnt; /* short frames dropped */
    uint32_t drop_cnt;     /* responses dropped (stale id / wrong func) */
    uint32_t timeout_cnt;  /* local mica_rpc_call timeouts */
    uint32_t last_rx_func;
    uint32_t last_rx_flags;
    uint32_t last_rx_id;
};

/* ---- socket proxy (slave client -> master worker) ---- */
#define RPC_FUNC_SOCKET     0x100
#define RPC_FUNC_BIND       0x101
#define RPC_FUNC_CONNECT    0x102
#define RPC_FUNC_LISTEN     0x103
#define RPC_FUNC_ACCEPT     0x104
#define RPC_FUNC_SEND       0x110
#define RPC_FUNC_SENDTO     0x111
#define RPC_FUNC_RECV       0x112
#define RPC_FUNC_RECVFROM   0x113
#define RPC_FUNC_CLOSE      0x120
#define RPC_FUNC_SETSOCKOPT 0x121

/* RPC_FUNC_SOCKET args/resp */
struct rpc_socket_args {
    int32_t domain;
    int32_t type;
    int32_t protocol;
};
struct rpc_socket_resp {
    int32_t fd; /* proxy-side fd, opaque to the slave */
};

/* RPC_FUNC_BIND / RPC_FUNC_CONNECT args: fd + raw sockaddr bytes */
struct rpc_sockaddr_args {
    int32_t fd;
    uint32_t addrlen;
    /* sockaddr bytes follow */
};

/* RPC_FUNC_LISTEN args */
struct rpc_listen_args {
    int32_t fd;
    int32_t backlog;
};

/* RPC_FUNC_ACCEPT args: listening fd; resp: new fd + peer sockaddr */
struct rpc_accept_args {
    int32_t fd;
};
struct rpc_accept_resp {
    int32_t fd;
    uint32_t addrlen;
    /* sockaddr bytes follow */
};

/* RPC_FUNC_SEND args: fd + flags + data (fragmented when > RPC_MAX_PAYLOAD) */
struct rpc_send_args {
    int32_t fd;
    int32_t flags;
    uint32_t len; /* TOTAL data length across all fragments */
    /* data bytes follow in this and subsequent fragments */
};

/* RPC_FUNC_SENDTO args: fd + flags + dest sockaddr + data */
struct rpc_sendto_args {
    int32_t fd;
    int32_t flags;
    uint32_t addrlen;
    uint32_t len; /* TOTAL data length across all fragments */
    /* sockaddr bytes, then data bytes (fragmented) */
};

/* RPC_FUNC_RECV args */
struct rpc_recv_args {
    int32_t fd;
    int32_t flags;
    int32_t timeout_ms; /* -1: block; 0: poll once; >0: poll with timeout */
};

/* RPC_FUNC_RECVFROM args */
struct rpc_recvfrom_args {
    int32_t fd;
    int32_t flags;
    int32_t timeout_ms;
};

/* RPC_FUNC_RECV / RECVFROM resp: ret/err in hdr carry the length/errno;
 * payload = received bytes (fragmented when > RPC_MAX_PAYLOAD - hdrs). */

/* RPC_FUNC_CLOSE args */
struct rpc_close_args {
    int32_t fd;
};

/* RPC_FUNC_SETSOCKOPT args: level/optname + raw optval bytes */
struct rpc_setsockopt_args {
    int32_t fd;
    int32_t level;
    int32_t optname;
    uint32_t optlen;
    /* optval bytes follow */
};

#endif /* MICA_RPC_PROTO_H */
