#ifndef PLATFORM_3516_H
#define PLATFORM_3516_H

/* 3516 是 Linux LiteOS-A, 有完整 POSIX:
 *   socket/getaddrinfo/close/openssl/pthread/clock_gettime/getenv/fork/exec
 * 不需要 platform_shim 那种大范围适配, 只需少量定义。
 */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#endif /* PLATFORM_3516_H */
