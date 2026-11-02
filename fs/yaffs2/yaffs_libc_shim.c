/*
 * yaffs_libc_shim.c — link-time symbol floor for the CLOSED prebuilt
 * libyaffs2.a (fbb diting-community, third_party/yaffs2/prebuilt).
 *
 * The archive references libc entry points the liteos_m kernel musl port
 * does not compile in (printf.c / qsort.c are absent from MUSL_LIBC_SRC)
 * plus the fbb-kernel-only ArchExcActive hook. See
 * YAFFS2_BINARY_LIB_PORTING_PLAN.md section 3.2 for the full undefined-
 * symbol audit. malloc/free come from the kernel posix library
 * (lib/posix/src/malloc.c) and are NOT shimmed here.
 *
 * Placement note: this file MUST NOT be compiled with the fmc/compat_fix.h
 * force-include (it #defines dprintf to a shim macro, which would rename
 * the definition below); it lives in this kernel module instead.
 *
 * Signature note: this TU deliberately avoids <stdio.h>/<stdlib.h>. musl
 * declares dprintf(int, const char *, ...) there, while the fbb archive
 * calls dprintf(fmt, ...) — fbb's 1-arg variadic form, confirmed by
 * disassembly of nand_yaffs_read()/yaffsfs_Lock(). Only the symbol name
 * exists at link time, and no other TU in the image defines these symbols
 * (the musl porting compiles none of printf/dprintf/qsort/malloc).
 */
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "los_typedef.h"
#include "los_memory.h"    /* LOS_MemAlloc / LOS_MemFree / OS_SYS_MEM_ADDR */

/* kernel/base/los_printf.c (no public header; same extern as the mmc shim) */
extern VOID LkDprintf(const CHAR *fmt, va_list ap);

/*
 * fbb kernel exception hook: returns nonzero while the exception handler is
 * running, so the closed yaffsfs_Lock() skips locking in dump context.
 * This port never touches the FS from exception context, so the answer is
 * always "not active".
 */
unsigned int ArchExcActive(void)
{
    return 0;
}

#ifndef LOSCFG_LIB_LIBC
/* fbb-style dprintf(fmt, ...) -> kernel console */
int dprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    LkDprintf(fmt, ap);
    va_end(ap);
    return 0;
}
#endif

/* yaffs trace / BUG() path printf */
int printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    LkDprintf(fmt, ap);
    va_end(ap);
    return 0;
}

/*
 * Insertion sort. The archive only sorts small block-index style arrays
 * (yaffs trace/sort paths), so a fixed 32-byte scratch with a conservative
 * bail-out keeps this dependency-free. n*log(n) library sort is not needed
 * at these sizes.
 */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    unsigned char key[32];
    unsigned char *b = (unsigned char *)base;
    size_t i;
    size_t j;

    if ((base == NULL) || (compar == NULL) || (nmemb < 2U) ||
        (size == 0U) || (size > sizeof(key))) {
        return;
    }
    for (i = 1U; i < nmemb; i++) {
        (void)memcpy(key, b + i * size, size);
        j = i;
        while ((j > 0U) && (compar(b + (j - 1U) * size, key) > 0)) {
            (void)memcpy(b + j * size, b + (j - 1U) * size, size);
            j--;
        }
        if (j != i) {
            (void)memcpy(b + j * size, key, size);
        }
    }
}

/*
 * malloc/free are NOT shimmed here: the kernel posix library already
 * provides them (kernel/liteos_m/lib/posix/src/malloc.c — plain
 * LOS_MemAlloc/LOS_MemFree on OS_SYS_MEM_ADDR, i.e. the kernel heap).
 * The closed archive's per-page 2112-byte buffer allocations (mtd_nandcfg
 * nf_read_page/nf_write_page) therefore land on the kernel heap; watch it
 * for [OsMemAlloc] spam during long scans (board bring-up item).
 */
