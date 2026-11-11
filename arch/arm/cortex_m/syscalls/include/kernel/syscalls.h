#ifndef _SYSCALL_H
#define _SYSCALL_H

/*
 * BOX syscall rename - kernel side. Same shape and location as refs
 * arch_s/arm/cortex_m/syscalls/include/kernel/syscalls.h.
 *
 * Reached via los_base.h (`#ifdef LOSCFG_BOX` + `__has_include`):
 * kernel compiles see
 *   LOS_xxx  ->  SYS_LOS_xxx
 * for every syscall-exposed API. The kernel implementation (defined in
 * kernel sources as LOS_xxx) and ALL internal call sites become direct
 * references to the SYS_ implementation - kernel threads call it
 * directly, no svc, no veneer.
 *
 * User code never wants this mapping: box usr modules compile with the
 * syscalls/include/usr root only, which carries no kernel/ subdir, so
 * the __has_include in los_base.h is FALSE there and this header is
 * unreachable from the user world.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOS_DYN_SYSCALL_LIB
#define PREFIX DYNSYSCALL
#else
#define PREFIX(f) SYS_##f
#endif

/* task system call */
#define LOS_TaskDelay                 PREFIX(LOS_TaskDelay)
#define LOS_PrintStr                  PREFIX(LOS_PrintStr)
#define LOS_TaskUsrExit               PREFIX(LOS_TaskUsrExit)
#define LOS_OffsetTest                PREFIX(LOS_OffsetTest)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SYSCALL_H */
