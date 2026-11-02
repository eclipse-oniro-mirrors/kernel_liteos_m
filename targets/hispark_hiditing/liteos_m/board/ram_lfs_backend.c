/*
 * hi3322_testsuits 专用 RAM littlefs 测试后端
 * （XTS_TESTLOG_FAILURE_ANALYSIS.md 方案 B：按套件挂/卸 TEST_ROOT 卷）。
 *
 * FS/stdio 域用例经 TEST_ROOT="/littlefs" 访问文件系统（posix_fs_test.h 由
 * LOSCFG_SUPPORT_LITTLEFS 选定路径）。本文件提供 TestFsVolumeEnsure/Release
 * 强符号，由 PosixStdioFuncTest()/PosixFsFuncTest() 首尾调用：套件运行期挂载、
 * 结束即卸载并归还内存，套件外零占用。
 *
 * 后端选型：RAM 虚拟 flash（qemu arm_mps2_an386 的 fs_init.c + ram_virt_flash.c
 * 同构）——
 *   - 不落 NOR：hi3322 与 hi3322_testsuits 两套固件共享同一块 NOR flash，
 *     测试写入会毁掉主产品 /nboot /nsystem /nuser 卷数据（nor_lfs_bridge.c
 *     卷规划）；
 *   - 每次挂载前整盘清 0xFF，空盘使 LfsMount 自动走 format 兜底
 *     （lfs_adapter.c: mount 失败 → format → 重挂），套件之间天然干净盘；
 *   - 盘体从内核堆懒分配。系统堆仅 ~180KB（实测池 0x2cd60，同
 *     It_los_dynload_011.c "hi3322 系统堆仅 ~190KB"），旧 128KB(32×4096)
 *     单块申请已失败（shell 套件期 free 0x1f5b8/最大空闲节点 0x1e5a4 均小于
 *     请求 0x20010，ItLosShell012-015 全体 mkdir ENOENT）——历史 "470KB 堆"
 *     结论随测试 bss 增长失效。改用 ws63 同款 2048B 块几何：默认 24 块/48KB
 *     覆盖 shell 012-014 具具与 FS/stdio 域，80KB 级申请（dynload 20×4096
 *     先例）在本堆实测可满足；块数与块大小解耦（littlefs 每目录固定 2 块
 *     元数据对），4096B 块下 Shell015 需 ~36 块=144KB 同样超预算。
 *
 * 约束：lfs_adapter.c 的 g_partitionCfg 是全局单套块回调（LfsMount 覆盖式
 * 赋值），同一镜像只能存在一套 littlefs 块后端。测试产品（LOSCFG_TEST）
 * 不挂 NOR/NAND 卷，无共存冲突；若未来需要共存，须先改造 lfs_adapter 的
 * 回调多实例化。
 */

#ifdef LOSCFG_TEST

#include <errno.h>
#include <string.h>

#include "los_fs.h"
#include "los_printf.h"
#include "securec.h"

#define RAM_LFS_BLOCK_SIZE   2048U
#define RAM_LFS_BLOCK_COUNT  24U /* 48KB/22 数据块默认几何(同 ws63): shell 012-014 具具
                                  * (~14-22 目录块,含 cp 深路径)与 FS/stdio 域(~6 块)共用;
                                  * 128KB(32×4096) 旧几何在 ~180KB 堆上单块申请失败(见文件头注) */
#define RAM_LFS_BLOCK_MAX    64U /* 域级盘几何上限: Shell015 需 ~36 块(17 mkdir×每目录 2 元数据块)
                                  * 由 It_los_shell.c 按域上调至 40 块/80KB(套件结束复位默认几何) */
#define RAM_LFS_DEV_NAME     "ram0"
#define RAM_LFS_MOUNT_POINT  "/littlefs"

static UINT8 *g_ramLfsDisk;
static int g_ramLfsDiskRegistered;
static int g_ramLfsMounted;
static UINT32 g_ramLfsBlocks = RAM_LFS_BLOCK_COUNT;

void RamLfsSetBlocks(UINT32 blocks)
{
    if ((blocks >= 4U) && (blocks <= RAM_LFS_BLOCK_MAX)) {
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

int TestFsVolumeEnsure(void)
{
    static int lengths[1];
    static int addrs[1];
    struct PartitionCfg cfg;
    int ret;

    if (g_ramLfsMounted) {
        return 0;
    }

    if (g_ramLfsDisk == NULL) {
        g_ramLfsDisk = (UINT8 *)LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR,
                                             RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks);
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
    cfg.readSize = 256; /* 256, qemu fs_init.c 同款：小 cache 控制挂载内存。
                         * 注(cache 4096 实验结论,XTS_FS_RAMDISK_PLAN.md 6.6):
                         * 大 cache 使 littlefs 每次元数据提交整块填充,擦除次数
                         * 13→72/轮、墙钟反而变慢,故保持 256。 */
    cfg.writeSize = 256;
    cfg.blockSize = (int)RAM_LFS_BLOCK_SIZE;
    cfg.blockCount = (int)g_ramLfsBlocks;
    cfg.cacheSize = 256;
    cfg.partNo = 0;
    cfg.lookaheadSize = 16; /* 16, qemu fs_init.c 同款 */
    cfg.blockCycles = 1000; /* 1000, RAM 盘无磨损语义，取 qemu 同款 */

    ret = mount(NULL, RAM_LFS_MOUNT_POINT, "littlefs", 0, &cfg);
    if (ret != 0) {
        return -1;
    }

    g_ramLfsMounted = 1;
    PRINTK("ram_lfs: %s mounted (%uKB ram disk)\n", RAM_LFS_MOUNT_POINT,
        (UINT32)(RAM_LFS_BLOCK_SIZE * g_ramLfsBlocks / 1024U));
    return 0;
}

int TestFsVolumeRelease(void)
{
    int ret;

    if (!g_ramLfsMounted) {
        return 0;
    }

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
    return 0;
}

#endif /* LOSCFG_TEST */
