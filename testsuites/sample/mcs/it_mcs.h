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
#ifndef _IT_MCS_H_
#define _IT_MCS_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_MCS_CAN
#include "can.h"
#endif
#ifdef LOSCFG_MCS_RPC
#include "mica/service.h"
#include "mica/rpc.h"
#include "mica/rpc_proto.h"
#include "mica/net_proxy.h"
#include "mica/pfs.h"

/* np_* socket proxy API, implemented in board netproxy_test.c */
int np_socket(int domain, int type, int protocol);
int np_connect(int fd, const uint8_t *addr, uint32_t addrLen);
int np_send(int fd, const void *buf, uint32_t len, int flags);
int np_recv(int fd, void *buf, uint32_t cap, int flags, int timeoutMs);
int np_sendto(int fd, const void *buf, uint32_t len, int flags,
              const uint8_t *dest, uint32_t addrLen);
int np_recvfrom(int fd, void *buf, uint32_t cap, int flags,
                int timeoutMs, uint8_t *from, uint32_t *fromLen);
int np_close(int fd);

/* sockaddr_in wire layout used by np_* (see netproxy_test.c
 * NpBuildSockaddrIn): 16-byte buffer, [0]=family(2), [2..3]=port BE,
 * [4..7]=ip BE, [8..15]=0 (the master executes real BSD socket calls,
 * so the full sockaddr_in length is required) */
#define IT_NP_SOCKADDR_LEN 16
static inline void ItNpBuildAddr(uint8_t *out, uint8_t ip0, uint8_t ip1,
                                 uint8_t ip2, uint8_t ip3, uint16_t port)
{
    (void)memset(out, 0, IT_NP_SOCKADDR_LEN);
    out[0] = 0x02;
    out[2] = (uint8_t)(port >> 8);
    out[3] = (uint8_t)(port & 0xff);
    out[4] = ip0;
    out[5] = ip1;
    out[6] = ip2;
    out[7] = ip3;
}

/* echo server used by np echo cases (PRECONDITION): an EXTERNAL host on the
 * switch segment — validates real external reachability via the np proxy
 * (ARP/switch/peer stack on the wire; a master-self target only exercises
 * the local route and loses the intent). Deployed on the peer PC
 * (proxy/echo_agent.ps1, peer_agent.sh installecho). default: peer PC. */
#define IT_NP_ECHO_IP0 192
#define IT_NP_ECHO_IP1 168
#define IT_NP_ECHO_IP2 2
#define IT_NP_ECHO_IP3 1
#define IT_NP_ECHO_PORT_UDP 9000
#define IT_NP_ECHO_PORT_TCP 9001
#endif





/* E2E 设计批次（mcs_svc_MR_1198）从核-主核配合协议（rpc_proto.h opcode 空闲段） */
#define RPC_FUNC_E2E_HANDLER  0x320  /* 主核反向 call 的从核测试 handler func_id（2026-11-02 挪号：
                                     * 原 0x300 与 PF_OPEN(0x300) 撞号——方向分离掩盖多年，
                                     * server 合流后同方向分发冲突暴露，测试宏整体挪 0x32x） */
#define RPC_FUNC_E2E_TRIGGER  0x321  /* 从核通知主核触发反向 call（resp 回报 handler 执行证据） */
#define RPC_FUNC_E2E_UMT_DOWN 0x322  /* 从核按需触发一帧 umt 下行（Svc013） */
#define RPC_FUNC_E2E_TTY_EXEC 0x323  /* 从核请求主核向 tty pty 注入长命令（combo04, D14） */

/* 主核功能远程执行（UmtHost 域：从核用例驱动主核侧 UMT 数据面 user_msg API 测试，
 * 主核被动执行并回结果；协议结构与 server/rpc_proto.h 保持同步） */
#define RPC_FUNC_EXEC_UMT_CTX_CREATE  0x340
#define RPC_FUNC_EXEC_UMT_CTX_DESTROY 0x341
#define RPC_FUNC_EXEC_UMT_REGISTER    0x342
#define RPC_FUNC_EXEC_UMT_UNREGISTER  0x343
#define RPC_FUNC_EXEC_UMT_SEND        0x344
#define RPC_FUNC_EXEC_UMT_RECV        0x345
#define RPC_FUNC_EXEC_UMT_CB_COUNT    0x346

struct rpc_exec_umt_ctx_create_args {
    int32_t instance;
    int32_t ped;
};
struct rpc_exec_umt_ok_resp {
    int32_t ok;
};
struct rpc_exec_umt_register_args {
    int32_t null_cb;
};
struct rpc_exec_umt_ret_resp {
    int32_t ret;
};
struct rpc_exec_umt_send_args {
    uint32_t len;
};
struct rpc_exec_umt_recv_args {
    uint32_t timeout_ms;
    uint32_t rlen;
};
struct rpc_exec_umt_recv_resp {
    int32_t ret;
    uint32_t rlen;
};
struct rpc_exec_umt_cb_count_resp {
    uint32_t count;
};

#endif /* _IT_MCS_H_ */
