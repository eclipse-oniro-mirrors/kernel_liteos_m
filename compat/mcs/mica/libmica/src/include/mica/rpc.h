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

#ifndef MICA_RPC_H
#define MICA_RPC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * rpmsg-rpc service (see services/rpc_service.c and services/rpc_proto.h).
 *
 * The slave plays BOTH roles on the single "rpmsg-rpc" endpoint:
 *   - server: requests from the master are dispatched to handlers
 *     registered with mica_rpc_register_handler();
 *   - client: mica_rpc_call() sends a request to the master and blocks
 *     until the response arrives (the socket proxy layer is built on it).
 */

/* Handler invoked in the receiver-thread callback context when a master
 * request arrives: fill resp (up to resp_cap) and return its length, or
 * return a negative errno-style value to report ret=-1/err=-ret.
 * Handlers must be fast: they run on the single rpmsg rx path. */
typedef int (*mica_rpc_handler_t)(uint32_t func_id,
                                  const void *req, uint32_t req_len,
                                  void *resp, uint32_t resp_cap);

/**
 * Whether the rpc endpoint is bound and ready.
 * @return: 1 ready, 0 not ready
 */
int mica_rpc_is_ready_pub(void);

/**
 * Register the server-side handler for master->slave requests
 * (single handler: a later call replaces the previous one).
 *
 * @return: 0 on success; -EAGAIN if the rpc service is not ready
 */
int mica_rpc_register_handler(mica_rpc_handler_t handler);

/**
 * Issue a synchronous RPC call to the master (client role).
 * Only ONE call may be in flight at a time (serialized internally).
 *
 * @param func_id   opcode (RPC_FUNC_*)
 * @param req       request payload (may be NULL when req_len == 0)
 * @param req_len   request payload length (<= RPC_MAX_PAYLOAD)
 * @param resp      response payload buffer (may be NULL)
 * @param resp_cap  response buffer capacity
 * @param resp_len  out: actual response payload length (may be NULL)
 * @param timeout_ms -1: wait forever; >=0: bounded wait
 * @return: hdr.ret of the response (>=0 or -1; on -1 the master errno is
 *          negated, e.g. -111 for ECONNREFUSED); on transport errors a
 *          negative errno of the local call itself (-ETIMEDOUT/-EIO/...)
 */
int mica_rpc_call(uint32_t func_id,
                  const void *req, uint32_t req_len,
                  void *resp, uint32_t resp_cap, uint32_t *resp_len,
                  int timeout_ms);

/* rpc service counters for diagnostics (see mica_rpc_get_diag) */
struct mica_rpc_diag {
    uint32_t rx_cnt;       /* frames received on the rpc endpoint */
    uint32_t rx_req_cnt;   /* requests dispatched to the registered handler */
    uint32_t tx_cnt;       /* successful rpmsg sends from this service */
    uint32_t tx_fail;      /* failed rpmsg sends */
    uint32_t rx_short_cnt; /* frames shorter than the rpc header */
    uint32_t drop_cnt;     /* responses dropped (stale id / wrong func) */
    uint32_t timeout_cnt;  /* mica_rpc_call waits that timed out */
    uint32_t last_rx_func;
    uint32_t last_rx_flags;
    uint32_t last_rx_id;
};

/**
 * Snapshot of the rpc service counters (filled atomically enough for
 * diagnostics; used by RPC_FUNC_STAT handlers).
 */
void mica_rpc_get_diag(struct mica_rpc_diag *d);

#ifdef __cplusplus
}
#endif

#endif /* MICA_RPC_H */
