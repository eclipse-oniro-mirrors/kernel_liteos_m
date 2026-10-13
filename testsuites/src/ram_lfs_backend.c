/*
 * ws63_m_testsuits 专用 RAM littlefs 测试后端（测试代码, 位于 testsuites/src;
 * 复制 hi3322 ram_lfs_backend.c 模式：按套件挂/卸 TEST_ROOT 卷）。
 *
 * 背景：ws63_m_testsuits 产品原本无任何文件系统挂载（LOSCFG_FS_LITTLEFS 未开），
 * FS/stdio 域用例经 TEST_ROOT="/littlefs" 访问文件系统（posix_fs_test.h 由
 * LOSCFG_SUPPORT_LITTLEFS 选定路径）。产品若开启 LOSCFG_FS_LITTLEFS
 * （vendor/ws63_m_testsuits/kernel_configs/debug.config），本文件提供
 * TestFsVolumeEnsure/Release 强符号（posix_test.c:96 弱符号兜底），
 * 由 PosixStdioFuncTest()/PosixFsFuncTest() 首尾调用：套件运行期挂载、结束即
 * 卸载并归还内存，套件外零占用；产品未开启 littlefs 时强符号降级返回 0
 * （弱默认语义），stdio 域非文件后端用例照常执行。
 *
 * 后端选型：RAM 虚拟 flash（不落 ws63 真实 flash；测试写入不允许破坏产品分区）。
 * 挂载时机：惰性——首个调用 TestFsVolumeEnsure 的套件挂载, TestFsVolumeRelease
 * 卸载归还。盘不能常驻: ws63 测试堆余量在 pthread 域仅 ~42KB, 常驻盘会挤压
 * XTS pthread 用例的 8KB 线程栈分配(实测 48KB/32KB 常驻盘均致挂死)。
 *   - 每次挂载前整盘清 0xFF，空盘使 LfsMount 自动走 format 兜底
 *     （lfs_adapter.c: mount 失败 → format → 重挂），套件之间天然干净盘；
 *   - 盘体从内核堆懒分配（ws63 测试产品堆约 296KB，board.ld g_intheap_size），
 *     96KB 单块分配余量充足（hi3322 先例：470KB 堆用 128KB）。
 *
 * 约束：lfs_adapter.c 的 g_partitionCfg 是全局单套块回调（LfsMount 覆盖式
 * 赋值），同一镜像只能存在一套 littlefs 块后端。测试产品（LOSCFG_TEST）
 * 不挂 NOR/NAND 卷，无共存冲突（同 hi3322 结论）。
 */

/* 测试代码承载(仅 testsuites): ws63_m_testsuits 产品的 RAM littlefs 后端。
 * 仅 ws63 平台编入——qemu 由 board fs_init.c 启动挂载 /littlefs、hi3322 由其
 * board/ram_lfs_backend.c 提供同符号后端(曾因 ARCH_RISCV 宽门控在 hi3322_testsuits
 * 上重复定义, 已收窄到平台宏), 其它板本文件编译为空,
 * TestFsVolumeEnsure/Release 走 posix_test.c 弱默认。 */
#if defined(LOSCFG_TEST) && defined(LOSCFG_PLATFORM_WS63_M)

#include <errno.h>
#include <string.h>

#include "los_fs.h"
#include "los_printf.h"
#include "securec.h"

#define RAM_LFS_BLOCK_SIZE   2048U
#define RAM_LFS_BLOCK_COUNT  24U /* 48KB/22 数据块: shell 具具(~12-16 目录块)与 FS 域(LfsRam 家族
                                  * 实测需 >6 数据块, 16KB 盘 Lfs 家族 7 例失败)共用同一几何;
                                  * ws63 fs 域期堆空闲 ~92KB, 48KB 盘+余量经 run8/run9 两轮实测标定 */ /* 48KB/2KB 块(22 数据块): shell vfs 用例具具需 ~12-16 目录块
                                  * (含 tc12/13 残留与 cp 深路径), 32KB/16 块实测 ENOSPC(ItLosShell014
                                  * 长名目录 237 行); FS 域 ~6 块。惰性挂载+真实释放: shell 套件期空闲
                                  * ~90KB 可容纳 48KB, pthread 域运行期盘已归还堆余量与无盘基线一致 */
#define RAM_LFS_DISK_BYTES   (RAM_LFS_BLOCK_SIZE * RAM_LFS_BLOCK_COUNT)
#define RAM_LFS_DEV_NAME     "ram0"
#define RAM_LFS_MOUNT_POINT  "/littlefs"

static UINT8 *g_ramLfsDisk;
static int g_ramLfsDiskRegistered;
static int g_ramLfsMounted;
static UINT32 g_ramLfsBlocks = RAM_LFS_BLOCK_COUNT;

/* 域级盘几何调节: shell vfs 具具(每用例 ~10-22 目录块)需要比 FS 域(6 块)更大的盘,
 * 两域盘均惰性挂载且套件结束真实释放, 互不重叠 */
void RamLfsSetBlocks(UINT32 blocks)
{
    if ((blocks >= 4U) && (blocks <= 64U)) {
        g_ramLfsBlocks = blocks;
    }
}

static int RamLfsRead(int partition, UINT32 *offset, void *buf, UINT32 size)
{
    (void)partition;
    if (memcpy_s(buf, size, g_ramLfsDisk + *offset, size) != EOK) {
        return -1;
    }
    return 0;
}

static int RamLfsWrite(int partition, UINT32 *offset, const void *buf, UINT32 size)
{
    (void)partition;
    if (memcpy_s(g_ramLfsDisk + *offset, size, buf, size) != EOK) {
        return -1;
    }
    return 0;
}

static int RamLfsErase(int partition, UINT32 offset, UINT32 size)
{
    (void)partition;
    if (memset_s(g_ramLfsDisk + offset, size, 0xFF, size) != EOK) {
        return -1;
    }
    return 0;
}

static void RamLfsHeapStat(const char *tag)
{
#ifdef LOSCFG_TEST
    LOS_MEM_POOL_STATUS memInfo = {0};
    if (LOS_MemInfoGet((VOID *)OS_SYS_MEM_ADDR, &memInfo) == LOS_OK) {
        PRINTK("ram_lfs[%s]: pool free=%u maxNode=%u\n", tag,
               (UINT32)memInfo.totalFreeSize, (UINT32)memInfo.maxFreeNodeSize);
    }
#endif
}

int TestFsVolumeEnsure(void)
{
    static int lengths[1];
    static int addrs[1];
    struct PartitionCfg cfg;
    int ret;

#ifndef LOSCFG_FS_LITTLEFS
    /* 产品配置未开启 littlefs(如 ws63_m_testsuits 默认配置): RAM 盘后端不可用,
     * 降级为 posix_test.c 弱默认语义(返回 0), stdio 域非文件后端用例
     * (printf/perror/snprintf 等)照常执行; fs 域用例由域配置门控本就不执行 */
    return 0;
#endif

    if (g_ramLfsMounted) {
        return 0;
    }
    RamLfsHeapStat("ensure-begin");

    if (g_ramLfsDisk == NULL) {
        g_ramLfsDisk = (UINT8 *)LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks);
        if (g_ramLfsDisk == NULL) {
            PRINTK("ram_lfs: alloc %u bytes failed\n", (UINT32)(RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks));
            return -1;
        }
    }

    /* 干净盘：全 0xFF → 无 superblock → LfsMount 内部 format 兜底后挂载成功 */
    if (memset_s(g_ramLfsDisk, RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks, 0xFF,
                  RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks) != EOK) {
        return -1;
    }

    if (!g_ramLfsDiskRegistered) {
        lengths[0] = (int)(RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks);
        addrs[0] = 0; /* 分区起始偏移：块回调直接以 offset 索引盘体 */
        ret = LOS_DiskPartition(RAM_LFS_DEV_NAME, "littlefs", lengths, addrs, 1);
        if (ret != 0) {
            return ret;
        }
        g_ramLfsDiskRegistered = 1;
    }

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.readFunc = RamLfsRead;
    cfg.writeFunc = RamLfsWrite;
    cfg.eraseFunc = RamLfsErase;
    /* 256/16/1000：qemu fs_init.c 与 hi3322 同款（小 cache 控制挂载内存，
     * 大 cache 反而擦除次数上升，见 hi3322 文件头注释引 XTS_FS_RAMDISK_PLAN 6.6） */
    cfg.readSize = 256;
    cfg.writeSize = 256;
    cfg.blockSize = (int)RAM_LFS_BLOCK_SIZE;
    cfg.blockCount = (int)g_ramLfsBlocks;
    cfg.cacheSize = 256;
    cfg.partNo = 0;
    cfg.lookaheadSize = 16;
    cfg.blockCycles = 1000;

    ret = mount(NULL, RAM_LFS_MOUNT_POINT, "littlefs", 0, &cfg);
    if (ret != 0) {
        PRINTK("ram_lfs: mount %s ret=%d errno=%d\n", RAM_LFS_MOUNT_POINT, ret, errno);
        return -1;
    }

    g_ramLfsMounted = 1;
    RamLfsHeapStat("ensure-end");
    PRINTK("ram_lfs: %s mounted (%uKB ram disk)\n", RAM_LFS_MOUNT_POINT,
        (UINT32)(RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks / 1024U));
    return 0;
}

/*
 * 惰性挂载语义（同 hi3322）：套件首尾 Ensure/Release 挂/卸并归还盘体。
 * 消费方: It_los_shell.c(shell vfs 用例, 先于 FS 域) 与 posix_test.c(stdio/fs 域)。
 * pthread 域(两消费方之间)运行期盘已归还, 堆余量与无盘基线一致
 * (ws63 实测: 盘常驻时 XTS pthread 用例 0x2024 栈分配失败挂死, 见文件头注)。
 */
int TestFsVolumeRelease(void)
{
    int ret;

    if (!g_ramLfsMounted) {
        PRINTK("ram_lfs[release]: not mounted\n");
        return 0;
    }
    RamLfsHeapStat("release-begin");

    ret = umount(RAM_LFS_MOUNT_POINT);
    if (ret != 0) {
        PRINTK("ram_lfs: umount %s ret=%d errno=%d\n", RAM_LFS_MOUNT_POINT, ret, errno);
        return -1;
    }

    g_ramLfsMounted = 0;

    /* 归还盘体，套件外零占用；设备表注册项（几十字节）保留供下次挂载复用 */
    if (LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, g_ramLfsDisk) != LOS_OK) {
        PRINTK("ram_lfs: free disk failed\n");
        return -1;
    }
    g_ramLfsDisk = NULL;
    RamLfsHeapStat("release-end");
    return 0;
}

#endif /* LOSCFG_TEST && LOSCFG_PLATFORM_WS63_M */
