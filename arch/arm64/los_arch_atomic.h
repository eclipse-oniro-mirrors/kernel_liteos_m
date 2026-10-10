/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * AArch64 atomics (LDXR/STXR exclusive sequence), following the
 * los_arch_atomic.h contract shared with cortex_a_r.
 */

#ifndef _LOS_ARCH_ATOMIC_H
#define _LOS_ARCH_ATOMIC_H

#include "los_compiler.h"
#include "los_interrupt.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

STATIC INLINE INT32 ArchAtomicRead(const Atomic *v)
{
    INT32 val;

    __asm__ __volatile__("ldxr %w0, [%1]"
                         : "=&r"(val)
                         : "r"(v)
                         : "cc");
    return val;
}

STATIC INLINE VOID ArchAtomicSet(Atomic *v, INT32 setVal)
{
    INT32 tmp;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %w0, [%3]\n"
                             "stxr %w1, %w2, [%3]"
                             : "=&r"(tmp), "=&r"(status)
                             : "r"(setVal), "r"(v)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));
}

STATIC INLINE INT32 ArchAtomicAdd(Atomic *v, INT32 addVal)
{
    INT32 val;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %w0, [%2]\n"
                             "add %w0, %w0, %w3\n"
                             "stxr %w1, %w0, [%2]"
                             : "=&r"(val), "=&r"(status)
                             : "r"(v), "r"(addVal)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return val;
}

STATIC INLINE INT32 ArchAtomicSub(Atomic *v, INT32 subVal)
{
    INT32 val;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %w0, [%2]\n"
                             "sub %w0, %w0, %w3\n"
                             "stxr %w1, %w0, [%2]"
                             : "=&r"(val), "=&r"(status)
                             : "r"(v), "r"(subVal)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return val;
}

STATIC INLINE VOID ArchAtomicInc(Atomic *v)
{
    (VOID)ArchAtomicAdd(v, 1);
}

STATIC INLINE VOID ArchAtomicDec(Atomic *v)
{
    (VOID)ArchAtomicSub(v, 1);
}

STATIC INLINE INT32 ArchAtomicIncRet(Atomic *v)
{
    return ArchAtomicAdd(v, 1);
}

STATIC INLINE INT32 ArchAtomicDecRet(Atomic *v)
{
    return ArchAtomicSub(v, 1);
}

STATIC INLINE INT32 ArchAtomicXchg32bits(Atomic *v, INT32 val)
{
    INT32 prevVal;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %w0, [%2]\n"
                             "stxr %w1, %w3, [%2]"
                             : "=&r"(prevVal), "=&r"(status)
                             : "r"(v), "r"(val)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return prevVal;
}

/* Returns TRUE when the swap did NOT happen (previous value differed). */
STATIC INLINE BOOL ArchAtomicCmpXchg32bits(Atomic *v, INT32 val, INT32 oldVal)
{
    INT32 prevVal = 0;
    UINT32 status = 0;

    do {
        __asm__ __volatile__("ldxr %w0, [%2]\n"
                             "mov %w1, #0\n"
                             "cmp %w0, %w3\n"
                             "bne 1f\n"
                             "stxr %w1, %w4, [%2]\n"
                             "1:"
                             : "=&r"(prevVal), "=&r"(status)
                             : "r"(v), "r"(oldVal), "r"(val)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return prevVal != oldVal;
}

STATIC INLINE INT64 ArchAtomic64Read(const Atomic64 *v)
{
    INT64 val;

    __asm__ __volatile__("ldxr %0, [%1]"
                         : "=&r"(val)
                         : "r"(v)
                         : "cc");
    return val;
}

STATIC INLINE VOID ArchAtomic64Set(Atomic64 *v, INT64 setVal)
{
    INT64 tmp;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %0, [%3]\n"
                             "stxr %w1, %2, [%3]"
                             : "=&r"(tmp), "=&r"(status)
                             : "r"(setVal), "r"(v)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));
}

STATIC INLINE INT64 ArchAtomic64Add(Atomic64 *v, INT64 addVal)
{
    INT64 val;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %0, [%2]\n"
                             "add %0, %0, %3\n"
                             "stxr %w1, %0, [%2]"
                             : "=&r"(val), "=&r"(status)
                             : "r"(v), "r"(addVal)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return val;
}

STATIC INLINE INT64 ArchAtomic64Sub(Atomic64 *v, INT64 subVal)
{
    INT64 val;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %0, [%2]\n"
                             "sub %0, %0, %3\n"
                             "stxr %w1, %0, [%2]"
                             : "=&r"(val), "=&r"(status)
                             : "r"(v), "r"(subVal)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return val;
}

STATIC INLINE VOID ArchAtomic64Inc(Atomic64 *v)
{
    (VOID)ArchAtomic64Add(v, 1);
}

STATIC INLINE INT64 ArchAtomic64IncRet(Atomic64 *v)
{
    return ArchAtomic64Add(v, 1);
}

STATIC INLINE VOID ArchAtomic64Dec(Atomic64 *v)
{
    (VOID)ArchAtomic64Sub(v, 1);
}

STATIC INLINE INT64 ArchAtomic64DecRet(Atomic64 *v)
{
    return ArchAtomic64Sub(v, 1);
}

STATIC INLINE INT64 ArchAtomicXchg64bits(Atomic64 *v, INT64 val)
{
    INT64 prevVal;
    UINT32 status;

    do {
        __asm__ __volatile__("ldxr %0, [%2]\n"
                             "stxr %w1, %3, [%2]"
                             : "=&r"(prevVal), "=&r"(status)
                             : "r"(v), "r"(val)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return prevVal;
}

/* Returns TRUE when the swap did NOT happen (previous value differed). */
STATIC INLINE BOOL ArchAtomicCmpXchg64bits(Atomic64 *v, INT64 val, INT64 oldVal)
{
    INT64 prevVal = 0;
    UINT32 status = 0;

    do {
        __asm__ __volatile__("ldxr %0, [%2]\n"
                             "mov %w1, #0\n"
                             "cmp %0, %3\n"
                             "bne 1f\n"
                             "stxr %w1, %4, [%2]\n"
                             "1:"
                             : "=&r"(prevVal), "=&r"(status)
                             : "r"(v), "r"(oldVal), "r"(val)
                             : "cc");
    } while (__builtin_expect(status != 0, 0));

    return prevVal != oldVal;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_ARCH_ATOMIC_H */
