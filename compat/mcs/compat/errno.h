/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Minimal freestanding <errno.h> shim for the LiteOS-M slave core.
 *
 * metal/errno.h falls back to <errno.h> on gcc/clang, and libmetal/libmica
 * return -EINVAL style errors to their callers. The values follow the
 * asm-generic numbering so they are stable across builds; they are only
 * compared among themselves on this freestanding target.
 */

#ifndef MICA_COMPAT_ERRNO_H__
#define MICA_COMPAT_ERRNO_H__

#ifdef __cplusplus
extern "C" {
#endif

#define EPERM            1
#define ENOENT           2
#define ESRCH            3
#define EINTR            4
#define EIO              5
#define ENXIO            6
#define E2BIG            7
#define ENOEXEC          8
#define EBADF            9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34
#define ENOTSUP         95
#define EOPNOTSUPP      ENOTSUP
#define EALREADY       114

#ifdef __cplusplus
}
#endif

#endif /* MICA_COMPAT_ERRNO_H__ */
