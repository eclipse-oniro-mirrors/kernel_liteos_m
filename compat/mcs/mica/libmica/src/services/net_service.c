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
 * np_* socket proxy: thin mica_rpc_call wrappers. Every call blocks until
 * the master netproxy worker answers; fd values are master-side fds and
 * stay opaque on the slave. See include/mica/net_proxy.h and rpc_proto.h.
 */

#include <errno.h>
#include <string.h>
#include "mica/rpc.h"
#include "mica/rpc_proto.h"
#include "mica/net_proxy.h"

/* raw sockaddr length cap: sockaddr_in6 (28) is the largest we proxy */
#define NP_ADDR_MAX 28

int np_socket(int domain, int type, int protocol)
{
    struct rpc_socket_args args = { domain, type, protocol };
    struct rpc_socket_resp resp;
    int ret = mica_rpc_call(RPC_FUNC_SOCKET, &args, sizeof(args),
                            &resp, sizeof(resp), NULL, -1);
    return ret < 0 ? -1 : resp.fd;
}

int np_connect(int fd, const uint8_t *addr, uint32_t addrLen)
{
    uint8_t msg[sizeof(struct rpc_sockaddr_args) + NP_ADDR_MAX];
    struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)msg;

    if (addrLen == 0 || addrLen > NP_ADDR_MAX) {
        return -EINVAL;
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
    uint8_t msg[RPC_MAX_PAYLOAD];
    struct rpc_sendto_args *a = (struct rpc_sendto_args *)msg;
    uint32_t maxData = RPC_MAX_PAYLOAD - sizeof(*a) - NP_ADDR_MAX;

    if (addrLen == 0 || addrLen > NP_ADDR_MAX) {
        return -EINVAL;
    }
    if (len > maxData) {
        return -EMSGSIZE; /* keep datagram atomic: no fragmentation for UDP */
    }
    a->fd = fd;
    a->flags = flags;
    a->addrlen = addrLen;
    a->len = len;
    memcpy(msg + sizeof(*a), dest, addrLen);
    memcpy(msg + sizeof(*a) + addrLen, buf, len);
    return mica_rpc_call(RPC_FUNC_SENDTO, msg,
                         sizeof(*a) + addrLen + len, NULL, 0, NULL, -1);
}

int np_recvfrom(int fd, void *buf, uint32_t cap, int flags,
                int timeoutMs, uint8_t *from, uint32_t *fromLen)
{
    /* response payload: [addrlen u32][sockaddr bytes][data bytes] */
    uint8_t tmp[RPC_MAX_PAYLOAD];
    struct rpc_recvfrom_args args = { fd, flags, timeoutMs };
    uint32_t got = 0;
    int ret = mica_rpc_call(RPC_FUNC_RECVFROM, &args, sizeof(args),
                            tmp, sizeof(tmp), &got, -1);

    if (ret < 0) {
        return ret;
    }
    if (got < sizeof(uint32_t)) {
        return -EIO;
    }
    {
        uint32_t alen = *(const uint32_t *)tmp;
        uint32_t copyLen;
        if (alen > got - sizeof(uint32_t)) {
            return -EIO;
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

int np_bind(int fd, const uint8_t *addr, uint32_t addrLen)
{
    uint8_t msg[sizeof(struct rpc_sockaddr_args) + NP_ADDR_MAX];
    struct rpc_sockaddr_args *a = (struct rpc_sockaddr_args *)msg;

    if (addrLen == 0 || addrLen > NP_ADDR_MAX) {
        return -EINVAL;
    }
    a->fd = fd;
    a->addrlen = addrLen;
    memcpy(msg + sizeof(*a), addr, addrLen);
    return mica_rpc_call(RPC_FUNC_BIND, msg, sizeof(*a) + addrLen,
                         NULL, 0, NULL, -1);
}

int np_listen(int fd, int backlog)
{
    struct rpc_listen_args args = { fd, backlog };

    return mica_rpc_call(RPC_FUNC_LISTEN, &args, sizeof(args),
                         NULL, 0, NULL, -1);
}

int np_accept(int fd, uint8_t *from, uint32_t *fromLen)
{
    struct rpc_accept_args args = { fd };
    uint8_t tmp[RPC_MAX_PAYLOAD];
    uint32_t got = 0;
    int ret = mica_rpc_call(RPC_FUNC_ACCEPT, &args, sizeof(args),
                            tmp, sizeof(tmp), &got, -1);
    struct rpc_accept_resp *r = (struct rpc_accept_resp *)tmp;

    if (ret < 0) {
        return -1;
    }
    if (got < sizeof(*r) || r->fd < 0) {
        return -1;
    }
    if (from != NULL && fromLen != NULL && r->addrlen > 0) {
        uint32_t avail = got - sizeof(*r);
        uint32_t c = (r->addrlen < avail) ? r->addrlen : avail;
        c = (c < *fromLen) ? c : *fromLen;
        memcpy(from, tmp + sizeof(*r), c);
        *fromLen = c;
    }
    return r->fd;
}

int np_setsockopt(int fd, int level, int optname,
                  const void *optval, uint32_t optlen)
{
    uint8_t msg[RPC_MAX_PAYLOAD];
    struct rpc_setsockopt_args *a = (struct rpc_setsockopt_args *)msg;

    if (optlen > RPC_MAX_PAYLOAD - sizeof(*a)) {
        return -EINVAL;
    }
    a->fd = fd;
    a->level = level;
    a->optname = optname;
    a->optlen = optlen;
    if (optlen > 0 && optval != NULL) {
        memcpy(msg + sizeof(*a), optval, optlen);
    }
    return mica_rpc_call(RPC_FUNC_SETSOCKOPT, msg, sizeof(*a) + optlen,
                         NULL, 0, NULL, -1);
}

int np_getsockopt(int fd, int level, int optname,
                  void *optval, uint32_t *optlen)
{
    struct rpc_getsockopt_args args = { fd, level, optname,
                                        optlen ? *optlen : 0 };
    uint8_t tmp[RPC_MAX_PAYLOAD];
    uint32_t got = 0;
    int ret = mica_rpc_call(RPC_FUNC_GETSOCKOPT, &args, sizeof(args),
                            tmp, sizeof(tmp), &got, -1);

    if (ret < 0) {
        return -1;
    }
    if (got < sizeof(uint32_t)) {
        return -EIO;
    }
    {
        uint32_t len = *(const uint32_t *)tmp;
        uint32_t cap = optlen ? *optlen : 0;
        uint32_t copyLen = (len < got - sizeof(uint32_t)) ?
                           len : got - sizeof(uint32_t);
        if (copyLen > cap) {
            copyLen = cap;
        }
        if (copyLen > 0 && optval != NULL) {
            memcpy(optval, tmp + sizeof(uint32_t), copyLen);
        }
        if (optlen != NULL) {
            *optlen = copyLen;
        }
        return 0;
    }
}

/* GETSOCKNAME / GETPEERNAME share the wire shape: resp payload is
 * [addrlen u32][sockaddr bytes] */
static int np_getname(uint32_t funcId, int fd, uint8_t *addr, uint32_t *addrLen)
{
    struct rpc_getname_args args = { fd };
    uint8_t tmp[RPC_MAX_PAYLOAD];
    uint32_t got = 0;
    int ret = mica_rpc_call(funcId, &args, sizeof(args),
                            tmp, sizeof(tmp), &got, -1);

    if (ret < 0) {
        return -1;
    }
    if (got < sizeof(uint32_t)) {
        return -EIO;
    }
    {
        uint32_t alen = *(const uint32_t *)tmp;
        uint32_t avail = got - sizeof(uint32_t);
        uint32_t c = (alen < avail) ? alen : avail;
        if (addrLen == NULL) {
            return -EINVAL;
        }
        if (addr != NULL && c > 0) {
            uint32_t copyLen = (c < *addrLen) ? c : *addrLen;
            memcpy(addr, tmp + sizeof(uint32_t), copyLen);
        }
        *addrLen = c;
        return 0;
    }
}

int np_getsockname(int fd, uint8_t *addr, uint32_t *addrLen)
{
    return np_getname(RPC_FUNC_GETSOCKNAME, fd, addr, addrLen);
}

int np_getpeername(int fd, uint8_t *addr, uint32_t *addrLen)
{
    return np_getname(RPC_FUNC_GETPEERNAME, fd, addr, addrLen);
}

int np_shutdown(int fd, int how)
{
    struct rpc_shutdown_args args = { fd, how };

    return mica_rpc_call(RPC_FUNC_SHUTDOWN, &args, sizeof(args),
                         NULL, 0, NULL, -1);
}

int np_select(int nfds, np_fd_set *rdset, np_fd_set *wrset,
              np_fd_set *exset, int timeoutMs)
{
    struct rpc_select_args args;
    uint8_t tmp[RPC_MAX_PAYLOAD];
    uint32_t got = 0;
    int ret;

    if (nfds < 0 || nfds > NP_FDSET_BYTES * 8) {
        return -EINVAL;
    }
    memset(&args, 0, sizeof(args));
    args.nfds = nfds;
    args.timeout_ms = timeoutMs;
    if (rdset != NULL) {
        memcpy(args.rdset, rdset->bits, RPC_SELECT_SET_BYTES);
    }
    if (wrset != NULL) {
        memcpy(args.wrset, wrset->bits, RPC_SELECT_SET_BYTES);
    }
    if (exset != NULL) {
        memcpy(args.exset, exset->bits, RPC_SELECT_SET_BYTES);
    }
    ret = mica_rpc_call(RPC_FUNC_SELECT, &args, sizeof(args),
                        tmp, sizeof(tmp), &got, -1);
    if (ret < 0) {
        return ret;
    }
    /* resp payload: rdset + wrset + exset (only when the caller passed
     * the corresponding set) */
    {
        uint32_t need = 0;
        uint32_t off = 0;
        if (rdset != NULL) {
            need += RPC_SELECT_SET_BYTES;
        }
        if (wrset != NULL) {
            need += RPC_SELECT_SET_BYTES;
        }
        if (exset != NULL) {
            need += RPC_SELECT_SET_BYTES;
        }
        if (got < need) {
            return -EIO;
        }
        if (rdset != NULL) {
            memcpy(rdset->bits, tmp + off, RPC_SELECT_SET_BYTES);
            off += RPC_SELECT_SET_BYTES;
        }
        if (wrset != NULL) {
            memcpy(wrset->bits, tmp + off, RPC_SELECT_SET_BYTES);
            off += RPC_SELECT_SET_BYTES;
        }
        if (exset != NULL) {
            memcpy(exset->bits, tmp + off, RPC_SELECT_SET_BYTES);
        }
    }
    return ret;
}
