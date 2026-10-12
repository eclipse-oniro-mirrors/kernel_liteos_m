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
 * 用例描述: 管道 fd 位图耗尽边界与清理路径
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeDevFdAlloc :318   fd 位图分配(64 槽位, 全局跨设备共享)
 *     PipeOpen :369-370     fd 分配失败 → EBUSY
 *     pipe :672-680         fd0/fd1 打开失败清理路径
 *
 * 断言口径(设计矩阵适配说明):
 *   对同一管道设备连续 open 40 次: fd 位图分配跨字(第 33 个起落在位图第 2 个
 *   32bit 字), 验证跨字分配正确性:
 *   - 40 次 open 全部成功且 fd 值两两不同(位图顺序分配, 含跨字边界)
 *   - 管道数据通路可用
 *   - 全部 close 后 pipe() 可再建(位图完全回收)
 *
 * 偏离说明(相对设计矩阵"第 65 个 open EBUSY"):
 *   实测源码 PIPE_DEV_FD_BITMAP_LEN=((64>>5)+1)=3 字=96 位, 而 g_devFd 数组仅
 *   PIPE_FD_NUM=64 项——位图分配到第 65 个 fd 时写 g_devFd[64] 越界
 *   (位图长度与数组长度不配平的疑似源码缺陷, 已登记问题跟踪),
 *   "EBUSY 分支/pipe 清理路径"在越界发生前不可达。为不触发内存越界,
 *   本用例收敛为 40 次跨字安全边界(远离 64 上限, 不受其它用例 fd 残留影响)。
 */

/* 用例简要描述: 管道 fd 位图耗尽边界与清理路径 */
#include "It_posix_io.h"
#include <fcntl.h>

#define PIPE015_OPEN_NUM 40 /* 40: 跨位图字(32bit)边界, 安全远离 64 数组上限 */

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

    /* 创建管道(干净套件状态下注册为 /dev/pipe0, 设备名编号取最低空闲位) */
    ret = pipe(pipeFds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 对该设备重复 open 40 次: 每次占用 1 个全局 fd 槽位(第 33 个起跨位图字) */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        openFds[i] = open("/dev/pipe0", O_RDONLY);
        ICUNIT_GOTO_NOT_EQUAL(openFds[i], -1, openFds[i], EXIT);
    }
    /* fd 值两两不同(位图顺序分配) */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        for (j = i + 1; j < PIPE015_OPEN_NUM; j++) {
            ICUNIT_GOTO_NOT_EQUAL(openFds[i], openFds[j], openFds[i], EXIT);
        }
    }

    /* 位图跨字分配状态下数据通路可用 */
    INT32 fullFds[2] = {-1, -1}; /* 2: pipe fd pair */
    ret = pipe(fullFds);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 数据通路验证 */
    ret = write(fullFds[1], buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(buf), ret, EXIT_FULL);
    ret = read(fullFds[0], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(rbuf), ret, EXIT_FULL);
    ret = memcmp(rbuf, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_FULL);

    (VOID)close(fullFds[0]);
    (VOID)close(fullFds[1]);

    /* 回收 62 个 open 句柄 */
    for (i = 0; i < PIPE015_OPEN_NUM; i++) {
        (VOID)close(openFds[i]);
        openFds[i] = -1;
    }

    /* 位图完全回收后 pipe() 可再建 */
    INT32 reFds[2] = {-1, -1}; /* 2: pipe fd pair */
    ret = pipe(reFds);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)close(reFds[0]);
    (VOID)close(reFds[1]);
    reFds[0] = -1;
    reFds[1] = -1;

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
EXIT_FULL:
    (VOID)close(fullFds[0]);
    (VOID)close(fullFds[1]);
    goto EXIT;
}

VOID ItPosixPipe015(VOID)
{
    TEST_ADD_CASE("ItPosixPipe015", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
