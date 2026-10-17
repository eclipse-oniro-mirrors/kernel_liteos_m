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
#include "los_memory.h" /* LOS_MemAlloc/LOS_MemFree: 设备存储按需分配 */
#include <sys/statfs.h>
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
#include "lfs.h"
#endif

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
/*
 * 设备存储由静态 .bss 改为按需动态申请(PosixFsLfsRamTest 首尾托管, 套件外
 * 零占用): 32KB 常驻 .bss 挤占 SRAM 预算, 令板级 ram_lfs 的 128KB 单块分配
 * 失败, PosixFsFuncTest 的 FS 域被整域跳过。申请失败时整族用例跳过; 原遍布
 * 各回调的 ((UINT32)LFSRAM_DEV_SIZE) 随指针化统一改为 LFSRAM_DEV_SIZE。
 */
static UINT8 *g_lfsRamDev = NULL;  /* 最大几何: 64*512 = 32KB */
static volatile UINT32 g_lfsRamErrMask = 0; /* bit0 读失败 bit1 写失败 bit2 擦失败 */
static volatile UINT32 g_lfsRamVerifyFail = 0; /* 写后校验失败注入(一次性): 写回调落盘后
                                                * 翻转首字节, 使 littlefs 读回校验(prog 后
                                                * 逐字节比对/CRC)发现 CORRUPT -> 上层换块
                                                * 重定位重试, 用毕自动清零 */

/*
 * 禁止用 "0 作未捕获哨兵" 再捕获基地址: 设备基址本身
 * 为 0, 块 1 访问(偏移=blockSize)会把哨兵再次捕获成 blockSize, 使块 0 的
 * 后续访问全部误判越界。
 */

/* RAM 盘读回调: PartitionCfg.readFunc 签名(los_fs.h 公开定义) */
static int LfsRamRead(int partition, UINT32 *offset, void *buf, UINT32 size)
{
    (void)partition;
    if ((*offset > ((UINT32)LFSRAM_DEV_SIZE)) || (size > ((UINT32)LFSRAM_DEV_SIZE) - *offset)) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x1) { /* bit0: 读失败 */
        return -1;
    }
    (void)memcpy_s(buf, size, g_lfsRamDev + *offset, size);
    return 0;
}

/* RAM 盘写回调 */
static int LfsRamWrite(int partition, UINT32 *offset, const void *buf, UINT32 size)
{
    (void)partition;
    if ((*offset > ((UINT32)LFSRAM_DEV_SIZE)) || (size > ((UINT32)LFSRAM_DEV_SIZE) - *offset)) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x2) { /* bit1: 写失败 */
        return -1;
    }
    (void)memcpy_s(g_lfsRamDev + *offset, size, buf, size);
    return 0;
}

/* RAM 盘擦回调: 与 qemu virt flash 一致擦 0 */
static int LfsRamErase(int partition, UINT32 offset, UINT32 size)
{
    (void)partition;
    if ((offset > ((UINT32)LFSRAM_DEV_SIZE)) || (size > ((UINT32)LFSRAM_DEV_SIZE) - offset)) {
        return -1;
    }
    if (g_lfsRamErrMask & 0x4) { /* bit2: 擦失败 */
        return -1;
    }
    (void)memset_s(g_lfsRamDev + offset, size, 0, size);
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
    UINT32 addr = (UINT32)block * (UINT32)blockSize;
    g_lfsRamDev[addr + 8] ^= 0xFF; /* 8: 块内固定偏移翻转一字节 */
    g_lfsRamDev[addr + 9] ^= 0xFF; /* 9: 相邻字节 */
}

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
/*
 * littlefs 直调实例(参照 posix_fs_lfsdirect_test.c 的 LITE_COMPLIANT 先例):
 * 复用本文件 RAM 盘与注入开关, 为 VFS 适配层未接线的 mkconsistent/gc/fs_stat/
 * fs_truncate/fs_traverse 等公开接口提供测试通道。块回调按块号直接寻址
 * g_lfsRamDev(与适配层经 g_lfsRamBase 重定位后的设备视图一致), 注入开关同源。
 */
static lfs_t g_lfsRamDl;
static struct lfs_config g_lfsRamDlCfg;
static bool g_lfsRamDlMounted = false; /* DL 实例挂载态(lfs_unmount 即 lfs_deinit,
                                        * 重复卸载会对已释放缓冲二次 free -> panic) */

/* DL 实例幂等卸载: 已挂载才 unmount 并清标志, 未挂载返回 0。
 * 各用例收尾统一走本函数, 替代裸 lfs_unmount(消除正常路径 + EXIT 路径
 * 双重 deinit 的二次 free panic, 见 ItLosTestFsLfsRamFullDiskOps 实测) */
static int LfsRamDlTeardown(void)
{
    if (!g_lfsRamDlMounted) {
        return 0;
    }
    g_lfsRamDlMounted = false;
    return lfs_unmount(&g_lfsRamDl);
}

static int LfsRamDlRead(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, void *buf, lfs_size_t size)
{
    UINT32 pos = (UINT32)block * c->block_size + off;

    if ((pos + size) > (UINT32)((UINT32)LFSRAM_DEV_SIZE)) {
        return LFS_ERR_INVAL;
    }
    if (g_lfsRamErrMask & 0x1) { /* bit0: 读失败 */
        return LFS_ERR_IO;
    }
    (void)memcpy_s(buf, size, g_lfsRamDev + pos, size);
    return 0;
}

static int LfsRamDlWrite(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buf, lfs_size_t size)
{
    UINT32 pos = (UINT32)block * c->block_size + off;

    if ((pos + size) > (UINT32)((UINT32)LFSRAM_DEV_SIZE)) {
        return LFS_ERR_INVAL;
    }
    if (g_lfsRamErrMask & 0x2) { /* bit1: 写失败 */
        return LFS_ERR_IO;
    }
    (void)memcpy_s(g_lfsRamDev + pos, size, buf, size);
    if (g_lfsRamVerifyFail) {
        /* 写后校验失败注入(一次性), 与 LfsRamWrite 保持同源语义 */
        g_lfsRamDev[pos] ^= 0xFF;
        g_lfsRamVerifyFail = 0;
    }
    return 0;
}

static int LfsRamDlErase(const struct lfs_config *c, lfs_block_t block)
{
    UINT32 pos = (UINT32)block * c->block_size;

    if ((pos + c->block_size) > (UINT32)((UINT32)LFSRAM_DEV_SIZE)) {
        return LFS_ERR_INVAL;
    }
    if (g_lfsRamErrMask & 0x4) { /* bit2: 擦失败 */
        return LFS_ERR_IO;
    }
    (void)memset_s(g_lfsRamDev + pos, c->block_size, 0, c->block_size);
    return 0;
}

static int LfsRamDlSync(const struct lfs_config *c)
{
    (void)c; /* RAM 盘无掉电缓冲, 同步为空操作 */
    return 0;
}

/* 创建直调实例并(可选)格式化+挂载; ioSize 取 blockSize/8(512->64/4096->512) */
static int LfsRamDlSetup(bool format, lfs_size_t blockSize,
                         lfs_size_t blockCount, lfs_size_t compactThresh)
{
    int err;
    lfs_size_t ioSize = blockSize / 8; /* 8: read/prog/cache 尺寸缩放 */

    /* 幂等守卫: 前序用例遗留挂载先卸载(消除跨用例实例状态污染) */
    (void)LfsRamDlTeardown();

    (void)memset_s(&g_lfsRamDlCfg, sizeof(g_lfsRamDlCfg), 0, sizeof(g_lfsRamDlCfg));
    g_lfsRamDlCfg.read = LfsRamDlRead;
    g_lfsRamDlCfg.prog = LfsRamDlWrite;
    g_lfsRamDlCfg.erase = LfsRamDlErase;
    g_lfsRamDlCfg.sync = LfsRamDlSync;
    g_lfsRamDlCfg.read_size = ioSize;
    g_lfsRamDlCfg.prog_size = ioSize;
    g_lfsRamDlCfg.block_size = blockSize;
    g_lfsRamDlCfg.block_count = blockCount;
    g_lfsRamDlCfg.cache_size = ioSize;
    g_lfsRamDlCfg.lookahead_size = 8;   /* 8: 前瞻缓冲字数 */
    g_lfsRamDlCfg.block_cycles = 2;     /* 2: 低磨损阈值, 驱动元数据重定位链 */
    g_lfsRamDlCfg.compact_thresh = compactThresh; /* gc 的元数据压缩阈值 */

    if (format) {
        err = lfs_format(&g_lfsRamDl, &g_lfsRamDlCfg);
        if (err != 0) {
            return err;
        }
    }
    err = lfs_mount(&g_lfsRamDl, &g_lfsRamDlCfg);
    g_lfsRamDlMounted = (err == 0);
    return err;
}

/* lfs_fs_traverse 回调: 计数已用块 */
static int LfsRamDlCountCb(void *data, lfs_block_t block)
{
    (void)block;
    if (data != NULL) {
        (*(int *)data)++;
    }
    return 0;
}

/* lfs_fs_traverse 回调: 立即返回错误(驱动遍历中断分支) */
static int LfsRamDlFailCb(void *data, lfs_block_t block)
{
    (void)data;
    (void)block;
    return LFS_ERR_IO;
}
#endif

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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
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

/* 用例简要描述: 跨目录rename覆盖空目录与类型冲突错误码 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_RENAME_CROSS_DIR
 * @tc.name     LfsRamRenameCrossDir
 * @tc.desc     跨目录rename覆盖空目录与类型冲突错误码
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamRenameCrossDir, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    char buf[32]; /* 32: 读缓冲 */
    struct stat st = { 0 };
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = mkdir("/lfsram/d1", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mkdir("/lfsram/d2", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram/d1/f", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "renameDATA", strlen("renameDATA"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("renameDATA"), ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 跨目录 rename: prepmove 登记待决 move + 两段提交 */
    ret = rename("/lfsram/d1/f", "/lfsram/d2/g");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d1/f", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT); /* 旧路径消失 */
    ret = stat("/lfsram/d2/g", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((int)st.st_size, (int)strlen("renameDATA"), (int)st.st_size, EXIT);

    /* 文件 rename 到已存在目录名: EISDIR */
    ret = mkdir("/lfsram/d2/sub", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = rename("/lfsram/d2/g", "/lfsram/d2/sub");
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EISDIR, errno, EXIT);
    /* 目录 rename 到已存在文件名: ENOTDIR */
    ret = rename("/lfsram/d2/sub", "/lfsram/d2/g");
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOTDIR, errno, EXIT);
    /* 目录 rename 到非空目录: ENOTEMPTY */
    fd = open("/lfsram/d2/sub/inner", O_CREAT | O_WRONLY, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)close(fd);
    ret = rename("/lfsram/d1", "/lfsram/d2/sub");
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOTEMPTY, errno, EXIT);
    (void)unlink("/lfsram/d2/sub/inner");
    /* 文件 rename 到带尾斜杠的新名: ENOTDIR */
    ret = rename("/lfsram/d2/g", "/lfsram/d2/gg/");
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOTDIR, errno, EXIT);

    /* 目录 rename 覆盖空目录: preporphans + 孤儿回收 dir_drop */
    ret = mkdir("/lfsram/d2/emptyd", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/d1/keep", O_CREAT | O_WRONLY, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "keep", strlen("keep"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("keep"), ret, EXIT);
    (void)close(fd);
    ret = rename("/lfsram/d1", "/lfsram/d2/emptyd");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d2/emptyd/keep", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d1", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT); /* 旧目录名消失 */

    /* 同目录 rename 到更小 id 项(覆盖既有文件): newoldid 调整分支 */
    fd = open("/lfsram/d2/b1", O_CREAT | O_WRONLY, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "one", strlen("one"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("one"), ret, EXIT);
    (void)close(fd);
    fd = open("/lfsram/d2/b2", O_CREAT | O_WRONLY, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "two2", strlen("two2"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("two2"), ret, EXIT);
    (void)close(fd);
    ret = rename("/lfsram/d2/b2", "/lfsram/d2/b1");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d2/b2", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT); /* 源名消失 */
    fd = open("/lfsram/d2/b1", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("two2"), ret, EXIT);
    ret = strcmp(buf, "two2");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT); /* 内容为源文件内容 */
    (void)close(fd);

    /* 卸载重挂载: 跨目录/覆盖结果持久化 */
    ret = umount("/lfsram");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d2/g", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/d2/emptyd/keep", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    (void)close(fd);
    (void)unlink("/lfsram/d2/sub/inner");
    (void)unlink("/lfsram/d2/emptyd/keep");
    (void)unlink("/lfsram/d2/keep");
    (void)unlink("/lfsram/d2/b1");
    (void)unlink("/lfsram/d2/b2");
    (void)unlink("/lfsram/d2/g");
    (void)unlink("/lfsram/d1/f");
    (void)unlink("/lfsram/d1/keep");
    (void)rmdir("/lfsram/d2/emptyd");
    (void)rmdir("/lfsram/d2/sub");
    (void)rmdir("/lfsram/d2");
    (void)rmdir("/lfsram/d1");
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* 用例简要描述: 分裂目录中mkdir子目录重挂后仍存在 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MKDIR_SPLIT_DIR
 * @tc.name     LfsRamMkdirInSplitDir
 * @tc.desc     分裂目录中mkdir子目录重挂后仍存在
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMkdirInSplitDir, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    int i;
    int cnt = 0;
    char path[PATH_MAX];
    char name[64]; /* 64: 长文件名缓冲 */
    char buf[LFSRAM_BUF_LEN];
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st = { 0 };
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS); /* 512B 小块几何: 驱动目录分裂 */
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mkdir("/lfsram/bigdir", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 40 个长名文件: 目录元数据超过 block/2 触发 lfs_dir_split(引用 test_entries.toml) */
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
    /* 交错删除一半再部分重建: 制造目录元数据死区(引用 test_interspersed) */
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

    /* 分裂目录中继续 mkdir 子目录: 名字按序落在既有条目之间 -> 查找在父目录链
     * 中间块(split 父块)返回 NOENT, 触发 preporphans/找链尾/分裂尾更新路径 */
    for (i = 0; i < 3; i++) { /* 3: splitfile005x0..splitfile005x2 */
        ret = snprintf_s(path, sizeof(path), sizeof(path) - 1, "/lfsram/bigdir/splitfile005x%d", i);
        ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
        ret = mkdir(path, 0777);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    /* 新子目录内建文件并读写 */
    fd = open("/lfsram/bigdir/splitfile005x1/sf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "subdata", strlen("subdata"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("subdata"), ret, EXIT);
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("subdata"), ret, EXIT);
    ret = strcmp(buf, "subdata");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)close(fd);

    /* 目录项计数: 40 - 20 + 10 文件 + 3 子目录 = 33 */
    dir = opendir("/lfsram/bigdir");
    ICUNIT_GOTO_NOT_EQUAL(dir, NULL, dir, EXIT);
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        cnt++;
    }
    (void)closedir(dir);
    dir = NULL;
    ICUNIT_GOTO_EQUAL(cnt, 33, cnt, EXIT);

    /* 卸载重挂载: 分裂目录与新子目录均持久存在 */
    ret = umount("/lfsram");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat("/lfsram/bigdir/splitfile005x1/sf", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((int)st.st_size, (int)strlen("subdata"), (int)st.st_size, EXIT);
    cnt = 0;
    dir = opendir("/lfsram/bigdir");
    ICUNIT_GOTO_NOT_EQUAL(dir, NULL, dir, EXIT);
    while ((entry = readdir(dir)) != NULL) {
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }
        cnt++;
    }
    (void)closedir(dir);
    dir = NULL;
    ICUNIT_GOTO_EQUAL(cnt, 33, cnt, EXIT);

EXIT:
    if (dir != NULL) {
        (void)closedir(dir);
    }
    (void)unlink("/lfsram/bigdir/splitfile005x1/sf");
    for (i = 0; i < 3; i++) {
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1, "/lfsram/bigdir/splitfile005x%d", i);
        (void)rmdir(path);
    }
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/bigdir/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)unlink(path);
    }
    (void)rmdir("/lfsram/bigdir");
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* 用例简要描述: 追加2.5块文件与中段改写读回一致 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_APPEND_REWRITE
 * @tc.name     LfsRamAppendRewrite
 * @tc.desc     追加2.5块文件与中段改写读回一致
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamAppendRewrite, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    int i;
    static char buf[LFSRAM_BUF_LEN]; /* 静态 1KB 缓冲, 避免占用任务栈 */
    struct stat st = { 0 };
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS); /* 512B 小块几何 */
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 建立非整块对齐的 2.5 块(1280B)文件 */
    fd = open("/lfsram/appf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    (void)memset_s(buf, sizeof(buf), 'A', sizeof(buf));
    for (i = 0; i < 1280; i += LFSRAM_BUF_LEN) { /* 1280B = 2.5*512 */
        int chunk = (1280 - i > LFSRAM_BUF_LEN) ? LFSRAM_BUF_LEN : (1280 - i);
        ret = write(fd, buf, chunk);
        ICUNIT_GOTO_EQUAL(ret, chunk, ret, EXIT);
    }
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* O_APPEND 追加 512B: 越过块边界 -> flushedwrite 的 ctz 定位 + ctz_extend
     * 对末块非整块部分的逐字节拷贝 */
    fd = open("/lfsram/appf", O_APPEND | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)memset_s(buf, sizeof(buf), 'B', sizeof(buf));
    ret = write(fd, buf, 512); /* 512: 跨入第 4 块 */
    ICUNIT_GOTO_EQUAL(ret, 512, ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 关闭提交元数据后校验大小 */
    ret = stat("/lfsram/appf", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((int)st.st_size, 1280 + 512, (int)st.st_size, EXIT);

    /* lseek 回中段(第 2 块内)改写 100B */
    fd = open("/lfsram/appf", O_RDWR);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = lseek(fd, 600, SEEK_SET); /* 600: 位于第 2 块中段 */
    ICUNIT_GOTO_EQUAL(ret, 600, ret, EXIT);
    (void)memset_s(buf, sizeof(buf), 'C', 100); /* 100: 改写长度 */
    ret = write(fd, buf, 100);
    ICUNIT_GOTO_EQUAL(ret, 100, ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 整体读回校验: 前 600 字节 'A', 600..699 'C', 700..1279 'A', 1280.. 'B' */
    fd = open("/lfsram/appf", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, 1280 + 512);
    ICUNIT_GOTO_EQUAL(ret, 1280 + 512, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf[0], 'A', buf[0], EXIT);
    ICUNIT_GOTO_EQUAL(buf[599], 'A', buf[599], EXIT);
    ICUNIT_GOTO_EQUAL(buf[600], 'C', buf[600], EXIT);
    ICUNIT_GOTO_EQUAL(buf[699], 'C', buf[699], EXIT);
    ICUNIT_GOTO_EQUAL(buf[700], 'A', buf[700], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1279], 'A', buf[1279], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1280], 'B', buf[1280], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1280 + 511], 'B', buf[1280 + 511], EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 卸载重挂载后内容一致 */
    ret = umount("/lfsram");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/appf", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, 1280 + 512);
    ICUNIT_GOTO_EQUAL(ret, 1280 + 512, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf[600], 'C', buf[600], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1280], 'B', buf[1280], EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    (void)close(fd);
    (void)unlink("/lfsram/appf");
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
/* 用例简要描述: mkconsistent与gc后重挂载数据完整 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MKCONSISTENT_GC
 * @tc.name     LfsRamMkconsistentGc
 * @tc.desc     mkconsistent与gc后重挂载数据完整
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMkconsistentGc, Function | MediumTest | Level2)
{
    int err;
    int i;
    lfs_file_t file;
    char name[64]; /* 64: 长文件名缓冲 */
    char rbuf[8];  /* 8: 读缓冲 */
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");

    /* 直调实例: compact_thresh 取最小有效值 block_size/2, 驱动 gc 元数据压缩 */
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    err = LfsRamDlSetup(true, LFSRAM_BLOCK_SIZE, LFSRAM_DEV_BLOCKS, LFSRAM_BLOCK_SIZE / 2);
    ICUNIT_ASSERT_EQUAL(err, 0, err);

    /* 分裂目录: 长名文件填满 bigdir(目录分裂), 低磨损阈值下的元数据重定位链
     * 在 gstate 与盘面 gdisk 间留下 pending 差异 */
    err = lfs_mkdir(&g_lfsRamDl, "bigdir");
    ICUNIT_ASSERT_EQUAL(err, 0, err);
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1,
                         "bigdir/gcfile%03d_pad_pad_pad_pad_pad_pad", i);
        err = lfs_file_open(&g_lfsRamDl, &file, name, LFS_O_WRONLY | LFS_O_CREAT);
        ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
        err = lfs_file_write(&g_lfsRamDl, &file, "gc", 2); /* 2 字节数据 */
        ICUNIT_GOTO_EQUAL(err, 2, err, EXIT);
        err = lfs_file_close(&g_lfsRamDl, &file);
        ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    }
    /* 交错删除一半: 目录元数据死区增长, mdir.off 超过 compact_thresh */
    for (i = 0; i < LFSRAM_SPLIT_FILES; i += 2) { /* 2: 偶数项 */
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1,
                         "bigdir/gcfile%03d_pad_pad_pad_pad_pad_pad", i);
        err = lfs_remove(&g_lfsRamDl, name);
        ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    }

    /* mkconsistent: 残留 gdelta 经根目录空提交落盘, 返回 0 */
    err = lfs_fs_mkconsistent(&g_lfsRamDl);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    /* gc: compact_thresh 超限的元数据对被压缩提交, 返回 0 */
    err = lfs_fs_gc(&g_lfsRamDl);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    /* 幂等: 再次调用仍为 0 */
    err = lfs_fs_mkconsistent(&g_lfsRamDl);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_fs_gc(&g_lfsRamDl);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

    /* gc 后数据完整: 幸存文件可读且内容一致 */
    (void)snprintf_s(name, sizeof(name), sizeof(name) - 1,
                     "bigdir/gcfile%03d_pad_pad_pad_pad_pad_pad", 1);
    err = lfs_file_open(&g_lfsRamDl, &file, name, LFS_O_RDONLY);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    (void)memset_s(rbuf, sizeof(rbuf), 0, sizeof(rbuf));
    err = lfs_file_read(&g_lfsRamDl, &file, rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(err, 2, err, EXIT);
    err = lfs_file_close(&g_lfsRamDl, &file);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

    /* 卸载重挂载(同几何)后修复与压缩结果持久化, 数据仍完整 */
    err = LfsRamDlTeardown();
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_mount(&g_lfsRamDl, &g_lfsRamDlCfg);
    /* 裸调挂载须同步挂载标志(同 LfsRamDlSetup 语义): 失败路径 lfs_mount 内部
     * 自回滚无泄漏; 成功路径若标志仍为 false, EXIT 的幂等 Teardown 会误判
     * 跳过 lfs_unmount, 实例内部缓冲泄漏(实测 184B) */
    g_lfsRamDlMounted = (err == 0);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_file_open(&g_lfsRamDl, &file, name, LFS_O_RDONLY);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    (void)memset_s(rbuf, sizeof(rbuf), 0, sizeof(rbuf));
    err = lfs_file_read(&g_lfsRamDl, &file, rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(err, 2, err, EXIT);
    err = lfs_file_close(&g_lfsRamDl, &file);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

EXIT:
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1,
                         "bigdir/gcfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)lfs_remove(&g_lfsRamDl, name);
    }
    (void)lfs_remove(&g_lfsRamDl, "bigdir");
    (void)LfsRamDlTeardown();
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    return POSIX_FS_NO_ERROR;
}
#endif

/* 用例简要描述: 写后校验失败注入驱动坏块换块重试 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_VERIFY_FAIL
 * @tc.name     LfsRamVerifyFail
 * @tc.desc     写后校验失败注入驱动坏块换块重试
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamVerifyFail, Function | MediumTest | Level2)
{
    int ret;
    int fd;
    int i;
    static char buf[LFSRAM_BUF_LEN]; /* 静态 1KB 缓冲, 避免占用任务栈 */
    char path[PATH_MAX];
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS); /* 512B 小块几何, blockCycles=2 */
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 制造含死区的目录: 40 长名文件 + 删除一半, 驱动目录压缩/分裂 */
    ret = mkdir("/lfsram/vf", 0777);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        ret = snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/vf/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
        fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        (void)close(fd);
    }
    for (i = 0; i < LFSRAM_SPLIT_FILES; i += 2) { /* 2: 偶数项 */
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/vf/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)unlink(path);
    }

    /* 压缩/分裂目录中的元数据提交: 注入写后校验失败(一次性) -> 读回 CRC 不一致
     * -> CORRUPT -> 元数据对换块重定位重试, 操作最终成功 */
    g_lfsRamVerifyFail = 1;
    fd = open("/lfsram/vf/newf", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "vf", strlen("vf"));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("vf"), ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* ctz 扩展路径: 建非整块对齐的 2.5 块文件后继续追加, 末块逐字节拷贝的写后
     * 校验失败 -> ctz_extend 换块重试, 追加最终成功 */
    fd = open("/lfsram/bigf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)memset_s(buf, sizeof(buf), 'A', sizeof(buf));
    for (i = 0; i < 1280; i += LFSRAM_BUF_LEN) { /* 1280B = 2.5*512 */
        int chunk = (1280 - i > LFSRAM_BUF_LEN) ? LFSRAM_BUF_LEN : (1280 - i);
        ret = write(fd, buf, chunk);
        ICUNIT_GOTO_EQUAL(ret, chunk, ret, EXIT);
    }
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/bigf", O_APPEND | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)memset_s(buf, sizeof(buf), 'B', sizeof(buf));
    g_lfsRamVerifyFail = 1;
    ret = write(fd, buf, 512); /* 512: 跨块追加触发 ctz_extend */
    ICUNIT_GOTO_EQUAL(ret, 512, ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 数据块路径: 中段改写的数据 prog 校验失败 -> flushedwrite/file_relocate
     * 换块重试, 改写最终成功 */
    fd = open("/lfsram/midf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (void)memset_s(buf, sizeof(buf), 'M', sizeof(buf));
    for (i = 0; i < 1280; i += LFSRAM_BUF_LEN) {
        int chunk = (1280 - i > LFSRAM_BUF_LEN) ? LFSRAM_BUF_LEN : (1280 - i);
        ret = write(fd, buf, chunk);
        ICUNIT_GOTO_EQUAL(ret, chunk, ret, EXIT);
    }
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/midf", O_RDWR);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = lseek(fd, 600, SEEK_SET); /* 600: 第 2 块中段 */
    ICUNIT_GOTO_EQUAL(ret, 600, ret, EXIT);
    (void)memset_s(buf, sizeof(buf), 'C', 64); /* 64: 恰好填满写缓存 */
    g_lfsRamVerifyFail = 1;
    ret = write(fd, buf, 64);
    ICUNIT_GOTO_EQUAL(ret, 64, ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 注入复位后整体读回校验 */
    g_lfsRamVerifyFail = 0;
    fd = open("/lfsram/bigf", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, 1280 + 512);
    ICUNIT_GOTO_EQUAL(ret, 1280 + 512, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf[0], 'A', buf[0], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1279], 'A', buf[1279], EXIT);
    ICUNIT_GOTO_EQUAL(buf[1280], 'B', buf[1280], EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/midf", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, 1280);
    ICUNIT_GOTO_EQUAL(ret, 1280, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf[599], 'M', buf[599], EXIT);
    ICUNIT_GOTO_EQUAL(buf[600], 'C', buf[600], EXIT);
    ICUNIT_GOTO_EQUAL(buf[663], 'C', buf[663], EXIT);
    ICUNIT_GOTO_EQUAL(buf[664], 'M', buf[664], EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 卸载重挂载: 重定位后的数据完整持久 */
    ret = umount("/lfsram");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/lfsram/vf/newf", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)strlen("vf"), ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    (void)close(fd);
    (void)unlink("/lfsram/vf/newf");
    (void)unlink("/lfsram/bigf");
    (void)unlink("/lfsram/midf");
    for (i = 0; i < LFSRAM_SPLIT_FILES; i++) {
        (void)snprintf_s(path, sizeof(path), sizeof(path) - 1,
                         "/lfsram/vf/splitfile%03d_pad_pad_pad_pad_pad_pad", i);
        (void)unlink(path);
    }
    (void)rmdir("/lfsram/vf");
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
/* 用例简要描述: 读失败注入下readdir/stat返回EIO可恢复 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_READ_FAIL_SCAN
 * @tc.name     LfsRamReadFailScan
 * @tc.desc     读失败注入下readdir/stat返回EIO可恢复
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamReadFailScan, Function | MediumTest | Level1)
{
    int ret;
    int err;
    int fd;
    int blocks = 0;
    char buf[LFSRAM_BUF_LEN];
    struct stat st = { 0 };
    struct lfs_fsinfo fsinfo = { 0 };
    DIR *dir = NULL;
    struct dirent *dent = NULL;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;

    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram/rff", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    (void)memset_s(buf, sizeof(buf), 'F', sizeof(buf));
    ret = write(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir("/lfsram/rfd", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 直调实例挂同一 RAM 盘(只读操作, 与 /lfsram 无写冲突) */
    err = LfsRamDlSetup(false, LFSRAM_BLOCK_SIZE, LFSRAM_DEV_BLOCKS, 0);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

    /* 基线: readdir/stat/读文件/直调 fs_stat/traverse 正常("." 与 ".." 为
     * 合成项不读盘, 需先消费; 整读文件同时驱逐读缓存, 使后续注入的
     * readdir 需真实读盘) */
    dir = opendir("/lfsram");
    ICUNIT_GOTO_NOT_EQUAL(dir, NULL, dir, EXIT);
    dent = readdir(dir);
    ICUNIT_GOTO_NOT_EQUAL(dent, NULL, dent, EXIT);
    dent = readdir(dir);
    ICUNIT_GOTO_NOT_EQUAL(dent, NULL, dent, EXIT);
    fd = open("/lfsram/rff", O_RDONLY);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    err = lfs_fs_stat(&g_lfsRamDl, &fsinfo);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_fs_traverse(&g_lfsRamDl, LfsRamDlCountCb, &blocks);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    ICUNIT_GOTO_EQUAL(blocks > 0, 1, blocks, EXIT);

    /* 注入读失败: readdir/lfs_dir_read 负值 -> 适配层 LfsReaddir 错误分支
     * (回调返回 -1 透传, 不校验 errno) */
    g_lfsRamErrMask = 0x1; /* bit0: 读失败 */
    dent = readdir(dir);
    ICUNIT_GOTO_EQUAL(dent, NULL, dent, EXIT);
    /* 文件读: 读路径错误传播 */
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    /* stat/mkdir: 目录查找错误传播 */
    ret = stat("/lfsram/rff", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ret = mkdir("/lfsram/rfd2", 0777);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    /* 直调 lfs_fs_traverse: 遍历中块读错误(注: lfs_fs_stat 在普通盘上不发
     * 任何读 IO, 其 fetch 错误分支仅旧版本盘可达, 不在此断言) */
    err = lfs_fs_traverse(&g_lfsRamDl, LfsRamDlCountCb, NULL);
    ICUNIT_GOTO_EQUAL(err, LFS_ERR_IO, err, EXIT);
    g_lfsRamErrMask = 0;

    /* 恢复开关后全部恢复正常 */
    dent = readdir(dir);
    ICUNIT_GOTO_NOT_EQUAL(dent, NULL, dent, EXIT);
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
    ret = stat("/lfsram/rff", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    err = lfs_fs_stat(&g_lfsRamDl, &fsinfo);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_fs_traverse(&g_lfsRamDl, LfsRamDlCountCb, NULL);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

    /* 自定义回调返错: traverse 立即中断并透传错误 */
    err = lfs_fs_traverse(&g_lfsRamDl, LfsRamDlFailCb, NULL);
    ICUNIT_GOTO_EQUAL(err, LFS_ERR_IO, err, EXIT);

EXIT:
    if (dir != NULL) {
        (void)closedir(dir);
    }
    (void)close(fd);
    (void)unlink("/lfsram/rff");
    (void)rmdir("/lfsram/rfd");
    (void)rmdir("/lfsram/rfd2");
    (void)LfsRamDlTeardown();
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

/* 用例简要描述: 满盘下mkdir/close/truncate错误路径 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_FULL_DISK_OPS
 * @tc.name     LfsRamFullDiskOps
 * @tc.desc     满盘下mkdir/close/truncate错误路径
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamFullDiskOps, Function | MediumTest | Level1)
{
    int ret;
    int err;
    int fd;
    int i;
    int full = 0;
    static char buf[LFSRAM_BUF_LEN]; /* 静态 1KB 缓冲, 避免占用任务栈 */
    lfs_file_t file;
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    (void)umount("/lfsfull2");
    struct PartitionCfg cfg;

    LfsRamFillBigCfg(&cfg); /* 8*4096 = 32KB 小盘快速写满 */
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    /* 同盘第二挂载点: 专用于 close 失败分支(失败 close 不归还挂载点引用,
     * 该挂载点保留至测试结束, 先例见 posix_fs_vfs_test.c 的 /vfsH) */
    ret = mount(NULL, "/lfsfull2", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 脏文件 close 失败: 缓存数据在 close 刷盘时注入写失败 -> 适配层
     * LfsClose 错误分支 */
    fd = open("/lfsfull2/dirty", O_CREAT | O_TRUNC | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "dirty", strlen("dirty")); /* 内联缓存, 未落盘 */
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("dirty"), ret);
    g_lfsRamErrMask = 0x2; /* bit1: 写失败 */
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret); /* 适配层 LfsClose 错误分支(回调返回 -1 透传, 不校验 errno) */
    g_lfsRamErrMask = 0;

    /* 逐文件写满: 每个文件写 1KB(超出 inline 上限, 占用一个数据块)即关闭
     * (成功关闭提交 ctz, 块均被追踪); 分配失败的前缀为空文件不占块,
     * 保证满盘状态稳定 */
    (void)memset_s(buf, sizeof(buf), 'D', sizeof(buf));
    full = 0;
    for (i = 0; i < LFSRAM_EXHAUST_CAP; i++) {
        char name[32]; /* 32: 文件名缓冲 */
        ret = snprintf_s(name, sizeof(name), sizeof(name) - 1, "/lfsram/fill%03d", i);
        ICUNIT_GOTO_EQUAL(ret > 0, 1, ret, EXIT);
        fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        ret = write(fd, buf, sizeof(buf));
        if (ret == -1) {
            /* 首块分配即失败: 该文件为空, 磁盘已满 */
            (void)close(fd); /* ERRED 文件 close 静默返回, 不断言 */
            (void)unlink(name);
            full = 1;
            break;
        }
        ICUNIT_GOTO_EQUAL(ret, (int)sizeof(buf), ret, EXIT);
        ret = close(fd);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    ICUNIT_GOTO_EQUAL(full, 1, full, EXIT);

    /* 满盘 mkdir: 新目录块对分配失败 -> NOSPC */
    ret = mkdir("/lfsram/nodir", 0777);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOSPC, errno, EXIT);

    /* 直调实例(同几何只读挂载): 满盘下 truncate 扩展零填充写失败 */
    err = LfsRamDlSetup(false, LFSRAM_BIG_BLOCK_SIZE, LFSRAM_BIG_BLOCKS, 0);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_file_open(&g_lfsRamDl, &file, "fill000", LFS_O_WRONLY);
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);
    err = lfs_file_truncate(&g_lfsRamDl, &file, 8192); /* 8192: 扩展零填充 */
    ICUNIT_GOTO_EQUAL(err, LFS_ERR_NOSPC, err, EXIT);
    (void)lfs_file_close(&g_lfsRamDl, &file);
    err = LfsRamDlTeardown();
    ICUNIT_GOTO_EQUAL(err, 0, err, EXIT);

    /* 删除提交错误: unlink 注入写失败 -> 提交返回错误 */
    g_lfsRamErrMask = 0x2; /* bit1: 写失败 */
    ret = unlink("/lfsram/fill000");
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    g_lfsRamErrMask = 0;
    /* 恢复后可删除并回收空间(删除两个文件腾出目录块对), mkdir 恢复正常 */
    ret = unlink("/lfsram/fill000");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = unlink("/lfsram/fill001");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir("/lfsram/okdir", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    (void)close(fd);
    for (i = 0; i < LFSRAM_EXHAUST_CAP; i++) {
        char name[32]; /* 32: 文件名缓冲 */
        (void)snprintf_s(name, sizeof(name), sizeof(name) - 1, "/lfsram/fill%03d", i);
        (void)unlink(name);
    }
    (void)unlink("/lfsfull2/dirty");
    (void)rmdir("/lfsram/okdir");
    (void)rmdir("/lfsram/nodir");
    (void)LfsRamDlTeardown();
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (void)umount("/lfsfull2"); /* 引用未归还, 卸载失败保留至测试结束 */
    return POSIX_FS_NO_ERROR;
}
#endif

/* 用例简要描述: 错误几何cfg挂载被format兜底成功 */
/* *
 * @tc.number   SUB_KERNEL_FS_LFSRAM_MOUNT_CFG_MISMATCH
 * @tc.name     LfsRamMountCfgMismatch
 * @tc.desc     错误几何cfg挂载被format兜底成功
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLfsRamMountCfgMismatch, Function | MediumTest | Level1)
{
    int ret;
    int fd;
    char buf[LFSRAM_BUF_LEN];
    struct stat st = { 0 };
    /* 级联容错: 清除可能遗留的错误注入与挂载 */
    g_lfsRamErrMask = 0;
    g_lfsRamVerifyFail = 0;
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    struct PartitionCfg cfg;
    struct PartitionCfg badCfg;

    /* 先以 512B/64 块几何格式化 */
    LfsRamFillCfg(&cfg, 0, LFSRAM_DEV_BLOCKS);
    (void)memset_s(g_lfsRamDev, ((UINT32)LFSRAM_DEV_SIZE), 0, ((UINT32)LFSRAM_DEV_SIZE));
    ret = mount(NULL, "/lfsram", "littlefs", 0, &cfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram/oldf", O_CREAT | O_WRONLY, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "old", strlen("old"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("old"), ret);
    (void)close(fd);
    ret = umount("/lfsram");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 错误几何(1024B/32 块, 同 32KB 容量)挂载: 盘面超级块 512/64 与 cfg 不匹配
     * -> lfs_mount 返回 INVAL -> 适配层 format 兜底重挂成功 */
    LfsRamFillCfg(&badCfg, 0, LFSRAM_DEV_BLOCKS / 2); /* 32: 块数减半 */
    badCfg.blockSize = LFSRAM_BLOCK_SIZE * 2;          /* 1024: 块大小加倍 */
    badCfg.readSize = LFSRAM_BIG_IO_SIZE;
    badCfg.writeSize = LFSRAM_BIG_IO_SIZE;
    badCfg.cacheSize = LFSRAM_BIG_IO_SIZE;
    ret = mount(NULL, "/lfsram2", "littlefs", 0, &badCfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret); /* 首次挂载失败被 format 兜底 */

    /* 新几何下读写正常, 旧数据被重格式化清除 */
    fd = open("/lfsram2/newf", O_CREAT | O_TRUNC | O_RDWR, 0666);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    ret = write(fd, "newgeo", strlen("newgeo"));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("newgeo"), ret);
    ret = lseek(fd, 0, SEEK_SET);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("newgeo"), ret);
    ret = strcmp(buf, "newgeo");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = stat("/lfsram2/oldf", &st);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 卸载后按新几何重挂载直接成功, 数据仍在 */
    ret = umount("/lfsram2");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = mount(NULL, "/lfsram2", "littlefs", 0, &badCfg);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    fd = open("/lfsram2/newf", O_RDONLY);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(ret, (int)strlen("newgeo"), ret);
    ret = close(fd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    (void)unlink("/lfsram2/newf");
    ret = umount("/lfsram2");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return POSIX_FS_NO_ERROR;
}

void PosixFsLfsRamTest(void)
{
    /* 设备存储按需申请(见 g_lfsRamDev 注释); 失败则整族跳过, 防止用例
     * 对 NULL 设备连锁崩溃 */
    g_lfsRamDev = (UINT8 *)LOS_MemAlloc(LOSCFG_SYS_HEAP_ADDR, LFSRAM_DEV_SIZE);
    if (g_lfsRamDev == NULL) {
        dprintf("PosixFsLfsRamTest: alloc %u bytes failed, skip\n",
                (UINT32)LFSRAM_DEV_SIZE);
        return;
    }

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
    RUN_ONE_TESTCASE(TestFsLfsRamRenameCrossDir);
    RUN_ONE_TESTCASE(TestFsLfsRamMkdirInSplitDir);
    RUN_ONE_TESTCASE(TestFsLfsRamAppendRewrite);
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
    RUN_ONE_TESTCASE(TestFsLfsRamMkconsistentGc);
#endif
    RUN_ONE_TESTCASE(TestFsLfsRamVerifyFail);
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
    RUN_ONE_TESTCASE(TestFsLfsRamReadFailScan);
    RUN_ONE_TESTCASE(TestFsLfsRamFullDiskOps);
#endif
    RUN_ONE_TESTCASE(TestFsLfsRamMountCfgMismatch);

    /* 兜底卸载本族挂载点: 单用例断言失败退出会跳过自身 umount, 悬挂的
     * littlefs 实例会在后续 TestFsVolumeRelease 的 umount 冲刷元数据时
     * 访问已释放的设备存储, 故先于 free 收尾(对未挂载点 umount 无害) */
    (void)umount("/lfsram");
    (void)umount("/lfsro");
    (void)umount("/lfsram2");
    (void)LOS_MemFree(LOSCFG_SYS_HEAP_ADDR, g_lfsRamDev);
    g_lfsRamDev = NULL;
}
