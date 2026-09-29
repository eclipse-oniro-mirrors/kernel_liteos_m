/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Minimal freestanding <string.h> shim for the LiteOS-M slave core.
 *
 * The implementations live in the board directory (los_libc.c) and in
 * the securec sources linked into the image; this header only provides
 * the declarations that libmetal/openamp reference.
 */

#ifndef MICA_COMPAT_STRING_H__
#define MICA_COMPAT_STRING_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dst, const void *src, size_t len);
void *memmove(void *dst, const void *src, size_t len);
void *memset(void *dst, int c, size_t len);
int memcmp(const void *a, const void *b, size_t len);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* MICA_COMPAT_STRING_H__ */
