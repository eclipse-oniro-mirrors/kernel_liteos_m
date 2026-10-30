/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixPipe015
 * 用例描述: 管道 fd 槽位耗尽边界与清理路径
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeDevFdAlloc :312   fd 槽位分配(64 槽位, 全局跨设备共享, 单结构线性扫描)
 *     PipeOpen :366-371     fd 分配失败 → EBUSY
 *     pipe :668-676         fd0/fd1 打开失败清理路径
 *
 * 断言口径(设计矩阵):
 *   pipe() 占 2 个槽位后, 对同一管道设备连续 open 62 次 → 64 槽位全满
 *   (最低槽位顺序分配, fd 值两两不同);
 *   第 65 次分配(open)失败且 errno == EBUSY;
 *   池满状态下 pipe() 同样失败(注册临时设备后 fd0 打开失败, 走设备注销
 *   清理路径)且 errno == EBUSY;
 *   全部 close 后槽位完全回收, pipe() 可再建且数据通路可用。
 *
 * 修复说明(原"偏离说明"对应缺陷已修复):
 *   原实现 PIPE_DEV_FD_BITMAP_LEN=((64>>5)+1)=3 字=96 位与 g_devFd[64]
 *   不配平, 第 65 次分配写 g_devFd[64] 越界(依次覆盖 g_devFdBitmap/
 *   g_devNumBitmap/g_devStartFd/g_bhEvent 等相邻 .bss), "EBUSY 分支/
 *   pipe 清理路径"在越界发生前不可达, 故曾收敛为 40 次安全边界。
 *   pipe.c 已改为单结构分配(g_devFd[].dev==NULL 即空闲)并纳入
 *   g_devFdMutex 临界区, 本用例恢复设计矩阵的 65 次 EBUSY 边界断言。
 */

/* 用例简要描述: 管道 fd 槽位耗尽边界与清理路径 */
#include "It_posix_io.h"
#include <fcntl.h>

#define PIPE015_OPEN_NUM 62 /* 62: 连同 pipe() 的 2 个 fd 恰好占满 64 槽位 */

static UINT32 Testcase(VOID)
{
    INT32 pipeFds[2] = {-1, -1}; /* 2: pipe fd pair */
    INT32 openFds[PIPE015_OPEN_NUM];
    INT32 ret;
    INT32 i;
    INT32 j;
    CHAR buf[8] = "edge15"; /* 8: buffer size */
    CHAR rbuf[8];

    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        openFds[i] = -1;
    }

    /* 创建管道(干净套件状态下注册为 /dev/pipe0), 占用槽位 0/1 */
    ret = pipe(pipeFds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 池满前先验证数据通路 */
    ret = write(pipeFds[1], buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(buf), ret, EXIT);
    ret = read(pipeFds[0], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(rbuf), ret, EXIT);
    ret = memcmp(rbuf, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 对该设备重复 open 62 次: 连同 pipe() 的 2 个 fd 恰好占满 64 槽位 */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        openFds[i] = open("/dev/pipe0", O_RDONLY);
        ICUNIT_GOTO_NOT_EQUAL(openFds[i], -1, openFds[i], EXIT);
    }
    /* fd 值两两不同(槽位唯一, 最低空闲槽位顺序分配) */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        for (j = i + 1; j < PIPE015_OPEN_NUM; j++) {
            ICUNIT_GOTO_NOT_EQUAL(openFds[i], openFds[j], openFds[i], EXIT);
        }
    }

    /* 第 65 次分配: fd 槽位耗尽 → open 失败且 errno == EBUSY */
    errno = 0;
    ret = open("/dev/pipe0", O_RDONLY);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EBUSY, errno, EXIT);

    /* 池满时 pipe() 亦失败: PipeDevRegister 成功注册临时设备后 fd0 打开
     * 失败(EBUSY), 走 pipe() 打开失败清理路径(注销设备回收设备号) */
    errno = 0;
    INT32 fullFds[2] = {-1, -1}; /* 2: pipe fd pair */
    ret = pipe(fullFds);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EBUSY, errno, EXIT);

    /* 回收 62 个 open 句柄与管道两端(最后一笔 close 使 ref 归零并注销设备) */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        (VOID)close(openFds[i]);
        openFds[i] = -1;
    }
    (VOID)close(pipeFds[0]);
    pipeFds[0] = -1;
    (VOID)close(pipeFds[1]);
    pipeFds[1] = -1;

    /* 槽位完全回收后 pipe() 可再建且数据通路正常 */
    INT32 reFds[2] = {-1, -1}; /* 2: pipe fd pair */
    ret = pipe(reFds);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = write(reFds[1], buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(buf), ret, EXIT_RE);
    ret = read(reFds[0], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(rbuf), ret, EXIT_RE);
    ret = memcmp(rbuf, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_RE);
    (VOID)close(reFds[0]);
    (VOID)close(reFds[1]);
    return LOS_OK;
EXIT_RE:
    (VOID)close(reFds[0]);
    (VOID)close(reFds[1]);
EXIT:
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        if (openFds[i] != -1) {
            (VOID)close(openFds[i]);
        }
    }
    if (pipeFds[0] != -1) {
        (VOID)close(pipeFds[0]);
    }
    if (pipeFds[1] != -1) {
        (VOID)close(pipeFds[1]);
    }
    return LOS_OK;
}

VOID ItPosixPipe015(VOID)
{
    TEST_ADD_CASE("ItPosixPipe015", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
