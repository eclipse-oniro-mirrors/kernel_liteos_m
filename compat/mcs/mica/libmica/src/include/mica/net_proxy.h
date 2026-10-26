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
 * np_* socket proxy API: BSD socket calls forwarded to the master netproxy
 * worker over the mica rpc channel (the master executes the real socket()
 * on its Linux stack). The fd values are master-side fds and stay opaque
 * here. Addresses are raw sockaddr byte arrays so the proxy stays
 * independent of any socket header on this side.
 */

#ifndef MICA_NET_PROXY_H
#define MICA_NET_PROXY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fd bitmap for np_select(): 1024 master fds, POSIX fd_set layout */
#define NP_FDSET_BYTES 128
typedef struct {
    uint8_t bits[NP_FDSET_BYTES];
} np_fd_set;

#define NP_FD_ZERO(s) do { \
    for (int _i = 0; _i < NP_FDSET_BYTES; _i++) { \
        (s)->bits[_i] = 0; \
    } \
} while (0)
#define NP_FD_SET(fd, s) do { \
    if ((fd) >= 0 && (fd) < NP_FDSET_BYTES * 8) { \
        (s)->bits[(fd) >> 3] |= (uint8_t)(1u << ((fd) & 7)); \
    } \
} while (0)
#define NP_FD_CLR(fd, s) do { \
    if ((fd) >= 0 && (fd) < NP_FDSET_BYTES * 8) { \
        (s)->bits[(fd) >> 3] &= (uint8_t)~(1u << ((fd) & 7)); \
    } \
} while (0)
#define NP_FD_ISSET(fd, s) \
    (((fd) >= 0 && (fd) < NP_FDSET_BYTES * 8) ? \
     (((s)->bits[(fd) >> 3] >> ((fd) & 7)) & 1) : 0)

/*
 * All calls block until the master worker answers (the master always
 * answers; poll timeouts come back as -EAGAIN). On failure the master
 * errno is returned negated (e.g. -111 for ECONNREFUSED).
 */
int np_socket(int domain, int type, int protocol);
int np_connect(int fd, const uint8_t *addr, uint32_t addrLen);
int np_send(int fd, const void *buf, uint32_t len, int flags);
int np_recv(int fd, void *buf, uint32_t cap, int flags, int timeoutMs);
int np_sendto(int fd, const void *buf, uint32_t len, int flags,
              const uint8_t *dest, uint32_t addrLen);
int np_recvfrom(int fd, void *buf, uint32_t cap, int flags,
                int timeoutMs, uint8_t *from, uint32_t *fromLen);
int np_close(int fd);

/* server-side and option calls (keep the raw-sockaddr convention) */
int np_bind(int fd, const uint8_t *addr, uint32_t addrLen);
int np_listen(int fd, int backlog);
int np_accept(int fd, uint8_t *from, uint32_t *fromLen);
int np_setsockopt(int fd, int level, int optname,
                  const void *optval, uint32_t optlen);
int np_getsockopt(int fd, int level, int optname,
                  void *optval, uint32_t *optlen);
int np_getsockname(int fd, uint8_t *addr, uint32_t *addrLen);
int np_getpeername(int fd, uint8_t *addr, uint32_t *addrLen);
int np_shutdown(int fd, int how);

/* multiplex wait over master fds; sets may be NULL; timeoutMs -1 blocks,
 * 0 polls once, >0 waits. returns the ready fd count or a negative errno */
int np_select(int nfds, np_fd_set *rdset, np_fd_set *wrset,
              np_fd_set *exset, int timeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* MICA_NET_PROXY_H */
