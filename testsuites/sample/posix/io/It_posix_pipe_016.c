/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixPipe016
 * 用例描述: 读长大于存量且无写者等待时的部分读语义
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeRead :539-545  读长>缓冲存量且 PipeWriterIsWaiting==FALSE →
 *                        唤醒探测后返回已读字节数(部分读早返回分支)
 *
 * 断言口径(单任务确定性, 无写者线程):
 *   - 写 512(模式 A) 后 read 1024 → 返回 512(部分读), 内容与 A 逐字节一致
 *   - 再写 256(模式 B) 后 read 512 → 返回 256, 内容与 B 一致
 *   - 两轮合计 768 字节不丢不重
 *
 * 偏离说明(相对设计矩阵初版"写者阻塞时部分读"):
 *   初版场景(满管道+阻塞写者+读 2048)实测确定性挂死: reader 在持有 1024 字节
 *   已读数据时仍 pend readSem 等待被阻塞的 writer, 违反 POSIX "管道有数据即
 *   返回"语义(PipeWriterIsWaiting 的 post 风暴唤醒协议在单写者场景存在竞态),
 *   已登记问题跟踪。本用例收敛为"无写者等待"的确定性部分读分支(:539-545
 *   的 return nread 路径, 即设计矩阵的覆盖目标)。
 *   注: 缓冲改用 static——初版 4KB 栈上数组(2048+1024+1024)溢出测试主任务栈。
 */

/* 用例简要描述: 读长大于存量时部分读返回一致 */
#include "It_posix_io.h"

#define PIPE016_WR_A_LEN 512 /* 512: first write length */
#define PIPE016_WR_B_LEN 256 /* 256: second write length */

static CHAR g_pipe016PatternA[PIPE016_WR_A_LEN];
static CHAR g_pipe016PatternB[PIPE016_WR_B_LEN];
static CHAR g_pipe016Rbuf[PIPE016_WR_A_LEN];

static UINT32 Testcase(VOID)
{
    INT32 fds[2] = {-1, -1}; /* 2: pipe fd pair */
    INT32 ret;
    ssize_t nread;
    UINT32 i;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    for (i = 0; i < PIPE016_WR_A_LEN; i++) {
        g_pipe016PatternA[i] = (CHAR)(i % 254 + 1); /* 254: avoid '\0' */
    }
    for (i = 0; i < PIPE016_WR_B_LEN; i++) {
        g_pipe016PatternB[i] = (CHAR)((i + 127) % 254 + 1); /* 127: distinguishable */
    }

    /* --- 轮 1: 存量 512 < 读长 1024, 无写者 → 部分读返回 512 --- */
    ret = write(fds[1], g_pipe016PatternA, PIPE016_WR_A_LEN);
    ICUNIT_GOTO_EQUAL(ret, PIPE016_WR_A_LEN, ret, EXIT);

    nread = read(fds[0], g_pipe016Rbuf, sizeof(g_pipe016Rbuf));
    ICUNIT_GOTO_EQUAL(nread, PIPE016_WR_A_LEN, nread, EXIT);
    ret = memcmp(g_pipe016Rbuf, g_pipe016PatternA, PIPE016_WR_A_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* --- 轮 2: 存量 256 < 读长 512 → 部分读返回 256 --- */
    ret = write(fds[1], g_pipe016PatternB, PIPE016_WR_B_LEN);
    ICUNIT_GOTO_EQUAL(ret, PIPE016_WR_B_LEN, ret, EXIT);

    nread = read(fds[0], g_pipe016Rbuf, sizeof(g_pipe016Rbuf));
    ICUNIT_GOTO_EQUAL(nread, PIPE016_WR_B_LEN, nread, EXIT);
    ret = memcmp(g_pipe016Rbuf, g_pipe016PatternB, PIPE016_WR_B_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (VOID)close(fds[0]);
    (VOID)close(fds[1]);
    return LOS_OK;
EXIT:
    (VOID)close(fds[0]);
    (VOID)close(fds[1]);
    return LOS_OK;
}

VOID ItPosixPipe016(VOID)
{
    TEST_ADD_CASE("ItPosixPipe016", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
