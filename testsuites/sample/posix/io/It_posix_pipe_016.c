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
 * 用例描述: 读长大于存量且写者阻塞时的跨段读取
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeRead  :527-543 读长(2048)>缓冲容量(1024): 读完存量后 tmpLen==0 →
 *                PIPE_RW_WAIT 排空陈旧令牌 + PipeWriterIsWaiting 探测(唤醒
 *                阻塞写者, num>=2 → TRUE) + pend readSem 等待写者补写 →
 *                while 循环跨段读完 2048
 *     PipeWrite :592-603 写者写 2048 填满阻塞, 被 probe post 唤醒后补写
 *
 * 断言口径(设计矩阵初版场景):
 *   - 写者任务(prio 20, 高于测试任务 25)写 2048: 填满 1024 环形缓冲后
 *     阻塞在 writeSem(确定性先阻塞)
 *   - 主测试任务 read 2048: 读出存量 1024 后经 probe 唤醒写者补写,
 *     最终返回 2048(读长全量), 内容与写入模式逐字节一致
 *   - 写者任务 write 正常返回 2048, 双方均无残留阻塞
 *
 * 根因更正说明(原"偏离说明"结论修正):
 *   初版本场景曾实测"确定性挂死 15min+"并归因于唤醒协议竞态。复核定位:
 *   挂死直接原因是用例自身的 4KB 栈上数组(2048+1024+1024)溢出测试任务栈
 *   (ARM 测试任务栈 OS_TSK_TEST_STACK_SIZE=0x1000, 恰 4KB), 栈溢出破坏
 *   相邻内存导致的挂死表象, 并非 pipe 协议死锁。缓冲改 static 后同场景
 *   复现验证: 单写者下 probe/风暴唤醒协议完整走通(num=2→TRUE→写者补写
 *   →读者消费令牌→read 返回 2048)。本用例以 static 缓冲恢复设计矩阵
 *   初版断言, 问题跟踪记录同步更正。
 */

/* 用例简要描述: 满管道阻塞写者+读2048 跨段全量读取 */
#include "It_posix_io.h"
#include <fcntl.h>
#include "los_task.h"

#define PIPE016_WR_LEN 2048 /* 2048: writer total, fills 1024 buffer then blocks */
#define PIPE016_WR_PRIO 20  /* 20: higher than test task(25), writer blocks first */
#define PIPE016_POLL_LIMIT 100 /* 100: poll ticks for writer exit */

static CHAR g_pipe016Wbuf[PIPE016_WR_LEN];
static CHAR g_pipe016Rbuf[PIPE016_WR_LEN];
static INT32 g_pipe016Fds[2] = {-1, -1}; /* 2: pipe fd pair */
static volatile INT32 g_pipe016WriterDone = 0;

static VOID Pipe016WriterTask(UINT32 arg)
{
    (VOID)arg;
    ssize_t ret = write(g_pipe016Fds[1], g_pipe016Wbuf, PIPE016_WR_LEN);
    ICUNIT_ASSERT_EQUAL_VOID(ret, (ssize_t)PIPE016_WR_LEN, ret);
    g_pipe016WriterDone = 1;
}

static UINT32 Testcase(VOID)
{
    UINT32 tid;
    UINT32 ret;
    UINT32 i;
    ssize_t nread;
    TSK_INIT_PARAM_S param = {0};

    ret = (UINT32)pipe(g_pipe016Fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    for (i = 0; i < PIPE016_WR_LEN; i++) {
        g_pipe016Wbuf[i] = (CHAR)(i % 254 + 1); /* 254: avoid '\0' */
    }

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)Pipe016WriterTask;
    param.usTaskPrio = PIPE016_WR_PRIO;
    param.pcName = "p016wr";
    param.uwStackSize = 0x800; /* 2KB: write path only, data buffers are static */
    param.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&tid, &param);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 确保写者已填满 1024 环形缓冲并阻塞在 writeSem(确定性先阻塞) */
    LOS_TaskDelay(PIPE016_POLL_LIMIT / 2); /* 50: ticks */

    /* 读 2048 > 存量 1024: 读完存量经 probe 唤醒写者补写, 跨段读全量 */
    nread = read(g_pipe016Fds[0], g_pipe016Rbuf, PIPE016_WR_LEN);
    ICUNIT_GOTO_EQUAL(nread, (ssize_t)PIPE016_WR_LEN, nread, EXIT);

    ret = memcmp(g_pipe016Rbuf, g_pipe016Wbuf, PIPE016_WR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    /* 写者应已被唤醒并完成 write 返回 */
    for (i = 0; i < PIPE016_POLL_LIMIT; i++) {
        if (g_pipe016WriterDone == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_pipe016WriterDone, 1, g_pipe016WriterDone);
    (VOID)close(g_pipe016Fds[0]);
    (VOID)close(g_pipe016Fds[1]);
    return LOS_OK;
}

VOID ItPosixPipe016(VOID)
{
    TEST_ADD_CASE("ItPosixPipe016", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
