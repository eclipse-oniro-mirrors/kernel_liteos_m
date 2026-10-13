/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd. All rights reserved.
 * Description: hiditing 专用 yaffs 测试卷后端（TestYaffsVolumeEnsure/
 *              Release 强符号覆盖，声明见 testsuites/unittest/posix/src/
 *              kernel_test.h）。
 *
 * 套件（PosixFsYaffsTest，testsuites/unittest/posix/src/fs/yaffs/
 * posix_fs_yaffs_test.c）首尾按需挂/收卷：
 *   - Ensure: nandflash_filesystem_init(true)（fmc 探测 + 独立池安装 +
 *     "yaffs" fs 注册 + 挂 /boot，nand_init.c:169-182），随后无条件
 *     nand_yaffs_remount(0, 1) 重格重挂 —— /boot 为测试卷且套件无持久化
 *     用例，每轮从确定性干净盘开始，消除持久卷跨复位残留状态触发的
 *     闭源库非确定性崩溃（RwAppend 0xdeadbeef，实测同位图一崩一过）。
 *   - Release: 恒 0 —— 保持挂载态。yaffs umount 会连带 yaffs_end 撤设备
 *     （vfs_yaffs2.c:134-147），重挂需全扫描且 checkpoint 丢失，测试产品
 *     一次挂载保持到复位即可；幂等标志保证重复调用零开销。
 *
 * 内存：/boot 单卷预算 124KB 池（config.gni board_nand_yaffs_pool_bytes，
 * fbb 固化 n_caches=20 × 2112B ≈ 42KB + dev/checkpoint/杂项；池已动态化
 * —— board_nand_yaffs_pool_dynamic，backend_init 时从系统堆申请）。
 *
 * 破坏性说明：无条件重格会清掉 C4 镜像卷 —— C4 测试每次启动整卷擦除
 * 重烧（nand_yaffs_c4.c:86-135），主产品侧自愈，无永久影响。
 */

#ifdef LOSCFG_TEST

#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "los_debug.h"

/* fmc 侧契约（同 board/main.c:29 的 extern 形态） */
extern int nandflash_filesystem_init(bool part_boot);
extern int nand_yaffs_remount(unsigned int part_no, int reformat);

static int g_yaffsTestMounted;

int TestYaffsVolumeEnsure(void)
{
    int ret;

    if (g_yaffsTestMounted != 0) {
        return 0;
    }

    /* fbb 对齐两阶段 API：phase true = FMC 探测 + 池 + 注册 + 挂 /boot。
     * nand_yaffs_register_volumes/nand_yaffs_mount 内部幂等，重复调用安全。 */
    ret = nandflash_filesystem_init(true);
    if (ret != 0) {
        PRINTK("nand_yaffs_test: filesystem init fail ret=0x%x\n", ret);
        return -1;
    }

    /* 无条件重格：/boot 为测试卷且 yaffs 套件无持久化用例，每轮从确定性
     * 干净盘开始——消除持久卷跨复位残留状态（上次崩溃遗留的半写文件/
     * orphan 块/扫描序列差异）触发的闭源库非确定性崩溃。重格后重挂走
     * 全扫描，代价 ~1-2s。 */
    ret = nand_yaffs_remount(0, 1);
    if (ret != 0) {
        PRINTK("nand_yaffs_test: reformat remount fail ret=%d\n", ret);
        return -1;
    }

    g_yaffsTestMounted = 1;
    PRINTK("nand_yaffs_test: /boot ready (yaffs POSIX suite volume, reformatted)\n");
    return 0;
}

int TestYaffsVolumeRelease(void)
{
    /* 保持挂载：umount 连带 yaffs_end 撤设备，重挂走全扫描且丢 checkpoint；
     * 测试产品挂载保持到复位，重复调用幂等返回。 */
    return 0;
}

#endif /* LOSCFG_TEST */
