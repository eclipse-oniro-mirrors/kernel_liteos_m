/*
 * AArch64 cache maintenance (by address / by set-way / icache sync).
 * Ported from hs-fbb arch_lite/arm64/src/cache.S. Needed by OpenAMP
 * shared-memory coherence on RK3568 (no hardware coherency between the
 * Linux cores and the RTOS core in stage-2 optimization).
 */

    .arch armv8-a

#define FUNCTION(x) .global x; .text; x:

#define CACHE_LINE 64

    .macro  cache_range_op, cache, op
    bic     x3, x0, #(CACHE_LINE-1) /* align the start with a cache line */
.Lcache_range_op_loop\@:
    \cache  \op, x3
    add     x3, x3, #CACHE_LINE
    cmp     x3, x1
    blo     .Lcache_range_op_loop\@
    dsb     sy
    .endm

/*
 * set/way operation over all levels of the data cache unified at the
 * point of coherency (CLIDR_EL1.LoC), see ARMv8 B4-82.
 */
    .macro  cache_setway_op, cache, op
    dsb     sy
    mrs     x0, clidr_el1
    ubfx    x2, x0, #24, #3      /* clidr_el1.LoC */
    cmp     x2, #0
    b.eq    arm64_flush_dcache_end\@
    mov     x1, #0               /* x1 = level */

arm64_flush_dcache_by_level\@:
    add     x3, x1, x1, lsl #1   /* x3 = x1*3, Ctype<n> 3bit */
    lsr     x3, x0, x3
    and     x3, x3, #7
    cmp     x3, #2               /* 2: data cache only */
    b.lt    arm64_flush_dcache_next_level\@

    lsl     x4, x1, #1
    msr     csselr_el1, x4       /* set cache level */
    isb

    mrs     x4, ccsidr_el1
    and     x3, x4, #7           /* (Log2(LINELEN)) - 4 */
    add     x3, x3, #4           /* L, line length offset */
    ubfx    x5, x4, #13, #15     /* NumSets - 1 */
    ubfx    x4, x4, #3, #10      /* Associativity - 1 */
    clz     w6, w4               /* (32-A) */
    lsl     x8, x1, #1           /* level field */

arm64_flush_dcache_by_set\@:
    mov     x7, x4
    lsl     x11, x5, x3          /* set Set */
    orr     x9, x8, x11
arm64_flush_dcache_by_way\@:
    lsl     x11, x7, x6          /* set Way */
    orr     x10, x9, x11
    \cache  \op, x10             /* cache maintenance */
    subs    x7, x7, #1           /* way-- */
    b.ge    arm64_flush_dcache_by_way\@
    subs    x5, x5, #1           /* set-- */
    b.ge    arm64_flush_dcache_by_set\@

arm64_flush_dcache_next_level\@:
    add     x1, x1, #1           /* level++ */
    cmp     x2, x1
    b.gt    arm64_flush_dcache_by_level\@

    mov     x0, #0
    msr     csselr_el1, x0       /* restore csselr_el1 */
arm64_flush_dcache_end\@:
    dsb     sy
    isb
    .endm

/*
 * VOID ArchDCacheInvByAddr(UINTPTR start, UINTPTR end)
 * VOID ArchDCacheCleanByAddr(UINTPTR start, UINTPTR end)
 * VOID ArchDCacheFlushByAddr(UINTPTR start, UINTPTR end)
 */
FUNCTION(ArchDCacheInvByAddr)
    cache_range_op dc, ivac
    ret

FUNCTION(ArchDCacheCleanByAddr)
    cache_range_op dc, cvac
    ret

FUNCTION(ArchDCacheFlushByAddr)
    cache_range_op dc, civac
    ret

/*
 * Whole-cache set/way operations.
 */
FUNCTION(ArchDCacheFlush)
    cache_setway_op dc, cisw
    ret

FUNCTION(ArchDCacheClean)
    cache_setway_op dc, csw
    ret

FUNCTION(ArchDCacheInvalidate)
    cache_setway_op dc, isw
    ret

/*
 * VOID ArchICacheFlushByAddr(UINTPTR start, UINTPTR end)
 * Ensure I/D coherency for a region that was just written
 * (e.g. code loaded by remoteproc).
 */
FUNCTION(ArchICacheFlushByAddr)
    mrs     x3, ctr_el0
    ubfx    x3, x3, #16, #4
    mov     x2, #0x4

    lsl     x2, x2, x3
    sub     x3, x2, #0x1
    bic     x4, x0, x3
fir1:
    dc      cvau, x4
    add     x4, x4, x2
    cmp     x4, x1
    b.cc    fir1
    dsb     ish
    mrs     x3, ctr_el0
    and     x3, x3, #0xf
    mov     x2, #0x4

    lsl     x2, x2, x3
    sub     x3, x2, #0x1
    bic     x4, x0, x3
fir2:
    ic      ivau, x4
    add     x4, x4, x2
    cmp     x4, x1
    b.cc    fir2
    dsb     ish
    isb
    mov     x0, #0x0

    ret

/*
 * VOID ArchDcacheFlushAll(VOID) - alias kept for libmetal glue.
 */
FUNCTION(ArchDCacheFlushAll)
    b       ArchDCacheFlush

    .end
