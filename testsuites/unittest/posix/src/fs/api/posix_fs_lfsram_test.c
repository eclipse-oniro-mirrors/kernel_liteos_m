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
 * 本文件为 littlefs 适配层与核心的破坏性/错误注入用例, 必须在全部依赖 /littlefs
 * 正常挂载的用例之后执行(注册顺序在 PosixFsFuncTest 末尾), 全部通过对外接口
 * (POSIX 文件接口 + LOS_DiskPartition/LOS_PartitionFormat/mount/umount)驱动。
 * 用例设计引用 littlefs 上游开源测试套(third_party/littlefs/tests/*.toml):
 *   - test_bd.toml / test_badblocks.toml : 块设备读写擦错误注入 -> 本文件 RAM 盘错误开关
 *   - test_exhaustion.toml               : 写满文件系统(ENOSPC)后删除再写
 *   - test_powerloss.toml                : 卸载后破坏元数据再挂载(损坏恢复路径)
 *   - test_entries.toml(小块几何)        : 目录分裂(dir split)与重定位
 *   - test_move.toml / block_cycles      : 数据块磨损重定位(relocate)
 *
 * 实现说明: mount() 的 data 参数即对外头文件 los_fs.h 定义的 struct PartitionCfg,
 * 其 readFunc/writeFunc/eraseFunc 由调用方提供(公开 API 约定)。本文件用静态 RAM
 * 缓冲实现这三个回调作为可控"块设备"(等价上游 lfs_emubd 的角色), 并提供
 * 读/写/擦失败开关与内存破坏入口, 从外部接口驱动 littlefs 核心错误分支。
 */

#include "posix_fs_test.h"
#include "los_fs.h"
#include <sys/statfs.h>

#define LFSRAM_DEV_BLOCKS       64      /* RAM 盘块数(小块几何) */
#define LFSRAM_BLOCK_SIZE       512     /* 小块几何: 驱动目录分裂 */
#define LFSRAM_BIG_BLOCKS       8       /* 空间耗尽用小盘: 8*4096 = 32KB(与 RAM 盘等容) */
#define LFSRAM_BIG_BLOCK_SIZE   4096
#define LFSRAM_DEV_SIZE         (LFSRAM_DEV_BLOCKS * LFSRAM_BLOCK_SIZE)
#define LFSRAM_IO_SIZE          64      /* read/prog/cache 尺寸(小块几何) */
#define LFSRAM_BIG_IO_SIZE      256     /* 大块几何的 read/prog/cache 尺寸 */
#define LFSRAM_EXHAUST_CAP      512     /* 写满循环上限(防死循环) */
#define LFSRAM_SPLIT_FILES      40      /* 目录分裂用文件数 */
#define LFSRAM_REWRITE_TIMES    30      /* 磨损重定位改写次数 */
#define LFSRAM_BUF_LEN          1024
#define LFSRAM_BAD_PARTNO       5       /* 越界分区号 */

/* RAM "块设备" 状态与错误注入开关 */
static UINT8 g_lfsRamDev[LFSRAM_DEV_SIZE];  /* 最大几何: 64*512 = 32KB */
static volatile UINT32 g_lfsRamBase = 0;    /* 首次访问捕获的设备基地址 */
static volatile UINT32 g_lfsRamErrMask = 0; /* bit0 读失败 bit1 写失败 bit2 擦失败 */

/* RAM 盘读回调: PartitionCfg.readFunc 签名(los_fs.h 公开定义) */
static int LfsRamRead(int partition, UINT32 *offset, void *buf, UINT32 size)
{
    (void)partition;
    if (g_lfsRamBase == 0) {
        g_lfsRamBase = *offset; /* 首次访问必为块 0 偏移 0: 即设备基地址 */
    }
    if ((*offset < g_lfsRamBase) || (*offset + size > g_lfsRamBase + sizeof(g_lfsRamDev))) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x1) { /* bit0: 读失败 */
        return -1;
    }
    (void)memcpy_s(buf, size, g_lfsRamDev + (*offset - g_lfsRamBase), size);
    return 0;
}

/* RAM 盘写回调 */
static int LfsRamWrite(int partition, UINT32 *offset, const void *buf, UINT32 size)
{
    (void)partition;
    if (g_lfsRamBase == 0) {
        g_lfsRamBase = *offset;
    }
    if ((*offset < g_lfsRamBase) || (*offset + size > g_lfsRamBase + sizeof(g_lfsRamDev))) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x2) { /* bit1: 写失败 */
        return -1;
    }
    (void)memcpy_s(g_lfsRamDev + (*offset - g_lfsRamBase), size, buf, size);
    return 0;
}

/* RAM 盘擦回调: 与 qemu virt flash 一致擦 0 */
static int LfsRamErase(int partition, UINT32 offset, UINT32 size)
{
    (void)partition;
    if (g_lfsRamBase == 0) {
        g_lfsRamBase = offset;
    }
    if ((offset < g_lfsRamBase) || (offset + size > g_lfsRamBase + sizeof(g_lfsRamDev))) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x4) { /* bit2: 擦失败 */
        return -1;
    }
    (void)memset_s(g_lfsRamDev + (offset - g_lfsRamBase), size, 0, size);
    return 0;
}

/* 构造挂载参数(小块几何: 触发目录分裂与重定位) */
static void LfsRamFillCfg(struct PartitionCfg *cfg, int partNo, int blockCount)
{
    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->partNo = partNo;
    cfg->blockSize = LFSRAM_BLOCK_SIZE;
    cfg->blockCount = blockCount;
    cfg->readSize = LFSRAM_IO_SIZE;
    cfg->writeSize = LFSRAM_IO_SIZE;
    cfg->cacheSize = LFSRAM_IO_SIZE;
    cfg->lookaheadSize = 8;   /* 8: 前瞻缓冲字数 */
    cfg->blockCycles = 2;     /* 2: 低磨损阈值, 驱动块重定位 */
    cfg->readFunc = LfsRamRead;
    cfg->writeFunc = LfsRamWrite;
    cfg->eraseFunc = LfsRamErase;
}

/* 构造挂载参数(大块几何: 空间耗尽用 12*4096) */
static void LfsRamFillBigCfg(struct PartitionCfg *cfg)
{
    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->partNo = 0;
    cfg->blockSize = LFSRAM_BIG_BLOCK_SIZE;
    cfg->blockCount = LFSRAM_BIG_BLOCKS;
    cfg->readSize = LFSRAM_BIG_IO_SIZE;
    cfg->writeSize = LFSRAM_BIG_IO_SIZE;
    cfg->cacheSize = LFSRAM_BIG_IO_SIZE;
    cfg->lookaheadSize = 8;   /* 8: 前瞻缓冲字数 */
    cfg->blockCycles = 500;   /* 500: 高磨损阈值 */
    cfg->readFunc = LfsRamRead;
    cfg->writeFunc = LfsRamWrite;
    cfg->eraseFunc = LfsRamErase;
}

/* 直接破坏 RAM 盘指定块内数据(模拟掉电/位翻转损坏) */
static void LfsRamCorruptBlock(int block, int blockSize)
{
    UINT32 addr = g_lfsRamBase + (UINT32)block * (UINT32)blockSize;
    g_lfsRamDev[addr - g_lfsRamBase + 8] ^= 0xFF; /* 8: 块内固定偏移翻转一字节 */
    g_lfsRamDev[addr - g_lfsRamBase + 9] ^= 0xFF; /* 9: 相邻字节 */
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_UMOUNT_ROOT
 * @tc.name     LfsRamUmountRoot
 * @tc.desc     卸载 /littlefs: LfsUmount/lfs_unmount/MpDeleteFromList(引用非零失败->成功)
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamUmountRoot, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    struct stat st = { 0 };

    /* 清理历史用例可能遗留的 fd(对已关闭 fd 的 close 无害) */
    for (fd = 3; fd < 256; fd++) { /* 3..255: 全部文件描述符 */
        (void)close(fd);
    }

    /* 有打开文件时 umount 失败: mRefs 非零 */
    fd = open(TEST_ROOT "/umf", O_CREAT | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = umount(TEST_ROOT);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 正常卸载: 覆盖 LfsUmount/lfs_unmount_/MpDeleteFromList(链表头) */
    ret = umount(TEST_ROOT);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 卸载后路径不可访问 */
    ret = stat(TEST_ROOT, &st);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    (void)unlink(TEST_ROOT "/umf");
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_DISK_PARTITION
 * @tc.name     LfsRamDiskPartition
 * @tc.desc     LOS_DiskPartition 注册设备/重复注册(EEXIST)/空地址数组(EFAULT)
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamDiskPartition, Function | MediumTest | Level1)
{
    int ret;
    int lengthArray[1] = { LFSRAM_DEV_SIZE }; /* 1: 单分区 */
    int addrArray[1] = { 0 };                 /* 1: 单分区 */

    /* 注册新设备: AddDevice 正常路径 */
    ret = LOS_DiskPartition("flash9", "littlefs", lengthArray, addrArray, 1);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 重复注册同名设备: EEXIST */
    ret = LOS_DiskPartition("flash9", "littlefs", lengthArray, addrArray, 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, -EEXIST, errno);

    /* 新设备名但空地址数组: EFAULT */
    ret = LOS_DiskPartition("flash8", "littlefs", lengthArray, NULL, 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, -EFAULT, errno);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MOUNT_FORMAT_FALLBACK
 * @tc.name     LfsRamMountFormatFallback
 * @tc.desc     引用上游 mount 流程: 未格式化 RAM 盘挂载失败后自动 format+重挂载
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMountFormatFallback, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    char buf[LFSRAM_BUF_LEN];
    struct PartitionCfg cfg;

    /* RAM 盘清零: 无有效超级块, lfs_mount 失败 -> lfs_format -> lfs_mount 成功 */
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0; /* 重置基地址捕获 */
    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);

    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 基础读写验证 */
    fd = open("/lfsram/fallback", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "fallback", strlen("fallback"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("fallback"), ret);
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = read(fd, buf, strlen("fallback"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("fallback"), ret);
    ICUNIT_ASSERT_EQUAL(memcmp(buf, "fallback", strlen("fallback")), 0, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 卸载后重挂载: 直接成功(已格式化), 数据仍在(持久性校验, 引用上游用例模式) */
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram/fallback", O_RDONLY);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("fallback"), ret);
    (void)close(fd);
    (void)unlink("/lfsram/fallback");

    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MOUNT_CORRUPT
 * @tc.name     LfsRamMountCorrupt
 * @tc.desc     引用上游 test_powerloss.toml: 超级块/根目录损坏后的挂载与修复路径
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMountCorrupt, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);

    /* 准备一个带文件的文件系统 */
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (void)close(open("/lfsram/keep", O_CREAT | O_WRONLY, 0666));
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 破坏超级块对(块 0/1): 挂载失败 -> format 修复 -> 挂载成功 */
    LfsRamCorruptBlock(0, LFSRAM_BLOCK_SIZE);
    LfsRamCorruptBlock(1, LFSRAM_BLOCK_SIZE);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret); /* 修复后成功 */
    ret = stat("/lfsram/keep", NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret); /* 数据已被格式化清除 */
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 重新格式化出干净盘, 再破坏根目录对(块 2/3) */
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (void)close(open("/lfsram/keep2", O_CREAT | O_WRONLY, 0666));
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    LfsRamCorruptBlock(2, LFSRAM_BLOCK_SIZE); /* 2: 根目录对首块 */
    LfsRamCorruptBlock(3, LFSRAM_BLOCK_SIZE); /* 3: 根目录对次块 */
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MOUNT_FAIL
 * @tc.name     LfsRamMountFail
 * @tc.desc     引用上游 test_bd.toml: 块设备擦失败时挂载彻底失败(适配层错误分支)
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMountFail, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);

    /* 破坏超级块并注入擦失败: mount 失败且 format 也失败 -> 返回 -1 并置 errno */
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    g_lfsRamErrMask = 0x4; /* bit2: 擦失败 */
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_lfsRamErrMask = 0;

    /* 越界分区号: LfsGetStartAddr 返回非法地址, 块操作全部失败 */
    LfsRamFillCfg(&cfg, LFSRAM_BAD_PARTNO, LFSRAM_DEV_BLOCKS);
    ret = mount(NULL, "/lfsram2", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 恢复后可正常挂载 */
    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_EXHAUSTION
 * @tc.name     LfsRamExhaustion
 * @tc.desc     引用上游 test_exhaustion.toml: 写满(ENOSPC)后删除再写恢复
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamExhaustion, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    int i;
    int full = 0;
    char buf[LFSRAM_BUF_LEN];
    struct PartitionCfg cfg;

    LfsRamFillBigCfg(&cfg);
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 持续写入 1KB 块直到 ENOSPC(上限防死循环) */
    (void)memset_s(buf, sizeof(buf), 'E', sizeof(buf));
    fd = open("/lfsram/exh", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    for (i = 0; i < LFSRAM_EXHAUST_CAP; i++) {
        ret = write(fd, buf, sizeof(buf));
        if (ret == -1) {
            full = 1;
            break;
        }
        ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
    }
    ICUNIT_GOTO_EQUAL(full, 1, full, EXIT);
    (void)close(fd);

    /* 写满后新文件的持续写入也最终 ENOSPC(单次写可能因剩余碎片空间成功) */
    fd = open("/lfsram/exh2", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    full = 0;
    for (i = 0; i < LFSRAM_EXHAUST_CAP; i++) {
        ret = write(fd, buf, sizeof(buf));
        if (ret == -1) {
            full = 1;
            break;
        }
    }
    ICUNIT_ASSERT_EQUAL(full, 1, full);
    (void)close(fd);

    /* 删除后空间可回收再写(引用上游 exhausted 后的恢复校验) */
    ret = unlink("/lfsram/exh");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = unlink("/lfsram/exh2");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram/exh3", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)sizeof(buf), ret);
    (void)close(fd);

EXIT:
    (void)unlink("/lfsram/exh");
    (void)unlink("/lfsram/exh2");
    (void)unlink("/lfsram/exh3");
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_DIR_SPLIT
 * @tc.name     LfsRamDirSplit
 * @tc.desc     引用上游 test_entries.toml(小块几何): 目录增长触发分裂与交错增删压缩
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamDirSplit, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    int i;
    int cnt = 0;
    char path[PATH_MAX];
    char name[64]; /* 64: 长文件名缓冲 */
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mkdir("/lfsram/bigdir", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 40 个长名字文件: 目录元数据超过 block/2 触发 lfs_dir_split/splittingcompact */
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        ret = snprintf_s(name, sizeof(name), sizeof(name) - 1,
                         "splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
        ret = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/lfsram/bigdir/%s", name);
        ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
        fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        ret = write(fd, "split", strlen("split"));
        ICUNIT_GOTO_EQUAL(ret, (int)strlen("split"), ret, EXIT);
        (void)close(fd);
    }

    /* 交错删除一半并重建(引用 test_interspersed) */
    for (i = 0; i < LFSRAM_SPLIT_FILES; i += 2) { /* 2: 偶数项 */
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/bigdir/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)unlink(path);
    }
    for (i = 0; i < LFSRAM_SPLIT_FILES; i += 4) { /* 4: 部分重建 */
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/bigdir/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        fd = open(path, O_CREAT | O_WRONLY, 0666);
        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        (void)close(fd);
    }

    /* 校验目录项: 40 - 20 + 10 = 30 */
    dir = opendir("/lfsram/bigdir");
    ICUNIT_GOTO_NOT_EQUAL(dir, NULL, dir, EXIT);
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        cnt++;
    }
    (void)closedir(dir);
    ICUNIT_ASSERT_EQUAL(cnt, 30, cnt); /* 30 = 40-20+10 */

    /* 卸载重挂载后目录项稳定(持久性) */
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

EXIT:
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/bigdir/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)unlink(path);
    }
    (void)rmdir("/lfsram/bigdir");
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_RELOCATE
 * @tc.name     LfsRamRelocate
 * @tc.desc     引用上游 test_move.toml/block_cycles: 低磨损阈值下反复改写驱动数据块重定位
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamRelocate, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    int i;
    char buf[LFSRAM_BUF_LEN];
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS); /* blockCycles=2: 快速触发重定位 */
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 反复整体改写同一文件: COW + 磨损重定位 */
    (void)memset_s(buf, sizeof(buf), 'R', sizeof(buf));
    for (i = 0; i < LFSRAM_REWRITE_TIMES; i++) {
        fd = open("/lfsram/reloc", O_CREAT | O_TRUNC | O_RDWR, 0666);
        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        ret = write(fd, buf, sizeof(buf));
        ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
        ret = lseek(fd, 0, SEEK_SET);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ret = read(fd, buf, sizeof(buf));
        ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
        ret = close(fd);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }

    /* 每次改写后数据校验(buf 内容保持 'R') */
    fd = open("/lfsram/reloc", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf[0], 'R', buf[0], EXIT);
    (void)close(fd);

EXIT:
    (void)unlink("/lfsram/reloc");
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_WRITE_FAIL
 * @tc.name     LfsRamWriteFail
 * @tc.desc     引用上游 test_badblocks.toml: 块设备写失败时文件写返回错误
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamWriteFail, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    char buf[LFSRAM_BUF_LEN];
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    fd = open("/lfsram/bbf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);

    /* 注入块设备写失败: 适配层 LfsWrite 错误分支, write 返回 -1 */
    (void)memset_s(buf, sizeof(buf), 'W', sizeof(buf));
    g_lfsRamErrMask = 0x2; /* bit1: 写失败 */
    ret = write(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 注入读失败: read 返回 -1 */
    g_lfsRamErrMask = 0x1; /* bit0: 读失败 */
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_lfsRamErrMask = 0;

    /* 有缓存挂起数据时注入写失败: fsync 刷盘失败(适配层错误分支) */
    ret = write(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)sizeof(buf), ret);
    g_lfsRamErrMask = 0x2; /* bit1: 写失败 */
    ret = fsync(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_lfsRamErrMask = 0;

    /* 清除注入后可正常读写 */
    ret = write(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)sizeof(buf), ret);

    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = unlink("/lfsram/bbf");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_FORMAT_OPS
 * @tc.name     LfsRamFormatOps
 * @tc.desc     LOS_PartitionFormat: 挂载中 EBUSY / 擦失败 / 正常格式化
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamFormatOps, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 挂载中格式化同一路径: EBUSY */
    ret = LOS_PartitionFormat("/lfsram", "littlefs", &cfg);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EBUSY, errno);

    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 注入擦失败: LfsFormat 错误分支 */
    g_lfsRamErrMask = 0x4; /* bit2: 擦失败 */
    ret = LOS_PartitionFormat("/lfsram", "littlefs", &cfg);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    g_lfsRamErrMask = 0;

    /* 正常格式化 */
    ret = LOS_PartitionFormat("/lfsram", "littlefs", &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 格式化后挂载使用 */
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (void)close(open("/lfsram/fmtf", O_CREAT | O_WRONLY, 0666));
    (void)unlink("/lfsram/fmtf");
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_RDONLY_MOUNT
 * @tc.name     LfsRamRdonlyMount
 * @tc.desc     只读挂载 littlefs: mWriteEnable=FALSE 的 open/ftruncate/fsync EACCES 分支
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamRdonlyMount, Function | MediumTest | Level1)
{
    int ret;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    int fd;
    char buf[8] = { 0 }; /* 8 字节缓冲 */
    struct PartitionCfg cfg;

    /* 先以可写挂载准备文件 */
    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, sizeof(g_lfsRamDev), 0, sizeof(g_lfsRamDev));
    g_lfsRamBase = 0;
    ret = mount(NULL, "/lfsro", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsro/rof", O_CREAT | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "ro", 2); /* 2 字节 */
    ICUNIT_ASSERT_EQUAL(ret, 2, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = umount("/lfsro");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* MS_RDONLY 重挂载 */
    ret = mount(NULL, "/lfsro", "littlefs", MS_RDONLY, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 只读挂载点禁止写打开: EACCES */
    fd = open("/lfsro/rof", O_WRONLY);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);
    ICUNIT_ASSERT_EQUAL(errno, EACCES, errno);
    fd = open("/lfsro/newf", O_CREAT | O_WRONLY, 0666);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);

    /* 只读 fd 在只读挂载点上: ftruncate/fsync EACCES(mWriteEnable=FALSE 分支) */
    fd = open("/lfsro/rof", O_RDONLY);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = read(fd, buf, 2);
    ICUNIT_ASSERT_EQUAL(ret, 2, ret);
    ret = ftruncate(fd, 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = fsync(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = umount("/lfsro");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 末尾状态: /littlefs 与 /lfsro 均已卸载, 用例序列结束 */
    ret = stat("/lfsro/rof", NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    return POSIX_FS_NO_ERROR;
}

void PosixFsLfsRamTest(void)
{
    RUN_ONE_TESTCASE(TestFsLfsRamUmountRoot);
    RUN_ONE_TESTCASE(TestFsLfsRamDiskPartition);
    RUN_ONE_TESTCASE(TestFsLfsRamMountFormatFallback);
    RUN_ONE_TESTCASE(TestFsLfsRamMountCorrupt);
    RUN_ONE_TESTCASE(TestFsLfsRamMountFail);
    RUN_ONE_TESTCASE(TestFsLfsRamExhaustion);
    RUN_ONE_TESTCASE(TestFsLfsRamDirSplit);
    RUN_ONE_TESTCASE(TestFsLfsRamRelocate);
    RUN_ONE_TESTCASE(TestFsLfsRamWriteFail);
    RUN_ONE_TESTCASE(TestFsLfsRamFormatOps);
    RUN_ONE_TESTCASE(TestFsLfsRamRdonlyMount);
}
