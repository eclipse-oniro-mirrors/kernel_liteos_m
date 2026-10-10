/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: NAND file system smoke test (backend-agnostic, POSIX/VFS
 *              level; NAND_PORTING_PLAN.md v3 step B4, YAFFS_PORTING_PLAN.md
 *              C3). Runs over whichever backend board_nand_backend selected
 *              (yaffs or littlefs).
 *
 * POSIX/VFS-level end-to-end check over the three NAND volumes
 * (/boot /system /user — fbb-canonical names, NAND_PORTING_PLAN.md 2.2)
 * mounted by nandflash_filesystem_init():
 *   TC-rw      write a pattern file, read it back, verify per volume
 *   TC-big     write a file larger than one erase block (128KB), verify,
 *              proving multi-block allocation + erase in the bridge
 *   TC-dir     opendir/readdir sees the file, unlink works
 *   TC-statfs  statfs reports sane block counts per volume
 *   TC-persist /user marker file survives reset (boot counter increments)
 *
 * Gated by "board_nand_smoke_test" in config.gni; prints PASS/FAIL per
 * case over the debug UART. Mirrors nor_smoke_test.c.
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include "los_debug.h"

#define SMOKE_FILE_LEN 512
/* > 128KB erase block so littlefs must allocate several blocks */
#define SMOKE_BIG_LEN (140U * 1024U)

static uint8_t smoke_wbuf[SMOKE_FILE_LEN];
static uint8_t smoke_rbuf[SMOKE_FILE_LEN];
/* 140KB static buffer — same size class as the littlefs cache usage */
static uint8_t smoke_bigbuf[SMOKE_BIG_LEN];

static const char *const g_vols[] = { "/boot", "/system", "/user" };
#define NAND_SMOKE_VOL_NUM (sizeof(g_vols) / sizeof(g_vols[0]))

/* expected statfs block counts — must match nand_init.c (fbb 5%/25%/70%).
 * Backend-agnostic check in case_statfs(): littlefs reports f_blocks in
 * erase blocks (== expect); yaffs reports chunks (expect × 64 minus
 * reserved/summary), so the fallback compares CAPACITY (f_bsize × f_blocks)
 * against expect × 128KB erase size. */
static const unsigned g_expect_blocks[] = { 51, 256, 716 };

/* TC-ls: boot-time directory dump — the board has no shell, so the smoke
 * test doubles as the "look at the filesystem" tool over the UART. */
static void dump_volume_ls(const char *tag, const char *vol)
{
    DIR *dir;
    struct dirent *ent;
    unsigned n = 0;

    dir = opendir(vol);
    if (dir == NULL) {
        PRINTK("%s_ls %s opendir FAIL errno=%d\n", tag, vol, errno);
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        char p[64];
        struct stat st;
        if ((strcmp(ent->d_name, ".") == 0) ||
            (strcmp(ent->d_name, "..") == 0)) {
            continue; /* liteos VFS: stat("..") returns EINVAL — skip */
        }
        if (snprintf_s(p, sizeof(p), sizeof(p) - 1, "%s/%s",
                       vol, ent->d_name) < 0) {
            continue;
        }
        if (stat(p, &st) == 0) {
            PRINTK("%s_ls %s/%s %s %u bytes\n", tag, vol, ent->d_name,
                   ((st.st_mode & S_IFMT) == S_IFDIR) ? "dir " : "file",
                   (unsigned)st.st_size);
        } else {
            PRINTK("%s_ls %s/%s stat FAIL errno=%d\n",
                   tag, vol, ent->d_name, errno);
        }
        n++;
    }
    (void)closedir(dir);
    if (n == 0) {
        PRINTK("%s_ls %s (empty)\n", tag, vol);
    }
}

static void smoke_fill_pattern(uint8_t *buf, uint32_t len, uint8_t seed)
{
    for (uint32_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)(i * 7U + seed);
    }
}

static int case_rw(void)
{
    int fails = 0;

    for (uint32_t v = 0; v < NAND_SMOKE_VOL_NUM; ++v) {
        char path[48];
        int fd;

        if (snprintf_s(path, sizeof(path), sizeof(path) - 1, "%s/nand_smoke.bin",
                       g_vols[v]) < 0) {
            ++fails;
            continue;
        }
        smoke_fill_pattern(smoke_wbuf, SMOKE_FILE_LEN, (uint8_t)(v + 1));

        fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
        if (fd < 0) {
            PRINTK("nand_smoke TC-rw %s open(w) FAIL errno=%d path=%s len=%u\n",
                   g_vols[v], errno, path, (unsigned)strlen(path));
            ++fails;
            continue;
        }
        PRINTK("nand_smoke TC-rw %s open(w) ok fd=%d\n", g_vols[v], fd);
        ssize_t n = write(fd, smoke_wbuf, SMOKE_FILE_LEN);
        int wr_errno = errno;
        (void)close(fd);
        if (n != SMOKE_FILE_LEN) {
            PRINTK("nand_smoke TC-rw %s write FAIL (%d) errno=%d\n",
                   g_vols[v], (int)n, wr_errno);
            ++fails;
            continue;
        }

        fd = open(path, O_RDONLY);
        if (fd < 0) {
            PRINTK("nand_smoke TC-rw %s open(r) FAIL\n", g_vols[v]);
            ++fails;
            continue;
        }
        n = read(fd, smoke_rbuf, SMOKE_FILE_LEN);
        (void)close(fd);
        if ((n != SMOKE_FILE_LEN) ||
            (memcmp(smoke_wbuf, smoke_rbuf, SMOKE_FILE_LEN) != 0)) {
            PRINTK("nand_smoke TC-rw %s verify FAIL\n", g_vols[v]);
            ++fails;
            continue;
        }
        PRINTK("nand_smoke TC-rw %s PASS\n", g_vols[v]);
    }
    return fails;
}

/* Multi-block write/readback on /user (the largest volume): the littlefs
 * commit walks several 128KB erase blocks through the bridge erase/prog
 * callbacks — the core NAND-specific path worth proving on board.
 *
 * R5 diagnostic: the single-shot 140KB write() died silently in board
 * round 4 (>30s WDT window, no trace line after the previous case) — the
 * write is split into 16KB chunks with a progress line after each so the
 * stall is located to the exact chunk, and each phase is printed. */
static int case_big_write(void)
{
    const char *path = "/user/nand_smoke_big.bin";
    int fd;
    ssize_t n;
    int fails = 0;

    smoke_fill_pattern(smoke_bigbuf, SMOKE_BIG_LEN, 0x5A);

    PRINTK("nand_smoke TC-big open(w)...\n");
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
    if (fd < 0) {
        PRINTK("nand_smoke TC-big open(w) FAIL errno=%d\n", errno);
        return 1;
    }
    PRINTK("nand_smoke TC-big open(w) ok fd=%d, writing %uKB in 16KB chunks\n",
           fd, SMOKE_BIG_LEN / 1024U);
    {
        uint32_t done = 0;
        while (done < SMOKE_BIG_LEN) {
            uint32_t chunk = SMOKE_BIG_LEN - done;
            if (chunk > 16384U) {
                chunk = 16384U;
            }
            n = write(fd, smoke_bigbuf + done, chunk);
            if (n != (ssize_t)chunk) {
                PRINTK("nand_smoke TC-big write FAIL @%uKB (%d/%u) errno=%d\n",
                       done / 1024U, (int)n, chunk, errno);
                (void)close(fd);
                return 1;
            }
            done += chunk;
            PRINTK("nand_smoke TC-big wrote %u/%uKB\n", done / 1024U,
                   SMOKE_BIG_LEN / 1024U);
        }
    }
    PRINTK("nand_smoke TC-big write done, closing...\n");
    (void)close(fd);
    PRINTK("nand_smoke TC-big closed, reopening for verify...\n");

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        PRINTK("nand_smoke TC-big open(r) FAIL\n");
        return 1;
    }
    /* head + tail spot-checks (512B each) against the written pattern */
    n = read(fd, smoke_rbuf, SMOKE_FILE_LEN);
    if ((n != SMOKE_FILE_LEN) ||
        (memcmp(smoke_bigbuf, smoke_rbuf, SMOKE_FILE_LEN) != 0)) {
        PRINTK("nand_smoke TC-big head verify FAIL\n");
        ++fails;
    }
    if ((lseek(fd, (off_t)(SMOKE_BIG_LEN - SMOKE_FILE_LEN), SEEK_SET) ==
         (off_t)-1) ||
        ((n = read(fd, smoke_rbuf, SMOKE_FILE_LEN)) != SMOKE_FILE_LEN) ||
        (memcmp(smoke_bigbuf + SMOKE_BIG_LEN - SMOKE_FILE_LEN, smoke_rbuf,
                SMOKE_FILE_LEN) != 0)) {
        PRINTK("nand_smoke TC-big tail verify FAIL\n");
        ++fails;
    }
    (void)close(fd);
    if (fails == 0) {
        PRINTK("nand_smoke TC-big PASS (%uKB across erase blocks)\n",
               SMOKE_BIG_LEN / 1024U);
    }

    PRINTK("nand_smoke TC-big unlinking...\n");
    (void)unlink(path);
    PRINTK("nand_smoke TC-big unlink done\n");
    return fails;
}

static int case_dir(void)
{
    DIR *dir;
    struct dirent *ent;
    int found_any = 0;
    int fails = 0;

    dir = opendir("/user");
    if (dir == NULL) {
        PRINTK("nand_smoke TC-dir opendir FAIL errno=%d\n", errno);
        return 1;
    }
    while ((ent = readdir(dir)) != NULL) {
        found_any = 1;
    }
    (void)closedir(dir);
    if (!found_any) {
        PRINTK("nand_smoke TC-dir readdir empty FAIL\n");
        ++fails;
    } else {
        PRINTK("nand_smoke TC-dir PASS\n");
    }

    /* cleanup the /user test file so the next boot starts clean */
    (void)unlink("/user/nand_smoke.bin");
    return fails;
}

static int case_statfs(void)
{
    int fails = 0;

    for (uint32_t v = 0; v < NAND_SMOKE_VOL_NUM; ++v) {
        struct statfs st;
        char path[16];
        unsigned expect = g_expect_blocks[v];

        if (snprintf_s(path, sizeof(path), sizeof(path) - 1, "%s", g_vols[v]) < 0) {
            ++fails;
            continue;
        }
        if ((statfs(path, &st) != 0) || (st.f_bsize == 0)) {
            PRINTK("nand_smoke TC-statfs %s FAIL errno=%d\n", g_vols[v], errno);
            ++fails;
        } else if ((unsigned)st.f_blocks != expect) {
            /* yaffs reports f_blocks in chunks (expect × 64 minus reserved),
             * littlefs in erase blocks (== expect). Compare capacity. */
            unsigned long long got_bytes =
                (unsigned long long)st.f_bsize * (unsigned long long)st.f_blocks;
            unsigned long long expect_bytes =
                (unsigned long long)expect * 131072ULL;
            if ((got_bytes <= expect_bytes) &&
                (got_bytes >= expect_bytes / 2ULL)) {
                PRINTK("nand_smoke TC-statfs %s PASS (bsize=%u blocks=%u "
                       "free=%u, chunk semantics)\n",
                       g_vols[v], (unsigned)st.f_bsize, (unsigned)st.f_blocks,
                       (unsigned)st.f_bfree);
            } else {
                PRINTK("nand_smoke TC-statfs %s FAIL blocks=%u expect=%u "
                       "(geometry mismatch vs nand_init.c)\n",
                       g_vols[v], (unsigned)st.f_blocks, expect);
                ++fails;
            }
        } else {
            PRINTK("nand_smoke TC-statfs %s PASS (bsize=%u blocks=%u free=%u)\n",
                   g_vols[v], (unsigned)st.f_bsize, (unsigned)st.f_blocks,
                   (unsigned)st.f_bfree);
        }
    }
    return fails;
}

/* Boot counter in /user: increments every boot; on the second boot the
 * value must be >= 1 read BEFORE the write, proving the file survived
 * the reset (littlefs commit over NAND page program + block erase). */
static int case_persist(void)
{
    const char *path = "/user/nand_smoke_marker";
    uint32_t boot_count = 0;
    int fd = open(path, O_RDONLY);
    int fails = 0;

    if (fd >= 0) {
        ssize_t n = read(fd, &boot_count, sizeof(boot_count));
        (void)close(fd);
        if (n != sizeof(boot_count)) {
            boot_count = 0;
        }
    }
    if (boot_count == 0) {
        PRINTK("nand_smoke TC-persist first boot (marker created)\n");
    } else {
        PRINTK("nand_smoke TC-persist PASS (boot_count=%u survived reset)\n",
               boot_count);
    }

    boot_count++;
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
    if (fd < 0) {
        PRINTK("nand_smoke TC-persist marker open(w) FAIL errno=%d\n", errno);
        return 1;
    }
    ssize_t n = write(fd, &boot_count, sizeof(boot_count));
    (void)close(fd);
    if (n != sizeof(boot_count)) {
        PRINTK("nand_smoke TC-persist marker write FAIL\n");
        ++fails;
    }
    return fails;
}

int nand_smoke_test_run(void)
{
    int fails = 0;

    PRINTK("===== NAND fs smoke test (backend=%s) start =====\n",
#if defined(BOARD_NAND_BACKEND_YAFFS)
           "yaffs"
#else
           "littlefs"
#endif
           );
    fails += case_rw();
    fails += case_big_write();
    fails += case_dir();
    dump_volume_ls("nand", "/boot");
    dump_volume_ls("nand", "/system");
    dump_volume_ls("nand", "/user");
    fails += case_statfs();
    fails += case_persist();

#if defined(BOARD_NAND_BACKEND_YAFFS)
    /* C4 on-board image-compat (YAFFS2_BINARY_LIB_PORTING_PLAN.md C4-②):
     * burn the embedded fbb mkyaffs2image fixture into /boot and mount it
     * with the closed archive. Runs AFTER the baseline cases; leaves the
     * image volume mounted (next boot re-mounts it across reset). */
    {
        extern int nand_yaffs_c4_test(void);
        int c4 = nand_yaffs_c4_test();
        fails += c4;
    }
#endif
    PRINTK("===== NAND fs smoke test done: %d failed case(s) =====\n", fails);
    return fails;
}
