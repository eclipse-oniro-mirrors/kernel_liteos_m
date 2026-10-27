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
 * pfs_* proxy filesystem + np_system proxy shell: mica_rpc_call wrappers.
 * The master netproxy worker executes the real file syscalls; fd values
 * are master-side fds and stay opaque here. See include/mica/pfs.h and
 * rpc_proto.h.
 */

#include <errno.h>
#include <string.h>
#include "mica/rpc.h"
#include "mica/rpc_proto.h"
#include "mica/pfs.h"

/* path length cap: must fit one rpc message beside the fixed args */
#define PFS_PATH_MAX 256

static int PfsCheckPath(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return -EINVAL;
    }
    if (strlen(path) >= PFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    return 0;
}

int pfs_open(const char *path, int flags, uint32_t mode)
{
    struct rpc_pf_open_args args;
    int ret = PfsCheckPath(path);

    if (ret != 0) {
        return ret;
    }
    args.flags = flags;
    args.mode = mode;
    /* args + NUL-terminated path packed in one message */
    {
        uint8_t msg[sizeof(args) + PFS_PATH_MAX];
        uint32_t len = (uint32_t)strlen(path) + 1;

        memcpy(msg, &args, sizeof(args));
        memcpy(msg + sizeof(args), path, len);
        return mica_rpc_call(RPC_FUNC_PF_OPEN, msg, sizeof(args) + len,
                             NULL, 0, NULL, -1);
    }
}

int pfs_close(int fd)
{
    struct rpc_pf_close_args args = { fd };

    return mica_rpc_call(RPC_FUNC_PF_CLOSE, &args, sizeof(args),
                         NULL, 0, NULL, -1);
}

int pfs_read(int fd, void *buf, uint32_t count)
{
    struct rpc_pf_read_args args = { fd, count };
    uint32_t got = 0;
    int ret;

    if (count > 0 && buf == NULL) {
        return -EINVAL;
    }
    ret = mica_rpc_call(RPC_FUNC_PF_READ, &args, sizeof(args),
                        buf, count, &got, -1);
    if (ret < 0) {
        return ret;
    }
    return (int)got;
}

int pfs_write(int fd, const void *buf, uint32_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t total = 0;
    int ret = 0;

    if (count > 0 && buf == NULL) {
        return -EINVAL;
    }
    while (count > 0) {
        uint8_t msg[RPC_MAX_PAYLOAD];
        struct rpc_pf_write_args *a = (struct rpc_pf_write_args *)msg;
        uint32_t chunk = RPC_MAX_PAYLOAD - sizeof(*a);
        if (chunk > count) {
            chunk = count;
        }
        a->fd = fd;
        a->len = chunk;
        memcpy(msg + sizeof(*a), p, chunk);
        ret = mica_rpc_call(RPC_FUNC_PF_WRITE, msg, sizeof(*a) + chunk,
                            NULL, 0, NULL, -1);
        if (ret < 0) {
            return (total > 0) ? (int)total : ret;
        }
        total += (uint32_t)ret;
        p += chunk;
        count -= chunk;
    }
    return (int)total;
}

int64_t pfs_lseek(int fd, int64_t offset, int whence)
{
    struct rpc_pf_lseek_args args = { fd, whence, 0, offset };
    int64_t pos = 0;
    int ret = mica_rpc_call(RPC_FUNC_PF_LSEEK, &args, sizeof(args),
                            &pos, sizeof(pos), NULL, -1);

    return ret < 0 ? ret : pos;
}

int pfs_fcntl(int fd, int cmd, uint32_t arg)
{
    struct rpc_pf_fcntl_args args = { fd, cmd, arg };

    return mica_rpc_call(RPC_FUNC_PF_FCNTL, &args, sizeof(args),
                         NULL, 0, NULL, -1);
}

/* UNLINK / REMOVE / RMDIR / CHDIR: NUL-terminated path as the whole
 * payload; Mkdir/Access carry a leading mode word. */
static int PfsPathCall(uint32_t funcId, const char *path)
{
    int ret = PfsCheckPath(path);

    if (ret != 0) {
        return ret;
    }
    return mica_rpc_call(funcId, path, (uint32_t)strlen(path) + 1,
                         NULL, 0, NULL, -1);
}

int pfs_unlink(const char *path)
{
    return PfsPathCall(RPC_FUNC_PF_UNLINK, path);
}

int pfs_remove(const char *path)
{
    return PfsPathCall(RPC_FUNC_PF_REMOVE, path);
}

int pfs_rmdir(const char *path)
{
    return PfsPathCall(RPC_FUNC_PF_RMDIR, path);
}

int pfs_chdir(const char *path)
{
    return PfsPathCall(RPC_FUNC_PF_CHDIR, path);
}

static int PfsModePathCall(uint32_t funcId, uint32_t mode, const char *path)
{
    struct rpc_pf_mode_args args;
    uint8_t msg[sizeof(args) + PFS_PATH_MAX];
    uint32_t len;
    int ret = PfsCheckPath(path);

    if (ret != 0) {
        return ret;
    }
    len = (uint32_t)strlen(path) + 1;
    args.mode = mode;
    memcpy(msg, &args, sizeof(args));
    memcpy(msg + sizeof(args), path, len);
    return mica_rpc_call(funcId, msg, sizeof(args) + len,
                         NULL, 0, NULL, -1);
}

int pfs_mkdir(const char *path, uint32_t mode)
{
    return PfsModePathCall(RPC_FUNC_PF_MKDIR, mode, path);
}

int pfs_access(const char *path, int mode)
{
    return PfsModePathCall(RPC_FUNC_PF_ACCESS, (uint32_t)mode, path);
}

/* STAT / FSTAT share the compact-stat response */
static int PfsStatFill(struct pfs_stat *st, const void *resp, uint32_t len)
{
    const struct rpc_pf_stat_resp *r = (const struct rpc_pf_stat_resp *)resp;

    if (len < sizeof(*r)) {
        return -EIO;
    }
    if (st != NULL) {
        st->size = r->st_size;
        st->mode = r->st_mode;
        st->blksize = r->st_blksize;
    }
    return 0;
}

int pfs_stat(const char *path, struct pfs_stat *st)
{
    struct rpc_pf_stat_resp resp;
    uint32_t got = 0;
    int ret = PfsCheckPath(path);

    if (ret != 0) {
        return ret;
    }
    ret = mica_rpc_call(RPC_FUNC_PF_STAT, path, (uint32_t)strlen(path) + 1,
                        &resp, sizeof(resp), &got, -1);
    if (ret < 0) {
        return ret;
    }
    return PfsStatFill(st, &resp, got);
}

int pfs_fstat(int fd, struct pfs_stat *st)
{
    struct rpc_pf_fstat_args args = { fd };
    struct rpc_pf_stat_resp resp;
    uint32_t got = 0;
    int ret = mica_rpc_call(RPC_FUNC_PF_FSTAT, &args, sizeof(args),
                            &resp, sizeof(resp), &got, -1);

    if (ret < 0) {
        return ret;
    }
    return PfsStatFill(st, &resp, got);
}

int pfs_rename(const char *oldPath, const char *newPath)
{
    uint8_t msg[PFS_PATH_MAX * 2];
    uint32_t oldLen;
    uint32_t newLen;
    int ret = PfsCheckPath(oldPath);

    if (ret != 0) {
        return ret;
    }
    ret = PfsCheckPath(newPath);
    if (ret != 0) {
        return ret;
    }
    oldLen = (uint32_t)strlen(oldPath) + 1;
    newLen = (uint32_t)strlen(newPath) + 1;
    memcpy(msg, oldPath, oldLen);
    memcpy(msg + oldLen, newPath, newLen);
    return mica_rpc_call(RPC_FUNC_PF_RENAME, msg, oldLen + newLen,
                         NULL, 0, NULL, -1);
}

int pfs_getcwd(char *buf, uint32_t size)
{
    struct rpc_pf_getcwd_args args = { size };
    uint32_t got = 0;
    int ret;

    if (buf == NULL || size == 0) {
        return -EINVAL;
    }
    ret = mica_rpc_call(RPC_FUNC_PF_GETCWD, &args, sizeof(args),
                        buf, size, &got, -1);
    if (ret < 0) {
        return ret;
    }
    if (got == 0 || got > size) {
        return -ERANGE;
    }
    /* keep it NUL-terminated even against a sloppy master */
    buf[got - 1] = '\0';
    return (int)got - 1;
}

int pfs_getdents(int fd, void *buf, uint32_t count)
{
    struct rpc_pf_getdents_args args = { fd, count };
    uint32_t got = 0;
    int ret;

    if (count > 0 && buf == NULL) {
        return -EINVAL;
    }
    ret = mica_rpc_call(RPC_FUNC_PF_GETDENTS, &args, sizeof(args),
                        buf, count, &got, -1);
    if (ret < 0) {
        return ret;
    }
    return (int)got;
}

int np_system(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') {
        return -EINVAL;
    }
    if (strlen(cmd) >= RPC_MAX_PAYLOAD) {
        return -ENAMETOOLONG;
    }
    return mica_rpc_call(RPC_FUNC_SYSTEM, cmd, (uint32_t)strlen(cmd) + 1,
                         NULL, 0, NULL, -1);
}
