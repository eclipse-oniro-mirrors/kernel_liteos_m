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
 * pfs_*: proxy filesystem API. File operations are forwarded to the master
 * netproxy worker over the mica rpc channel and executed on the master's
 * Linux filesystem. The fd values are master-side fds and stay opaque
 * here. Paths are master-side absolute or cwd-relative paths.
 *
 * Flags/whence/fcntl-cmd values come from the local fcntl.h and match the
 * Linux ABI (musl-compatible), so O_RDONLY/O_CREAT/SEEK_SET/F_GETFL/...
 * can be used directly.
 */

#ifndef MICA_PFS_H
#define MICA_PFS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* compact stat: the fields a slave-side program typically needs */
struct pfs_stat {
    uint64_t size;
    uint32_t mode;    /* Linux st_mode (S_IFREG 0x8000, S_IFDIR 0x4000, ...) */
    uint32_t blksize;
};

/* All calls block until the master worker answers. On failure the master
 * errno is returned negated (e.g. -2 for ENOENT). */
int pfs_open(const char *path, int flags, uint32_t mode);
int pfs_close(int fd);
int pfs_read(int fd, void *buf, uint32_t count);
int pfs_write(int fd, const void *buf, uint32_t count);
int64_t pfs_lseek(int fd, int64_t offset, int whence);
int pfs_fcntl(int fd, int cmd, uint32_t arg);

int pfs_unlink(const char *path);
int pfs_stat(const char *path, struct pfs_stat *st);
int pfs_fstat(int fd, struct pfs_stat *st);
int pfs_rename(const char *oldPath, const char *newPath);
int pfs_remove(const char *path);
int pfs_mkdir(const char *path, uint32_t mode);
int pfs_rmdir(const char *path);
int pfs_chdir(const char *path);
int pfs_getcwd(char *buf, uint32_t size);
int pfs_access(const char *path, int mode);

/* raw Linux getdents64(2) layout: use PfsGetdentsNext() to walk entries */
struct pfs_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

int pfs_getdents(int fd, void *buf, uint32_t count);

/* helper: return the next entry in a getdents64 buffer, advance *pos;
 * returns NULL at the end of the buffer */
static inline const struct pfs_dirent64 *PfsGetdentsNext(const void *buf,
                                                         uint32_t bufLen,
                                                         uint32_t *pos)
{
    const struct pfs_dirent64 *d;

    if (buf == NULL || pos == NULL || *pos + sizeof(*d) > bufLen) {
        return NULL;
    }
    d = (const struct pfs_dirent64 *)((const uint8_t *)buf + *pos);
    if (d->d_reclen < sizeof(*d) || *pos + d->d_reclen > bufLen) {
        return NULL;
    }
    *pos += d->d_reclen;
    return d;
}

/* proxy shell: run a command on the master with system() (output goes to
 * the master console); returns the system() exit status */
int np_system(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* MICA_PFS_H */
