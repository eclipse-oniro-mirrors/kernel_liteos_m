/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_shell.h"
#include "shcmd.h"
#include <unistd.h>

#if defined(LOSCFG_SHELL) && defined(LOSCFG_SHELL_LK)

/* shell_lk.c 私有级别定义镜像(头文件 It_los_shell.h 已同步): 0:EMG 1:COMMON 2:ERROR
 * 3:WARN 4:INFO 5:DEBUG, 默认 TRACE_DEFAULT=2 */
#define LK017_LEVEL_ERROR 2
#define LK017_LEVEL_WARN  3
#define LK017_LEVEL_DEBUG  5
#define LK017_LOG_FILE    "/littlefs/lk017.log"
#define LK017_PROBE_FILE  "/littlefs/lk017probe"

/* 根挂载段依赖 qemu 板级 virt_flash 回调(ram_virt_flash.h 为 qemu 专用头文件),
 * 仅在 qemu 产品编译; 其他启用 littlefs 的板(如 hi3322)不进入本段 */
#if defined(LOSCFG_FS_LITTLEFS) && (LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4 == 1)
#define LK017_QEMU_ROOTFS 1
#include <fcntl.h>
#include <sys/stat.h>
#include "los_fs.h"
#include "flash/ram_virt_flash.h"
#else
#define LK017_QEMU_ROOTFS 0
#endif

#if (LK017_QEMU_ROOTFS == 1)

/* 32KB RAM 盘几何, 用于在 "/" 临时挂载 littlefs 覆盖根目录枚举分支;
 * littlefs 适配层的 g_partitionCfg 为全局单例, 挂载会替换全局回调,
 * 故该段先卸载 /littlefs, 结束后用板级 virt_flash 回调原样重挂恢复 */
#define LK017_RAM_BLOCKS 64
#define LK017_RAM_BLOCK_SIZE 512
#define LK017_RAM_IO_SIZE 64
#define LK017_RAM_DEV_SIZE (LK017_RAM_BLOCKS * LK017_RAM_BLOCK_SIZE)
static UINT8 g_lk017RamDev[LK017_RAM_DEV_SIZE];
static volatile UINT32 g_lk017RamBase = 0; /* 首次访问捕获的设备基地址 */

static int Lk017RamRead(int partition, UINT32 *offset, void *buf, UINT32 size)
{
    (VOID)partition;
    if (g_lk017RamBase == 0) {
        g_lk017RamBase = *offset;
    }
    if ((*offset < g_lk017RamBase) || (*offset + size > g_lk017RamBase + sizeof(g_lk017RamDev))) {
        return -1;
    }
    (VOID)memcpy_s(buf, size, g_lk017RamDev + (*offset - g_lk017RamBase), size);
    return 0;
}

static int Lk017RamWrite(int partition, UINT32 *offset, const void *buf, UINT32 size)
{
    (VOID)partition;
    if (g_lk017RamBase == 0) {
        g_lk017RamBase = *offset;
    }
    if ((*offset < g_lk017RamBase) || (*offset + size > g_lk017RamBase + sizeof(g_lk017RamDev))) {
        return -1;
    }
    (VOID)memcpy_s(g_lk017RamDev + (*offset - g_lk017RamBase), size, buf, size);
    return 0;
}

static int Lk017RamErase(int partition, UINT32 offset, UINT32 size)
{
    (VOID)partition;
    if (g_lk017RamBase == 0) {
        g_lk017RamBase = offset;
    }
    if ((offset < g_lk017RamBase) || (offset + size > g_lk017RamBase + sizeof(g_lk017RamDev))) {
        return -1;
    }
    (VOID)memset_s(g_lk017RamDev + (offset - g_lk017RamBase), size, 0, size);
    return 0;
}

/* 用板级 virt_flash 回调填充重挂配置(同 fs_init.c 几何) */
static VOID Lk017FillVirtCfg(struct PartitionCfg *cfg)
{
    (VOID)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->partNo = FLASH_PARTITION_DATA0;
    cfg->blockSize = 4096;  /* 4096, lfs 块大小,同 fs_init.c */
    cfg->blockCount = 2048; /* 2048, lfs 块数,同 fs_init.c */
    cfg->readSize = 256;    /* 256, 同 fs_init.c */
    cfg->writeSize = 256;
    cfg->cacheSize = 256;
    cfg->lookaheadSize = 16; /* 16, 同 fs_init.c */
    cfg->blockCycles = 1000; /* 1000, 同 fs_init.c */
    cfg->readFunc = (int (*)(int, UINT32 *, void *, UINT32))virt_flash_read;
    cfg->writeFunc = (int (*)(int, UINT32 *, const void *, UINT32))virt_flash_write;
    cfg->eraseFunc = (int (*)(int, UINT32, UINT32))virt_flash_erase;
}
#endif /* LOSCFG_FS_LITTLEFS */

/* 用例简要描述: log 命令(CmdLog)直调全分支: argc 非法/level 合法与越界与非数字/
 * module 合法与越界/path 失败与 NULL/未知子命令, 覆盖 OsLkTraceLvSet/OsLkModuleLvSet/
 * OsLkLogFileSet/OsLkCurLogLvGet; qemu 上另含: 在 "/" 临时挂载 RAM littlefs 使
 * opendir("/") 成功(覆盖 OsLs 根路径枚举与 BAD file 分支,结束重挂恢复 /littlefs),
 * 之后才执行 path 打开成功分支(日志 fd 由 g_logger 设计性永久持有,此后不再 umount)。
 * 用例自包含: 全部全局副作用在本用例内部闭合,与其余用例无执行顺序依赖 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    const CHAR *argvLvOnly[1] = {"level"};
    const CHAR *argvLvOver[2] = {"level", "9"};   /* 越界(>DEBUG) */
    const CHAR *argvLvNan[2] = {"level", "abc"};  /* 非数字,尾指针非 0 */
    const CHAR *argvLvWarn[2] = {"level", "3"};   /* 合法 WARN */
    const CHAR *argvLvReset[2] = {"level", "2"};  /* 恢复默认 ERROR */
    const CHAR *argvMdOver[2] = {"module", "9"};  /* 越界(>MODULE4) */
    const CHAR *argvMdOk[2] = {"module", "2"};
    const CHAR *argvPath[2] = {"path", LK017_LOG_FILE};
    const CHAR *argvPathNull[2] = {"path", NULL};
    const CHAR *argvPathBad[2] = {"path", "/no_such_dir_017/f"};
    const CHAR *argvUnknown[2] = {"xyz", "1"};
#if (LK017_QEMU_ROOTFS == 1)
    struct PartitionCfg cfg;
    struct stat st = {0};
    INT32 fd = -1;
    INT32 rootMounted = 0;  /* "/" 根挂载是否活跃 */
    INT32 littlefsOff = 0;  /* /littlefs 是否处于卸载状态 */
    const CHAR *argvRoot[1] = {"/"};
#endif

    /* 前置: 确保 shell 命令表已初始化 */
    ret = (INT32)OsShellInit();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 参数个数非 2(Usage 提示) */
    ret = CmdLog(1, argvLvOnly);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: level 越界(打印当前级别后落到 return 0) */
    ret = CmdLog(2, argvLvOver);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: level 非数字(strtoul 尾指针非 0) */
    ret = CmdLog(2, argvLvNan);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: level 合法(OsLkTraceLvSet + OsLkCurLogLvGet 打印) */
    ret = CmdLog(2, argvLvWarn);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: module 越界 */
    ret = CmdLog(2, argvMdOver);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: module 合法(OsLkModuleLvSet) */
    ret = CmdLog(2, argvMdOk);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: path 参数为 NULL(OsLkLogFileSet 提前返回) */
    ret = CmdLog(2, argvPathNull);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: path 打开失败(父目录不存在) */
    ret = CmdLog(2, argvPathBad);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: 未知子命令(Usage 提示) */
    ret = CmdLog(2, argvUnknown);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

#if (LK017_QEMU_ROOTFS == 1)
    /* 前置: 创建探针文件用于重挂恢复验证(open 后即关闭,不占挂载点引用) */
    fd = open(LK017_PROBE_FILE, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;

    /* F-Pos: 在 "/" 临时挂载 RAM littlefs 后, ls "/" 走根路径枚举(OsLsGetFullpath
     * 的 path[1]=='\0' 分支)。g_partitionCfg 为全局单例,先卸载 /littlefs,
     * 结束后用板级 virt_flash 回调重挂恢复原文件系统(数据未动)。
     * 根挂载点仅匹配路径 "/" 本身, 无法在根下创建文件; 适配层 readdir 合成的
     * "."/".." 条目经根路径分支拼接为 "/."、"/.." 后 stat 失败, 走 BAD file 分支 */
    ret = umount("/littlefs");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    littlefsOff = 1;
    (VOID)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.blockSize = LK017_RAM_BLOCK_SIZE;
    cfg.blockCount = LK017_RAM_BLOCKS;
    cfg.readSize = LK017_RAM_IO_SIZE;
    cfg.writeSize = LK017_RAM_IO_SIZE;
    cfg.cacheSize = LK017_RAM_IO_SIZE;
    cfg.lookaheadSize = 8; /* 8: 前瞻缓冲字数 */
    cfg.blockCycles = 2;  /* 2: 低磨损阈值 */
    cfg.readFunc = Lk017RamRead;
    cfg.writeFunc = Lk017RamWrite;
    cfg.eraseFunc = Lk017RamErase;
    ret = mount(NULL, "/", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    rootMounted = 1;
    ret = OsShellCmdLs(1, argvRoot);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = umount("/");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    rootMounted = 0;
    /* 用板级 virt_flash 回调重挂 /littlefs, 恢复原文件系统 */
    Lk017FillVirtCfg(&cfg);
    ret = mount(NULL, "/littlefs", "littlefs", 0, &cfg);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    littlefsOff = 0;
    /* 原 /littlefs 数据恢复可见(探针文件仍在) */
    ret = stat(LK017_PROBE_FILE, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: path 指向可创建文件, 打开成功(OsLkLogFileSet 成功分支)。
     * 日志 fd 由 g_logger 设计性永久持有(无对外关闭接口,会占用挂载点引用),
     * 故本子步骤置于根挂载段之后执行,此后本用例不再 umount /littlefs */
    ret = CmdLog(2, argvPath);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
#endif

    /* 恢复默认日志级别(ERROR), 避免影响后续用例 */
    ret = CmdLog(2, argvLvReset);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

#ifdef LOSCFG_SHELL_DMESG
    /* F-Pos: 提升 dmesg 级别后经 LOS_LkPrint 打印越级 WARN 日志:
     * OsLkDefaultFunc 的 dmesg 环形记录分支 + OsLogCycleRecord */
    (VOID)LOS_DmesgLvSet(LK017_LEVEL_DEBUG);
    LOS_LkPrint(LK017_LEVEL_WARN, "ItLosShell017", 0, "lk to dmesg record test\r\n");
    (VOID)LOS_DmesgLvSet(LK017_LEVEL_ERROR);
#endif

EXIT:
    /* 异常路径同样恢复全局状态(尽力而为): 卸载残留根挂载, /littlefs 用板级
     * virt_flash 回调尽力重挂, 恢复日志级别, 清理探针与日志文件 */
#if (LK017_QEMU_ROOTFS == 1)
    if (rootMounted == 1) {
        (VOID)umount("/");
    }
    if (littlefsOff == 1) {
        Lk017FillVirtCfg(&cfg);
        (VOID)mount(NULL, "/littlefs", "littlefs", 0, &cfg);
    }
    if (fd >= 0) {
        (VOID)close(fd);
    }
    (VOID)unlink(LK017_PROBE_FILE);
    (VOID)unlink(LK017_LOG_FILE);
#endif
    (VOID)CmdLog(2, argvLvReset);
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell017
 * @tc.desc     验证 log 命令对外接口全分支: argc 非法/level 合法、越界、非数字/
 *              module 合法与越界/path 成功(qemu,日志 fd 设计性持有)与失败与 NULL/
 *              未知子命令; 覆盖 OsLkTraceLvSet/OsLkModuleLvSet/OsLkLogFileSet/
 *              OsLkCurLogLvGet; qemu 上另在 "/" 临时挂载 RAM littlefs 覆盖
 *              OsLs 根路径枚举与 BAD file 分支(结束重挂恢复); DMESG 板经
 *              LOS_DmesgLvSet+LOS_LkPrint 触发越级日志的 dmesg 环形记录。
 *              用例自包含,与其余用例无执行顺序依赖
 */
VOID ItLosShell017(VOID)
{
    TEST_ADD_CASE("ItLosShell017", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_SHELL_LK */
