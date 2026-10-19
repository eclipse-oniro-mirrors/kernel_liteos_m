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

/* rpmsg-rpc service: synchronous request/response over the "rpmsg-rpc"
 * endpoint, modelled on the UniProton proxy service (rpmsg_service.c).
 *
 * The slave plays both roles on this single endpoint:
 *   server - requests from the master (flags & RPC_F_REQ) are dispatched
 *            to the handler registered via mica_rpc_register_handler()
 *            directly in the receiver-thread callback context (handlers
 *            must be fast; slow work belongs to its own task);
 *   client - mica_rpc_call() serializes one in-flight request and waits
 *            for the matching response (req_id + RPC_F_RESP). The socket
 *            proxy layer on top of it never needs a local timeout: the
 *            master worker always answers (poll timeouts are reported as
 *            ret=-1/err=EAGAIN responses).
 *
 * Fragmentation (RPC_F_MORE) is honoured on received responses; requests
 * sent by mica_rpc_call are limited to RPC_MAX_PAYLOAD per call (the np_
 * socket layer slices larger sends into multiple RPC calls itself).
 */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <mica/mica.h>
#include <mica/rpc.h>
#include <mica/platform/log.h>
#include <mica/platform/sem.h>
#include <mica/platform/delay.h>
#include "mica_service_internal.h"
#include <mica/rpc_proto.h>

#define RPMSG_RPC_EPT_NAME "rpmsg-rpc"

/* mica_rpc_call timeout: poll the response flag every tick. The slave OS
 * tick is 1 ms (1000 Hz), so one mica_delay_tick(1) == 1 ms - wait_ticks
 * must be timeout_ms / 1. (A wrong 10 ms assumption here silently shrinks
 * every call timeout by 10x: the 30 s default becomes 3 s and races the
 * master worker's own poll timeouts.) */
#define MICA_RPC_TICK_MS 1
#define MICA_RPC_DEFAULT_TIMEOUT_MS 30000

static struct rpmsg_endpoint g_rpc_ept;

/* server side */
static mica_rpc_handler_t g_rpc_handler;

/* client side: one in-flight call, guarded by call_sem (binary) */
static mica_sem_t g_rpc_call_sem;
static mica_sem_t g_rpc_resp_sem;
static uint32_t g_rpc_req_id;
static volatile uint32_t g_rpc_wait_id;
static volatile uint32_t g_rpc_wait_func; /* RPC_FUNC_* of the pending call:
                                           * responses match id AND func (the
                                           * master runs its own req_id counter,
                                           * so ids alone are ambiguous) */
static struct {
    int32_t ret;
    int32_t err;
    uint32_t len;              /* total received so far */
    uint32_t cap;              /* caller buffer capacity */
    uint8_t *buf;              /* caller buffer (NULL: discard) */
    volatile bool done;
} g_rpc_resp_state;

/* diagnostics snapshot (RPC_FUNC_STAT / mica_rpc_get_diag) */
static volatile struct mica_rpc_diag g_rpc_diag;

/* tx serialization: open-amp on this side has no internal lock, and the
 * receiver thread (responses to master requests) and caller tasks
 * (mica_rpc_call requests) may hit rpmsg_send concurrently - racing the
 * virtqueue avail ring corrupts it (frames lost, channel wedges). */
static mica_sem_t g_rpc_tx_sem;

static int RpcSend(struct rpmsg_endpoint *ept, const void *buf, uint32_t len)
{
    int ret;

    (void)mica_sem_wait(g_rpc_tx_sem);
    ret = rpmsg_send(ept, buf, len);
    (void)mica_sem_post(g_rpc_tx_sem);
    if (ret < 0) {
        g_rpc_diag.tx_fail++;
    } else {
        g_rpc_diag.tx_cnt++;
    }
    return ret;
}

int mica_rpc_is_ready_pub(void)
{
    return is_rpmsg_ept_ready(&g_rpc_ept);
}

int mica_rpc_register_handler(mica_rpc_handler_t handler)
{
    if (!mica_rpc_is_ready_pub()) {
        return -EAGAIN;
    }
    g_rpc_handler = handler;
    return 0;
}

/* receiver-thread context: dispatch a master request to the registered
 * handler and send the response back on the same endpoint. */
static void RpcHandleRequest(struct rpmsg_endpoint *ept,
                             struct rpc_hdr *hdr, uint32_t len)
{
    uint8_t resp_buf[RPC_MAX_PAYLOAD];
    struct rpc_hdr *resp = (struct rpc_hdr *)resp_buf;
    const void *req_payload = (len > sizeof(*hdr)) ?
        (const void *)(hdr + 1) : NULL;
    uint32_t req_len = (len > sizeof(*hdr)) ? len - sizeof(*hdr) : 0;
    int hret;
    int ret;

    resp->func_id = hdr->func_id;
    resp->req_id = hdr->req_id;
    resp->flags = RPC_F_RESP;
    resp->ret = 0;
    resp->err = 0;

    if (g_rpc_handler == NULL) {
        resp->ret = -1;
        resp->err = -EOPNOTSUPP;
        resp->payload_len = 0;
        (void)RpcSend(ept, resp, sizeof(*resp));
        return;
    }

    hret = g_rpc_handler(hdr->func_id, req_payload, req_len,
                         resp_buf + sizeof(*resp),
                         RPC_MAX_PAYLOAD - sizeof(*resp));
    if (hret < 0) {
        resp->ret = -1;
        resp->err = hret; /* negative errno, master negates */
        resp->payload_len = 0;
    } else {
        resp->payload_len = (uint16_t)hret;
    }
    ret = RpcSend(ept, resp, sizeof(*resp) + resp->payload_len);
    if (ret < 0) {
        /* no mica_log here: the slave console can be wedged (TX ring full,
         * never drained) and any log write would freeze the receiver thread;
         * failures are visible via mica_rpc_get_diag()/RPC_FUNC_STAT */
        return;
    }
}

/* receiver-thread context: match and collect a response fragment for the
 * pending mica_rpc_call(). */
static void RpcHandleResponse(struct rpc_hdr *hdr, uint32_t len)
{
    uint32_t payload_len = (len > sizeof(*hdr)) ? len - sizeof(*hdr) : 0;
    const uint8_t *payload = (const uint8_t *)(hdr + 1);

    if (hdr->req_id != g_rpc_wait_id || hdr->func_id != g_rpc_wait_func) {
        g_rpc_diag.drop_cnt++;
        return; /* stale/unsolicited/wrong-func: drop (counter, no log:
                 * the slave console may be wedged and a log write here
                 * would freeze the receiver thread) */
    }
    if (g_rpc_resp_state.done) {
        return;
    }

    g_rpc_resp_state.ret = hdr->ret;
    g_rpc_resp_state.err = hdr->err;

    if (g_rpc_resp_state.buf != NULL && payload_len > 0) {
        uint32_t room = g_rpc_resp_state.cap - g_rpc_resp_state.len;
        uint32_t copy = (payload_len < room) ? payload_len : room;
        memcpy(g_rpc_resp_state.buf + g_rpc_resp_state.len, payload, copy);
    }
    g_rpc_resp_state.len += payload_len;

    if ((hdr->flags & RPC_F_MORE) == 0) {
        g_rpc_resp_state.done = true;
        (void)mica_sem_post(g_rpc_resp_sem);
    }
}

static int rpc_rx_callback(struct rpmsg_endpoint *ept, void *data,
                           size_t len, uint32_t src, void *priv)
{
    struct rpc_hdr *hdr = data;

    (void)src;
    (void)priv;
    if (len < sizeof(*hdr)) {
        g_rpc_diag.rx_short_cnt++;
        return 0;
    }
    /* no mica_log on this hot path: the slave console may be wedged (TX
     * ring never drains) and one log write would freeze the receiver
     * thread - diagnostics go out via RPC_FUNC_STAT counters instead */
    g_rpc_diag.rx_cnt++;
    g_rpc_diag.last_rx_func = hdr->func_id;
    g_rpc_diag.last_rx_flags = hdr->flags;
    g_rpc_diag.last_rx_id = hdr->req_id;
    if (hdr->flags & RPC_F_RESP) {
        RpcHandleResponse(hdr, (uint32_t)len);
    } else if (hdr->flags & RPC_F_REQ) {
        g_rpc_diag.rx_req_cnt++;
        RpcHandleRequest(ept, hdr, (uint32_t)len);
    }
    /* open-amp virtio rx buffers are returned by the stack after the
     * callback; nothing to hold here (no zero-copy use of the payload
     * beyond this frame: RpcHandleRequest copies into resp_buf, and
     * RpcHandleResponse copies into the caller buffer). */
    return 0;
}

int mica_rpc_call(uint32_t func_id,
                  const void *req, uint32_t req_len,
                  void *resp, uint32_t resp_cap, uint32_t *resp_len,
                  int timeout_ms)
{
    uint8_t msg[RPC_MAX_PAYLOAD + sizeof(struct rpc_hdr)];
    struct rpc_hdr *hdr = (struct rpc_hdr *)msg;
    uint32_t wait_ticks;
    uint32_t waited = 0;
    int ret;

    if (timeout_ms < 0) {
        timeout_ms = MICA_RPC_DEFAULT_TIMEOUT_MS;
    }
    wait_ticks = (uint32_t)timeout_ms / MICA_RPC_TICK_MS;
    if (wait_ticks == 0) {
        wait_ticks = 1;
    }

    if (!mica_rpc_is_ready_pub()) {
        return -EAGAIN;
    }
    if (req_len > RPC_MAX_PAYLOAD || (req == NULL && req_len > 0)) {
        return -EINVAL;
    }

    (void)mica_sem_wait(g_rpc_call_sem); /* serialize in-flight calls */

    g_rpc_req_id++;
    g_rpc_wait_id = g_rpc_req_id;
    g_rpc_wait_func = func_id;
    g_rpc_resp_state.buf = (uint8_t *)resp;
    g_rpc_resp_state.cap = resp_cap;
    g_rpc_resp_state.len = 0;
    g_rpc_resp_state.done = false;

    hdr->func_id = func_id;
    hdr->req_id = g_rpc_req_id;
    hdr->flags = RPC_F_REQ;
    hdr->payload_len = (uint16_t)req_len;
    hdr->ret = 0;
    hdr->err = 0;
    if (req_len > 0) {
        memcpy(hdr + 1, req, req_len);
    }

    ret = RpcSend(&g_rpc_ept, msg, sizeof(*hdr) + req_len);
    if (ret < 0) {
        /* counted in tx_fail (see RpcSend); no log - the console may be
         * wedged and writing here would freeze this caller */
        ret = -EIO;
        goto out;
    }

    /* poll for the response: mica sem has no timed wait, and a stuck
     * master (worker not up yet) must not hang this caller forever */
    while (!g_rpc_resp_state.done) {
        if (waited++ >= wait_ticks) {
            g_rpc_diag.timeout_cnt++;
            ret = -ETIMEDOUT;
            goto out;
        }
        mica_delay_tick(1);
    }

    if (resp_len != NULL) {
        *resp_len = g_rpc_resp_state.len;
    }
    if (g_rpc_resp_state.ret < 0) {
        ret = g_rpc_resp_state.err; /* negative errno from the master */
    } else {
        ret = g_rpc_resp_state.ret;
    }

out:
    g_rpc_resp_state.done = true; /* ignore stragglers */
    g_rpc_wait_id = 0;
    g_rpc_wait_func = 0;
    (void)mica_sem_post(g_rpc_call_sem);
    return ret;
}

void mica_rpc_get_diag(struct mica_rpc_diag *d)
{
    if (d == NULL) {
        return;
    }
    d->rx_cnt = g_rpc_diag.rx_cnt;
    d->rx_req_cnt = g_rpc_diag.rx_req_cnt;
    d->tx_cnt = g_rpc_diag.tx_cnt;
    d->tx_fail = g_rpc_diag.tx_fail;
    d->rx_short_cnt = g_rpc_diag.rx_short_cnt;
    d->drop_cnt = g_rpc_diag.drop_cnt;
    d->timeout_cnt = g_rpc_diag.timeout_cnt;
    d->last_rx_func = g_rpc_diag.last_rx_func;
    d->last_rx_flags = g_rpc_diag.last_rx_flags;
    d->last_rx_id = g_rpc_diag.last_rx_id;
}

void *rpc_service_thread(void *arg)
{
    struct rpmsg_device *rpdev = mica_get_rpdev();
    int ret;

    (void)arg;
    if (!rpdev) {
        mica_log("RPC: rpmsg device not initialized\n");
        goto err_init;
    }

    ret = mica_sem_init(&g_rpc_call_sem, 1);
    if (ret != 0) {
        mica_log("RPC: failed to create call_sem\n");
        goto err_init;
    }
    ret = mica_sem_init(&g_rpc_resp_sem, 0);
    if (ret != 0) {
        mica_log("RPC: failed to create resp_sem\n");
        goto err_call_sem;
    }
    ret = mica_sem_init(&g_rpc_tx_sem, 1);
    if (ret != 0) {
        mica_log("RPC: failed to create tx_sem\n");
        goto err_resp_sem;
    }
    g_rpc_req_id = 0;
    g_rpc_wait_id = 0;
    g_rpc_wait_func = 0;
    g_rpc_resp_state.done = true;

    ret = rpmsg_create_ept(&g_rpc_ept, rpdev, RPMSG_RPC_EPT_NAME,
                           RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                           rpc_rx_callback, NULL);
    if (ret != 0) {
        mica_log("RPC: failed to create endpoint: %d\n", ret);
        goto err_tx_sem;
    }

    /* all rx processing happens in the receiver-thread callbacks; this
     * thread just keeps the service alive (echo_service pattern) */
    for (;;) {
        mica_delay_tick(1000);
    }

err_tx_sem:
    mica_sem_destroy(g_rpc_tx_sem);
err_resp_sem:
    mica_sem_destroy(g_rpc_resp_sem);
err_call_sem:
    mica_sem_destroy(g_rpc_call_sem);
err_init:
    mica_log("RPC: service thread exiting with error\n");
    pthread_exit(NULL);
}

/* internal (mica_service.c): spawn the rpc service thread */
int mica_rpc_init_service(pthread_attr_t *attr)
{
    pthread_t thread;

    return pthread_create(&thread, attr, rpc_service_thread, NULL);
}

int mica_rpc_is_ready(void)
{
    return mica_rpc_is_ready_pub();
}
