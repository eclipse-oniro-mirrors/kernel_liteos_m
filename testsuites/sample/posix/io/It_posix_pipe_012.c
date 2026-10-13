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
 * 用例名称: ItPosixPipe012
 * 用例描述: 写满与环形回绕后 poll 事件语义
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipePoll :631  readIndex==writeIndex 且 roll=TRUE → nbytes=bufferSize →
 *                    写端无 POLLOUT(写满态)
 *     PipePoll :638  readIndex > writeIndex 环形跨界 → nbytes=bufferSize-readIndex+writeIndex
 *
 * 断言口径(环形缓冲 1024 字节, PIPE_DEV_BUF_SIZE):
 *   - 写满 1024 后: 写端 poll(POLLOUT) 无事件(0); 读端 poll(POLLIN) 返回 1 且含 POLLIN
 *   - 读 256 → 写 512 制造回绕(readIndex>writeIndex) 后: 读端 POLLIN/写端 POLLOUT 均有效,
 *     且剩余数据 768 字节与写入模式逐字节一致(回绕数据量计算正确)
 */

/* 用例简要描述: 写满与回绕后 poll 事件语义 */
#include "It_posix_io.h"
#include <poll.h>

#define PIPE012_BUF_SIZE 1024 /* 1024: PIPE_DEV_BUF_SIZE 环形缓冲容量 */

static UINT32 Testcase(VOID)
{
    INT32 fds[2] = {0}; /* 2: pipe fd pair */
    INT32 ret;
    struct pollfd pfd;
    CHAR pattern[PIPE012_BUF_SIZE];
    CHAR rbuf[PIPE012_BUF_SIZE];
    INT32 i;
    ssize_t nread;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    for (i = 0; i < PIPE012_BUF_SIZE; i++) {
        pattern[i] = (CHAR)(i % 254 + 1); /* 254: avoid '\0' pattern span */
    }

    /* --- 阶段 1: 写满 1024 → roll=TRUE, PipePoll :631 nbytes=bufferSize --- */
    ret = write(fds[1], pattern, PIPE012_BUF_SIZE);
    ICUNIT_GOTO_EQUAL(ret, PIPE012_BUF_SIZE, ret, EXIT);

    /* 写满后写端无 POLLOUT */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[1];
    pfd.events = POLLOUT;
    ret = poll(&pfd, 1, 0); /* 1: nfds, 0: no wait */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 读端有 POLLIN */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 0);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL((pfd.revents & POLLIN), POLLIN, (UINT32)pfd.revents, EXIT);

    /* --- 阶段 2: 读 256 → 写 128 制造回绕(writeIndex=128 < readIndex=256) --- */
    ret = read(fds[0], rbuf, 256); /* 256: consumed prefix */
    ICUNIT_GOTO_EQUAL(ret, 256, ret, EXIT);
    ret = memcmp(rbuf, pattern, 256);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 空闲仅 [0,256) 共 256 字节: 写 128(全量写入, 阻塞语义下不挂死) → writeIndex=128 */
    ret = write(fds[1], pattern + 512, 128); /* 128: wrap writer into [0,256) */
    ICUNIT_GOTO_EQUAL(ret, 128, ret, EXIT);

    /* 回绕态: PipePoll :638 nbytes = 1024 - 256 + 128 = 896 → 读端 POLLIN */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 0);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL((pfd.revents & POLLIN), POLLIN, (UINT32)pfd.revents, EXIT);

    /* 回绕态: 896 < 1023 → 写端 POLLOUT */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[1];
    pfd.events = POLLOUT;
    ret = poll(&pfd, 1, 0);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL((pfd.revents & POLLOUT), POLLOUT, (UINT32)pfd.revents, EXIT);

    /* 回绕数据量与内容一致: 剩余 = pattern[256..1023](768) + pattern[512..639](128) 共 896 字节 */
    nread = read(fds[0], rbuf, 896); /* 896: remaining bytes */
    ICUNIT_GOTO_EQUAL(nread, 896, nread, EXIT);
    ret = memcmp(rbuf, pattern + 256, 768);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = memcmp(rbuf + 768, pattern + 512, 128);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 读空后: 读端无事件 */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    close(fds[0]);
    close(fds[1]);
    return LOS_OK;
}

VOID ItPosixPipe012(VOID)
{
    TEST_ADD_CASE("ItPosixPipe012", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
