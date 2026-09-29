/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Minimal freestanding <stdio.h> shim for the LiteOS-M slave core.
 *
 * libmetal (log.c) and the libmica tty service (mica_tty_printf) include
 * <stdio.h> and use the printf family. The implementations live in the
 * board directory (los_libc.c); this header only provides declarations.
 */

#ifndef MICA_COMPAT_STDIO_H__
#define MICA_COMPAT_STDIO_H__

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FILE for the <securec.h> declarations (fscanf_s etc.). Same struct tag
 * as the musl porting layer's stdio.h, so the types stay compatible even
 * when both headers are pulled into one TU. */
#ifndef __cplusplus
typedef struct _IO_FILE FILE;
#endif

int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);
int printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* MICA_COMPAT_STDIO_H__ */
