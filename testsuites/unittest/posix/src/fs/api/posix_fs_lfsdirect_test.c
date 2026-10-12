/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 本文件为 littlefs 核心(third_party/littlefs/lfs.c)与 RAM 块设备(bd/lfs_rambd.c)
 * 的直接 API 测试(LITE_COMPLIANT 库级单测, 不经过 VFS 适配层)。
 * 背景: VFS 适配层(lfs_adapter.c)未接线 truncate/tell/rewind/dir_seek/attrs/
 * fs_stat/fs_size/fs_traverse/fs_gc/fs_grow/mkconsistent/opencfg 等 littlefs
 * 接口, 这些代码无法通过 POSIX 对外接口驱动; 本文件按上游官方用法
 * (lfs_* + lfs_rambd)直接测试, 用例设计引用上游开源测试套:
 *   - test_truncate.toml   : 截断收缩(inline 回退/ctz)/扩展(零填充)/INVAL
 *   - test_seek.toml       : tell/rewind/size/seek 组合
 *   - test_files.toml      : opencfg 静态缓冲/默认 malloc 缓冲
 *   - test_dirs.toml       : dir_tell/dir_seek/dir_rewind/EOF/越界 seek
 *   - test_attrs.toml      : setattr/getattr/overwrite/removeattr/NOATTR/超限
 *   - test_superblocks.toml: lfs_fs_stat 几何与版本字段
 *   - test_interspersed    : lfs_fs_size 增长与 lfs_fs_traverse 遍历去重
 *   - test_shrink(grow)    : lfs_fs_grow 等容/扩容/跨旧边界写/持久化
 *
 * 每个用例完全自包含: rambd 创建 -> format -> mount -> 操作 -> unmount -> destroy,
 * 不触碰 VFS 全局状态与 /littlefs 挂载。
 * 文件内容受 LOSCFG_SUPPORT_LITTLEFS 门控: 未启用 littlefs 的配置(如 FATFS-only)
 * 下编译为空桩, 不引用 lfs_* 与 lfs_rambd_* 符号, 与 rambd 专用库的 GN 级门控配套。
 */

#include "posix_fs_test.h"
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
#include "lfs.h"
#include "lfs_rambd.h"

#define DIRECT_ERASE_SIZE       512     /* 块大小(小块几何) */
#define DIRECT_MAX_BLOCKS       64      /* 静态缓冲按最大几何: 64*512=32KB */
#define DIRECT_BLOCKS           32      /* 常规几何 32 块 */
#define DIRECT_IO_SIZE          64      /* read/prog/cache 尺寸 */
#define DIRECT_BUF_LEN          1024
#define DIRECT_FILE_NUM         5       /* 目录定位用例的文件数 */

/* lfs_rambd 与 lfs 实例(全部静态, 避免占用任务栈) */
static uint8_t g_directRambdBuf[DIRECT_MAX_BLOCKS * DIRECT_ERASE_SIZE];
static struct lfs_rambd_config g_directBdCfg;
static lfs_rambd_t g_directRambd;
static struct lfs_config g_directCfg;
static lfs_t g_directLfs;

/* 创建 rambd 并(可选)格式化+挂载, 返回 0 成功 */
static int LfsDirectSetup(bool format, lfs_size_t blockCount)
{
    int err;

    (void)memset_s(&g_directBdCfg, sizeof(g_directBdCfg), 0, sizeof(g_directBdCfg));
    g_directBdCfg.read_size = DIRECT_IO_SIZE;
    g_directBdCfg.prog_size = DIRECT_IO_SIZE;
    g_directBdCfg.erase_size = DIRECT_ERASE_SIZE;
    g_directBdCfg.erase_count = blockCount;
    g_directBdCfg.buffer = g_directRambdBuf; /* 静态缓冲, 与静态文件缓冲等价的上游用法 */

    (void)memset_s(&g_directCfg, sizeof(g_directCfg), 0, sizeof(g_directCfg));
    g_directCfg.context = &g_directRambd;
    g_directCfg.read = lfs_rambd_read;
    g_directCfg.prog = lfs_rambd_prog;
    g_directCfg.erase = lfs_rambd_erase;
    g_directCfg.sync = lfs_rambd_sync;
    g_directCfg.read_size = DIRECT_IO_SIZE;
    g_directCfg.prog_size = DIRECT_IO_SIZE;
    g_directCfg.block_size = DIRECT_ERASE_SIZE;
    g_directCfg.block_count = blockCount;
    g_directCfg.cache_size = DIRECT_IO_SIZE;
    g_directCfg.lookahead_size = 8;   /* 8: 前瞻缓冲字数 */
    g_directCfg.block_cycles = 500;   /* 500: 默认磨损阈值 */

    err = lfs_rambd_create(&g_directCfg, &g_directBdCfg);
    if (err != 0) {
        return err;
    }
    if (format) {
        err = lfs_format(&g_directLfs, &g_directCfg);
        if (err != 0) {
            return err;
        }
    }
    return lfs_mount(&g_directLfs, &g_directCfg);
}

static void LfsDirectTeardown(void)
{
    (void)lfs_unmount(&g_directLfs);
    (void)lfs_rambd_destroy(&g_directCfg);
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_TRUNCATE
 * @tc.name     LfsDirectTruncate
 * @tc.desc     引用上游 test_truncate.toml: 截断收缩(ctz/inline 回退)/扩展零填充/INVAL
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectTruncate, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    char buf[DIRECT_BUF_LEN];

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 创建 600 字节文件(超出 inline 上限, 走 ctz 跳表) */
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    (void)memset_s(buf, sizeof(buf), 'T', sizeof(buf));
    for (int i = 0; i < 600; i += DIRECT_BUF_LEN) { /* 600 字节分块写 */
        lfs_size_t chunk = (600 - i > DIRECT_BUF_LEN) ? DIRECT_BUF_LEN : (600 - i);
        err = lfs_file_write(&g_directLfs, &file, buf, chunk);
        ICUNIT_ASSERT_EQUAL(err, (int)chunk, err);
    }
    err = lfs_file_truncate(&g_directLfs, &file, 300); /* 300: 仍大于 inline 上限, ctz 收缩分支 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 收缩到 inline 上限内: 回退为内联文件分支 */
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_WRONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_truncate(&g_directLfs, &file, 10); /* 10: 小于 inline 上限 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 校验收缩结果: 300 字节收缩后再 10 字节, 前缀保持 'T' */
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_size(&g_directLfs, &file), 10, -1);
    err = lfs_file_read(&g_directLfs, &file, buf, 10);
    ICUNIT_ASSERT_EQUAL(err, 10, err);
    ICUNIT_ASSERT_EQUAL(buf[0], 'T', buf[0]);

    /* 扩展截断: 以零填充(上游语义) */
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_WRONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_truncate(&g_directLfs, &file, 100); /* 100: 扩展 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_read(&g_directLfs, &file, buf, 100);
    ICUNIT_ASSERT_EQUAL(err, 100, err);
    ICUNIT_ASSERT_EQUAL(buf[0], 'T', buf[0]);
    ICUNIT_ASSERT_EQUAL(buf[10], 0, buf[10]); /* 扩展区零填充 */
    ICUNIT_ASSERT_EQUAL(buf[99], 0, buf[99]);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 非法长度: 超过 LFS_FILE_MAX */
    err = lfs_file_open(&g_directLfs, &file, "tf", LFS_O_WRONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_truncate(&g_directLfs, &file, (lfs_off_t)LFS_FILE_MAX + 1);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_remove(&g_directLfs, "tf");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_FILE_POS
 * @tc.name     LfsDirectFilePos
 * @tc.desc     引用上游 test_seek.toml: tell/rewind/size/seek 组合
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectFilePos, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    char buf[DIRECT_BUF_LEN];
    const char *msg = "kittycatcat"; /* 上游用例同名测试数据, 11 字节 */

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 追加写 132 次(上游 COUNT): 过程中 tell 随写入推进 */
    err = lfs_file_open(&g_directLfs, &file, "pf", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (int i = 0; i < 132; i++) { /* 132: 上游 test_seek COUNT */
        err = lfs_file_write(&g_directLfs, &file, msg, strlen(msg));
        ICUNIT_ASSERT_EQUAL(err, (int)strlen(msg), err);
        if (i == 0) {
            ICUNIT_ASSERT_EQUAL((int)lfs_file_tell(&g_directLfs, &file), (int)strlen(msg), -1);
        }
    }
    ICUNIT_ASSERT_EQUAL((int)lfs_file_tell(&g_directLfs, &file), 132 * (int)strlen(msg), -1);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_size(&g_directLfs, &file), 132 * (int)strlen(msg), -1);

    /* rewind: 位置归零, 尺寸不变 */
    err = lfs_file_rewind(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_tell(&g_directLfs, &file), 0, -1);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_size(&g_directLfs, &file), 132 * (int)strlen(msg), -1);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 只读打开: seek/tell 组合, 越界 seek INVAL */
    err = lfs_file_open(&g_directLfs, &file, "pf", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = (int)lfs_file_seek(&g_directLfs, &file, 100, LFS_SEEK_SET);
    ICUNIT_ASSERT_EQUAL(err, 100, err);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_tell(&g_directLfs, &file), 100, -1);
    err = (int)lfs_file_seek(&g_directLfs, &file, -(lfs_soff_t)strlen(msg), LFS_SEEK_CUR);
    ICUNIT_ASSERT_EQUAL(err, 100 - (int)strlen(msg), err);
    err = (int)lfs_file_seek(&g_directLfs, &file, -(lfs_soff_t)strlen(msg), LFS_SEEK_END);
    ICUNIT_ASSERT_EQUAL(err, 132 * (int)strlen(msg) - (int)strlen(msg), err);
    err = lfs_file_read(&g_directLfs, &file, buf, strlen(msg));
    ICUNIT_ASSERT_EQUAL(err, (int)strlen(msg), err);
    err = memcmp(buf, msg, strlen(msg));
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = (int)lfs_file_seek(&g_directLfs, &file, -1, LFS_SEEK_SET);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_remove(&g_directLfs, "pf");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_OPENCFG
 * @tc.name     LfsDirectOpenCfg
 * @tc.desc     引用上游 test_files.toml(buffer 变体): opencfg 静态缓冲与默认 malloc 缓冲
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectOpenCfg, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    static uint8_t fileBuf[DIRECT_IO_SIZE]; /* 静态文件缓冲, 尺寸=cache_size */
    struct lfs_file_config fcfg = {0};
    const char *msg = "Hello World!"; /* 上游用例同名测试数据 */

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* opencfg + 静态缓冲写 */
    fcfg.buffer = fileBuf;
    fcfg.attrs = NULL;
    fcfg.attr_count = 0;
    err = lfs_file_opencfg(&g_directLfs, &file, "cf", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_write(&g_directLfs, &file, msg, strlen(msg) + 1);
    ICUNIT_ASSERT_EQUAL(err, (int)strlen(msg) + 1, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* opencfg + 全零配置(等价 lfs_file_open_ 内部的 defaults, 缓冲走 malloc):
     * 注意本版本 opencfg 的 cfg 参数不允许 NULL(内部无条件解引用 file->cfg) */
    struct lfs_file_config defcfg = {0};
    err = lfs_file_opencfg(&g_directLfs, &file, "cf", LFS_O_RDONLY, &defcfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    char buf[32]; /* 32: 读缓冲 */
    err = lfs_file_read(&g_directLfs, &file, buf, strlen(msg) + 1);
    ICUNIT_ASSERT_EQUAL(err, (int)strlen(msg) + 1, err);
    err = strcmp(buf, msg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* O_CREAT|O_EXCL 已存在: LFS_ERR_EXIST */
    err = lfs_file_opencfg(&g_directLfs, &file, "cf", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_EXIST, err);

    err = lfs_remove(&g_directLfs, "cf");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_DIR_POS
 * @tc.name     LfsDirectDirPos
 * @tc.desc     引用上游 test_dirs.toml: dir_tell/dir_seek/dir_rewind/EOF/越界 seek
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectDirPos, Function | MediumTest | Level1)
{
    int err;
    int i;
    lfs_file_t file;
    lfs_dir_t dir;
    struct lfs_info info;
    char name[32]; /* 32: 文件名缓冲 */
    char nameAt3[32]; /* 32: 记录第 3 个条目名 */
    lfs_off_t tellAt3 = 0;

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_mkdir(&g_directLfs, "dp");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (i = 0; i < DIRECT_FILE_NUM; i++) {
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1, "dp/f%d", i);
        err = lfs_file_open(&g_directLfs, &file, name, LFS_O_WRONLY | LFS_O_CREAT);
        ICUNIT_ASSERT_EQUAL(err, 0, err);
        err = lfs_file_close(&g_directLfs, &file);
        ICUNIT_ASSERT_EQUAL(err, 0, err);
    }

    /* 顺序读: . .. f0 f1 f2 ... 记录第 3 个条目(f2)的 tell */
    err = lfs_dir_open(&g_directLfs, &dir, "dp");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (i = 0; i < 3; i++) { /* 前 3 个条目: . .. f0 */
        err = lfs_dir_read(&g_directLfs, &dir, &info);
        ICUNIT_ASSERT_EQUAL(err, 1, err);
    }
    err = lfs_dir_read(&g_directLfs, &dir, &info); /* 第 4 个条目: f1 */
    ICUNIT_ASSERT_EQUAL(err, 1, err);
    tellAt3 = lfs_dir_tell(&g_directLfs, &dir);
    ICUNIT_ASSERT_NOT_EQUAL((int)tellAt3, 0, -1);
    /* tell 为"已读位置": 其后读取的下一个条目是 seek 恢复后的期望值 */
    err = lfs_dir_read(&g_directLfs, &dir, &info); /* 第 5 个条目: f2 */
    ICUNIT_ASSERT_EQUAL(err, 1, err);
    (void)strcpy_s(nameAt3, sizeof(nameAt3), info.name);

    /* 读到 EOF(err==0) */
    while ((err = lfs_dir_read(&g_directLfs, &dir, &info)) == 1) {
    }
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* seek 回记录点: 后续读取重现记录时的下一条目 */
    err = lfs_dir_seek(&g_directLfs, &dir, tellAt3);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_dir_read(&g_directLfs, &dir, &info);
    ICUNIT_ASSERT_EQUAL(err, 1, err);
    err = strcmp(info.name, nameAt3);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* rewind: 位置归 0, 重读第一个条目 '.' */
    err = lfs_dir_rewind(&g_directLfs, &dir);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)lfs_dir_tell(&g_directLfs, &dir), 0, -1);
    err = lfs_dir_read(&g_directLfs, &dir, &info);
    ICUNIT_ASSERT_EQUAL(err, 1, err);
    err = strcmp(info.name, ".");
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 越界 seek: 目录无 split 且超出条目数, LFS_ERR_INVAL */
    err = lfs_dir_seek(&g_directLfs, &dir, 9999); /* 9999: 远超条目数 */
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);

    err = lfs_dir_close(&g_directLfs, &dir);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    for (i = 0; i < DIRECT_FILE_NUM; i++) {
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1, "dp/f%d", i);
        (void)lfs_remove(&g_directLfs, name);
    }
    err = lfs_remove(&g_directLfs, "dp");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_ATTRS
 * @tc.name     LfsDirectAttrs
 * @tc.desc     引用上游 test_attrs.toml: set/get/overwrite/remove + NOATTR/NOENT/超限
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectAttrs, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    char buf[64]; /* 64: 属性读缓冲 */

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_file_open(&g_directLfs, &file, "af", LFS_O_WRONLY | LFS_O_CREAT);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 创建属性 */
    err = lfs_setattr(&g_directLfs, "af", 0x01, "data", 4); /* 4 字节属性 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    /* 读回属性 */
    err = lfs_getattr(&g_directLfs, "af", 0x01, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, 4, err);
    err = memcmp(buf, "data", 4);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    /* 覆盖同名属性 */
    err = lfs_setattr(&g_directLfs, "af", 0x01, "new", 3);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_getattr(&g_directLfs, "af", 0x01, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, 3, err);
    /* 第二个属性类型 */
    err = lfs_setattr(&g_directLfs, "af", 0x02, "second", 6);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_getattr(&g_directLfs, "af", 0x02, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, 6, err);
    /* 不存在的属性类型: LFS_ERR_NOATTR */
    err = lfs_getattr(&g_directLfs, "af", 0x77, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOATTR, err);
    /* 删除属性 */
    err = lfs_removeattr(&g_directLfs, "af", 0x01);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_getattr(&g_directLfs, "af", 0x01, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOATTR, err);
    err = lfs_removeattr(&g_directLfs, "af", 0x01); /* 重复删除幂等, 返回 0 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    /* 不存在的路径: NOENT */
    err = lfs_setattr(&g_directLfs, "no_such", 0x01, "x", 1);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOENT, err);
    err = lfs_getattr(&g_directLfs, "no_such", 0x01, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOENT, err);
    /* 属性超限(attr_max=1022): 失败 */
    static char bigAttr[LFS_ATTR_MAX + 2]; /* 静态超限缓冲, 1024 字节 */
    err = lfs_setattr(&g_directLfs, "af", 0x03, bigAttr, sizeof(bigAttr));
    ICUNIT_ASSERT_EQUAL(err < 0, 1, err);

    err = lfs_remove(&g_directLfs, "af");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_FS_STAT
 * @tc.name     LfsDirectFsStat
 * @tc.desc     引用上游 test_superblocks.toml: lfs_fs_stat 几何/版本/上限字段
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectFsStat, Function | MediumTest | Level1)
{
    int err;
    struct lfs_fsinfo fsinfo = {0};

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_fs_stat(&g_directLfs, &fsinfo);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.disk_version, (int)LFS_DISK_VERSION, (int)fsinfo.disk_version);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.block_size, DIRECT_ERASE_SIZE, (int)fsinfo.block_size);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.block_count, DIRECT_BLOCKS, (int)fsinfo.block_count);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.name_max, LFS_NAME_MAX, (int)fsinfo.name_max);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.file_max, LFS_FILE_MAX, (int)fsinfo.file_max);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.attr_max, LFS_ATTR_MAX, (int)fsinfo.attr_max);

    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* lfs_fs_traverse 回调: 位图去重计数 */
static int LfsDirectTraverseCb(void *data, lfs_block_t block)
{
    uint8_t *seen = (uint8_t *)data;
    seen[block / 8] |= (uint8_t)(1 << (block % 8)); /* 8: 每字节 8 个块位 */
    return 0;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_FS_SIZE_TRAVERSE
 * @tc.name     LfsDirectFsSizeTraverse
 * @tc.desc     引用上游 test_interspersed.toml: fs_size 增长与 fs_traverse 遍历去重
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectFsSizeTraverse, Function | MediumTest | Level1)
{
    int err;
    int i;
    int cnt = 0;
    lfs_file_t file;
    lfs_ssize_t size0;
    lfs_ssize_t size1;
    static uint8_t seen[DIRECT_MAX_BLOCKS / 8]; /* 遍历位图, 每位一个块 */
    char buf[DIRECT_BUF_LEN];

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 空文件系统: 至少超级块+根目录占用 */
    size0 = lfs_fs_size(&g_directLfs);
    ICUNIT_ASSERT_NOT_EQUAL((int)size0, 0, -1);
    ICUNIT_ASSERT_EQUAL((int)(size0 > 0 && size0 <= DIRECT_BLOCKS), 1, (int)size0);

    /* 写 6KB 数据(超过旧容量一半): 分配块数增长 */
    (void)memset_s(buf, sizeof(buf), 'S', sizeof(buf));
    err = lfs_file_open(&g_directLfs, &file, "szf", LFS_O_WRONLY | LFS_O_CREAT);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (i = 0; i < 6; i++) { /* 6KB = 6*1024 */
        err = lfs_file_write(&g_directLfs, &file, buf, DIRECT_BUF_LEN);
        ICUNIT_ASSERT_EQUAL(err, DIRECT_BUF_LEN, err);
    }
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    size1 = lfs_fs_size(&g_directLfs);
    ICUNIT_ASSERT_EQUAL((int)(size1 > size0), 1, (int)(size1 - size0));

    /* 遍历: 每个已分配块恰好访问一次 */
    (void)memset_s(seen, sizeof(seen), 0, sizeof(seen));
    err = lfs_fs_traverse(&g_directLfs, LfsDirectTraverseCb, seen);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (i = 0; i < DIRECT_BLOCKS; i++) {
        if (seen[i / 8] & (1 << (i % 8))) { /* 8: 每字节 8 个块位 */
            cnt++;
        }
    }
    ICUNIT_ASSERT_EQUAL(cnt, (int)size1, cnt);

    err = lfs_remove(&g_directLfs, "szf");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_FS_GROW
 * @tc.name     LfsDirectFsGrow
 * @tc.desc     引用上游 test_shrink.toml(扩容方向): 等容/扩容/跨旧边界写/持久化
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectFsGrow, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    lfs_ssize_t sz;
    struct lfs_fsinfo fsinfo = {0};
    static char big[DIRECT_BUF_LEN]; /* 静态 1KB 写缓冲 */

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 等容 grow: 0 */
    err = lfs_fs_grow(&g_directLfs, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 扩容到 64 块: 0, fs_stat 反映新容量 */
    err = lfs_fs_grow(&g_directLfs, DIRECT_MAX_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_fs_stat(&g_directLfs, &fsinfo);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.block_count, DIRECT_MAX_BLOCKS, (int)fsinfo.block_count);

    /* 写入超过旧 32 块(16KB)边界的文件: 20KB 数据 + 元数据, 强制使用扩容块 */
    (void)memset_s(big, sizeof(big), 'G', sizeof(big));
    err = lfs_file_open(&g_directLfs, &file, "growf", LFS_O_WRONLY | LFS_O_CREAT);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (int i = 0; i < 20; i++) { /* 20KB, 跨过旧 16KB 边界 */
        err = lfs_file_write(&g_directLfs, &file, big, sizeof(big));
        ICUNIT_ASSERT_EQUAL(err, (int)sizeof(big), err);
    }
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 容量持久化: 卸载重挂(以新几何)后文件可读 */
    err = lfs_unmount(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    g_directCfg.block_count = DIRECT_MAX_BLOCKS;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_open(&g_directLfs, &file, "growf", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)lfs_file_size(&g_directLfs, &file), 20 * 1024, -1);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    sz = lfs_fs_size(&g_directLfs);
    ICUNIT_ASSERT_EQUAL((int)(sz > DIRECT_BLOCKS), 1, (int)sz);

    (void)lfs_remove(&g_directLfs, "growf");
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_FS_GC_MKCONSISTENT
 * @tc.name     LfsDirectFsGcMkconsistent
 * @tc.desc     lfs_fs_mkconsistent 与 lfs_fs_gc 在干净/使用中文件系统上返回 0
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectFsGcMkconsistent, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 干净文件系统: mkconsistent/gc 均为 0 */
    err = lfs_fs_mkconsistent(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_fs_gc(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 写入若干文件后: 仍为 0 且 fs 可继续使用 */
    for (int i = 0; i < 4; i++) { /* 4 个小文件 */
        char name[16]; /* 16: 文件名缓冲 */
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1, "gf%d", i);
        err = lfs_file_open(&g_directLfs, &file, name, LFS_O_WRONLY | LFS_O_CREAT);
        ICUNIT_ASSERT_EQUAL(err, 0, err);
        err = lfs_file_write(&g_directLfs, &file, "gc", 2);
        ICUNIT_ASSERT_EQUAL(err, 2, err);
        err = lfs_file_close(&g_directLfs, &file);
        ICUNIT_ASSERT_EQUAL(err, 0, err);
    }
    err = lfs_fs_mkconsistent(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_fs_gc(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_file_open(&g_directLfs, &file, "gf0", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    for (int i = 0; i < 4; i++) {
        char name[16];
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1, "gf%d", i);
        (void)lfs_remove(&g_directLfs, name);
    }
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_RAMBD
 * @tc.name     LfsDirectRambd
 * @tc.desc     引用上游 test_bd.toml: rambd 创建(malloc 路径)/直接块操作/销毁
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectRambd, Function | MediumTest | Level1)
{
    int err;
    struct lfs_rambd_config bdCfg = {0};
    struct lfs_config cfg = {0};
    lfs_rambd_t rambd;
    uint8_t wBuf[DIRECT_IO_SIZE];
    uint8_t rBuf[DIRECT_IO_SIZE];

    /* buffer=NULL: lfs_rambd_create 走 lfs_malloc 分配路径 */
    bdCfg.read_size = DIRECT_IO_SIZE;
    bdCfg.prog_size = DIRECT_IO_SIZE;
    bdCfg.erase_size = DIRECT_ERASE_SIZE;
    bdCfg.erase_count = 2; /* 2: 最小块数(元数据对) */
    bdCfg.buffer = NULL;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.context = &rambd;
    cfg.read = lfs_rambd_read;
    cfg.prog = lfs_rambd_prog;
    cfg.erase = lfs_rambd_erase;
    cfg.sync = lfs_rambd_sync;
    cfg.read_size = DIRECT_IO_SIZE;
    cfg.prog_size = DIRECT_IO_SIZE;
    cfg.block_size = DIRECT_ERASE_SIZE;
    cfg.block_count = 2; /* 2: 最小块数 */

    err = lfs_rambd_create(&cfg, &bdCfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 直接块操作: prog -> read 回读 -> erase -> read 全零 -> sync */
    (void)memset_s(wBuf, sizeof(wBuf), 0xA5, sizeof(wBuf));
    err = lfs_rambd_prog(&cfg, 1, 0, wBuf, sizeof(wBuf)); /* 块 1 偏移 0 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_rambd_read(&cfg, 1, 0, rBuf, sizeof(rBuf));
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = memcmp(rBuf, wBuf, sizeof(wBuf));
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_rambd_erase(&cfg, 1);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_rambd_read(&cfg, 1, 0, rBuf, sizeof(rBuf));
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL(rBuf[0], 0xA5, rBuf[0]); /* rambd erase 为 noop, 数据保持 */
    err = lfs_rambd_sync(&cfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    err = lfs_rambd_destroy(&cfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    return POSIX_FS_NO_ERROR;
}

/* 用例简要描述: 超级块cfg校验与旧版本升级重写 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_SUPERBLOCK_CFG
 * @tc.name     LfsDirectSuperblockCfg
 * @tc.desc     超级块cfg校验与旧版本升级重写
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectSuperblockCfg, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    char buf[DIRECT_BUF_LEN];
    /* lfs_init 静态用户缓冲(尺寸须与 cache_size/lookahead_size 一致) */
    static uint8_t readBuf[DIRECT_IO_SIZE];
    static uint8_t progBuf[DIRECT_IO_SIZE];
    static uint8_t lookBuf[8]; /* 8: 前瞻缓冲字数 */

    /* 以默认上限(name_max=255 等)格式化并挂载 */
    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_unmount(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* name_max 小于盘面值: 超级块记录 255 > cfg 100 -> INVAL */
    g_directCfg.name_max = 100;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);
    g_directCfg.name_max = 0;
    /* file_max 小于盘面值: INVAL */
    g_directCfg.file_max = 100;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);
    g_directCfg.file_max = 0;
    /* attr_max 小于盘面值: INVAL */
    g_directCfg.attr_max = 100;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_INVAL, err);
    g_directCfg.attr_max = 0;

#ifdef LFS_MULTIVERSION
    /* 旧次版本(2.0)格式化 + 默认版本(2.1)挂载: 挂载登记 needssuperblock,
     * 首次目录操作触发 desuperblock 重写超级块, 版本升级并持久化。
     * 注: 本构建未定义 LFS_MULTIVERSION(无 disk_version 配置字段),
     * 该段仅在启用多版本支持的构建中生效。 */
    struct lfs_fsinfo fsinfo = { 0 };
    g_directCfg.disk_version = 0x00020000; /* v2.0 旧次版本 */
    err = lfs_format(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    g_directCfg.disk_version = 0;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_fs_stat(&g_directLfs, &fsinfo);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.disk_version, 0x00020000, (int)fsinfo.disk_version);
    err = lfs_mkdir(&g_directLfs, "vdir"); /* 首次目录写 -> desuperblock 升级 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_fs_stat(&g_directLfs, &fsinfo);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL((int)fsinfo.disk_version, (int)LFS_DISK_VERSION, (int)fsinfo.disk_version);
    (void)lfs_remove(&g_directLfs, "vdir");
    err = lfs_unmount(&g_directLfs);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
#endif

    /* read/prog/lookahead 静态用户缓冲挂载: lfs_init 直接采用用户缓冲 */
    g_directCfg.read_buffer = readBuf;
    g_directCfg.prog_buffer = progBuf;
    g_directCfg.lookahead_buffer = lookBuf;
    err = lfs_mount(&g_directLfs, &g_directCfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 静态缓冲下的读写校验 */
    err = lfs_file_open(&g_directLfs, &file, "sbf", LFS_O_WRONLY | LFS_O_CREAT);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_write(&g_directLfs, &file, "Hello", strlen("Hello") + 1);
    ICUNIT_ASSERT_EQUAL(err, (int)strlen("Hello") + 1, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_open(&g_directLfs, &file, "sbf", LFS_O_RDONLY);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    err = lfs_file_read(&g_directLfs, &file, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, (int)strlen("Hello") + 1, err);
    err = strcmp(buf, "Hello");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 注: read_buffer 等字段须保持至 unmatch/teardown 之后由 LfsDirectSetup
     * 清零, 提前置空会使 lfs_deinit 误释放静态缓冲 */
    err = lfs_remove(&g_directLfs, "sbf");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

/* 用例简要描述: opencfg属性读写与路径名错误码 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSDIRECT_OPENCFG_ATTRS
 * @tc.name     LfsDirectOpenCfgAttrs
 * @tc.desc     opencfg属性读写与路径名错误码
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsDirectOpenCfgAttrs, Function | MediumTest | Level1)
{
    int err;
    lfs_file_t file;
    char rBuf0[8] = { 0 }; /* 8: 属性读缓冲 0 */
    char rBuf1[8] = { 0 }; /* 8: 属性读缓冲 1 */
    char buf[DIRECT_BUF_LEN];
    static char attrData[] = "newattr"; /* 7 字节写入属性 */
    static char bigAttr[LFS_ATTR_MAX + 2]; /* 静态超限缓冲, 1024 字节 */
    static char longName[LFS_NAME_MAX + 2]; /* 静态超长名, 256 字节 */
    struct lfs_attr rAttrs[2];
    struct lfs_attr wAttrs[1];
    struct lfs_attr overAttrs[1];
    struct lfs_file_config fcfg = { 0 };

    err = LfsDirectSetup(true, DIRECT_BLOCKS);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 建文件并写入一个用户属性 */
    err = lfs_file_open(&g_directLfs, &file, "af", LFS_O_WRONLY | LFS_O_CREAT);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_setattr(&g_directLfs, "af", 0x11, "attr", 4); /* 4 字节属性 */
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 读开取回属性: opencfg 的 attrs 缓冲在 open 时回填 */
    rAttrs[0].type = 0x11;
    rAttrs[0].buffer = rBuf0;
    rAttrs[0].size = 4; /* 4: 与盘面属性同尺寸 */
    rAttrs[1].type = 0x22; /* 不存在的属性类型 */
    rAttrs[1].buffer = rBuf1;
    rAttrs[1].size = 4;
    fcfg.attrs = rAttrs;
    fcfg.attr_count = 2; /* 2: 存在 + 不存在各一 */
    err = lfs_file_opencfg(&g_directLfs, &file, "af", LFS_O_RDONLY, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = memcmp(rBuf0, "attr", 4);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    ICUNIT_ASSERT_EQUAL(rBuf1[0], 0, rBuf1[0]); /* 不存在属性不回填 */

    /* 写开置脏: 属性随文件内容在关闭时原子提交 */
    wAttrs[0].type = 0x33;
    wAttrs[0].buffer = attrData;
    wAttrs[0].size = strlen(attrData);
    fcfg.attrs = wAttrs;
    fcfg.attr_count = 1;
    err = lfs_file_opencfg(&g_directLfs, &file, "af", LFS_O_WRONLY, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_file_write(&g_directLfs, &file, "data", 4); /* 4 字节数据 */
    ICUNIT_ASSERT_EQUAL(err, 4, err);
    err = lfs_file_close(&g_directLfs, &file);
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    err = lfs_getattr(&g_directLfs, "af", 0x33, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(err, (int)strlen(attrData), err);
    err = memcmp(buf, attrData, strlen(attrData));
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 写开属性超限(attr_max=1022): NOSPC */
    overAttrs[0].type = 0x44;
    overAttrs[0].buffer = bigAttr;
    overAttrs[0].size = sizeof(bigAttr);
    fcfg.attrs = overAttrs;
    fcfg.attr_count = 1;
    err = lfs_file_opencfg(&g_directLfs, &file, "af", LFS_O_WRONLY, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOSPC, err);

    /* 带尾斜杠的新路径: NOTDIR */
    fcfg.attrs = NULL;
    fcfg.attr_count = 0;
    err = lfs_file_opencfg(&g_directLfs, &file, "trail/", LFS_O_WRONLY | LFS_O_CREAT, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NOTDIR, err);

    /* 超长名(name_max=255): NAMETOOLONG */
    (void)memset_s(longName, sizeof(longName), 'l', sizeof(longName) - 1);
    longName[LFS_NAME_MAX + 1] = '\0';
    err = lfs_file_opencfg(&g_directLfs, &file, longName, LFS_O_WRONLY | LFS_O_CREAT, &fcfg);
    ICUNIT_ASSERT_EQUAL(err, LFS_ERR_NAMETOOLONG, err);

    err = lfs_remove(&g_directLfs, "af");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    LfsDirectTeardown();
    return POSIX_FS_NO_ERROR;
}

void PosixFsLfsDirectTest(void)
{
    RUN_ONE_TESTCASE(TestFsLfsDirectTruncate);
    RUN_ONE_TESTCASE(TestFsLfsDirectFilePos);
    RUN_ONE_TESTCASE(TestFsLfsDirectOpenCfg);
    RUN_ONE_TESTCASE(TestFsLfsDirectDirPos);
    RUN_ONE_TESTCASE(TestFsLfsDirectAttrs);
    RUN_ONE_TESTCASE(TestFsLfsDirectFsStat);
    RUN_ONE_TESTCASE(TestFsLfsDirectFsSizeTraverse);
    RUN_ONE_TESTCASE(TestFsLfsDirectFsGrow);
    RUN_ONE_TESTCASE(TestFsLfsDirectFsGcMkconsistent);
    RUN_ONE_TESTCASE(TestFsLfsDirectRambd);
    RUN_ONE_TESTCASE(TestFsLfsDirectSuperblockCfg);
    RUN_ONE_TESTCASE(TestFsLfsDirectOpenCfgAttrs);
}
#else
void PosixFsLfsDirectTest(void)
{
    /* 未启用 littlefs 时无直接 API 用例, 保留空桩供 PosixFsFuncTest 调用 */
}
#endif
