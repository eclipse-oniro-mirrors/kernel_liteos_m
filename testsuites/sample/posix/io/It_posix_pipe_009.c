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
 * 用例名称: ItPosixPipe009
 * 用例描述: 管道设备 open 错误分支(非法标志/不存在设备)
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeOpen :357-358  openFlag 非 O_RDONLY/O_WRONLY → EINVAL(标志校验先于设备查找)
 *     PipeOpen :363-364  PipeDevFind 失败(设备不存在) → ENODEV
 *
 * 断言口径:
 *   open("/dev/pipe999", O_RDWR)    == -1 且 errno==EINVAL
 *   open("/dev/pipe999", O_RDONLY)  == -1 且 errno==ENODEV
 * 说明: 设备编号 999 超出 PIPE_DEV_NUM(32), PipeDevFind 必然失败;
 *       O_RDWR 分支在路径查找之前被拒绝, 与真实设备名上的行为等价。
 */

/* 用例简要描述: 管道设备非法标志与不存在路径打开 */
#include "It_posix_io.h"
#include <fcntl.h>

static UINT32 Testcase(VOID)
{
    INT32 fd;

    /* 非法打开标志: O_RDWR → PipeOpen :357-358 EINVAL */
    errno = 0;
    fd = open("/dev/pipe999", O_RDWR);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 不存在的管道设备: O_RDONLY → PipeOpen :363-364 ENODEV */
    errno = 0;
    fd = open("/dev/pipe999", O_RDONLY);
    ICUNIT_ASSERT_EQUAL(fd, -1, fd);
    ICUNIT_ASSERT_EQUAL(errno, ENODEV, errno);

    return LOS_OK;
}

VOID ItPosixPipe009(VOID)
{
    TEST_ADD_CASE("ItPosixPipe009", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
