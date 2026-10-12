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
 * 用例名称: ItPosixPipe008
 * 用例描述: 管道 read/write 零长度参数校验
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeRead  :504-505  buf==NULL 或 len==0 → errno=EINVAL, return -1
 *     PipeWrite :566-567  buf==NULL 或 len==0 → errno=EINVAL, return -1
 *
 * 断言口径(按现实现, 与 POSIX "计数 0 返回 0" 惯例不同, 属实现语义差异登记项):
 *   read(fd, buf, 0)  == -1 且 errno==EINVAL
 *   write(fd, buf, 0) == -1 且 errno==EINVAL
 */

/* 用例简要描述: 管道读写零长度参数返回 EINVAL */
#include "It_posix_io.h"

static UINT32 Testcase(VOID)
{
    INT32 fds[2] = {0}; /* 2: pipe fd pair */
    CHAR buf[8] = "pipe008"; /* 8: buffer size */
    ssize_t ret;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 零长度读: PipeRead :504-505 参数校验分支 */
    errno = 0;
    ret = read(fds[0], buf, 0);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 零长度写: PipeWrite :566-567 参数校验分支 */
    errno = 0;
    ret = write(fds[1], buf, 0);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 参数拒绝后管道仍可用 */
    ret = write(fds[1], buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(buf), ret, EXIT);
    ret = read(fds[0], buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(buf), ret, EXIT);

EXIT:
    close(fds[0]);
    close(fds[1]);
    return LOS_OK;
}

VOID ItPosixPipe008(VOID)
{
    TEST_ADD_CASE("ItPosixPipe008", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
