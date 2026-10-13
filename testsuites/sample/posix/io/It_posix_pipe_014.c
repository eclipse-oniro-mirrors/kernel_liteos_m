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
 * 用例名称: ItPosixPipe014
 * 用例描述: 管道创建资源耗尽与回收恢复
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeDevRegister :241,267-272  LOS_MuxCreate 失败 → -ENOSPC + ERROR 清理
 *     PipeDevRegister :246-256      LOS_SemCreate 失败 → 回滚删除 mux → -ENOSPC
 *     pipe :665-666                 PipeDevRegister<0 → errno=-ret(ENOSPC), return -1
 *
 * 断言口径(资源配比实测, 两板耗尽源不同但错误码一致):
 *   - qemu(ARM, MUX_LIMIT=24): mux 先于 sem(48) 耗尽, 约第 20~23 个 pipe() 失败
 *   - ws63(riscv, SEM_LIMIT=32): sem(每设备 2 个)先于 mux(60) 耗尽, 约第 16~17 个失败
 *   - 循环 pipe() 至 -1 且 errno==ENOSPC(两种耗尽路径均映射 ENOSPC)
 *   - 逐对 close 全部回收后, 再建 pipe() 成功(资源恢复)
 */

/* 用例简要描述: 管道创建互斥锁耗尽与回收恢复 */
#include "It_posix_io.h"

static UINT32 Testcase(VOID)
{
    INT32 fds[LOSCFG_BASE_IPC_MUX_LIMIT][2] = {0}; /* 2: pipe fd pair; 上限按 mux 配额 */
    INT32 ret;
    INT32 created = 0;
    INT32 i;

    /* 循环创建至资源耗尽(mux 或 sem, 均返回 ENOSPC) */
    for (i = 0; i < LOSCFG_BASE_IPC_MUX_LIMIT; i++) {
        ret = pipe(fds[i]);
        if (ret != 0) {
            break;
        }
        created++;
    }
    ICUNIT_GOTO_NOT_EQUAL(created, 0, created, EXIT);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOSPC, errno, EXIT);

    /* 逐对关闭全部管道, 释放 mux/sem/设备号 */
    for (i = 0; i < created; i++) {
        (VOID)close(fds[i][0]);
        (VOID)close(fds[i][1]);
    }
    created = 0;

    /* 资源回收后可再建 */
    INT32 newFds[2] = {-1, -1}; /* 2: pipe fd pair */
    ret = pipe(newFds);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)close(newFds[0]);
    (VOID)close(newFds[1]);

EXIT:
    for (i = 0; i < created; i++) {
        (VOID)close(fds[i][0]);
        (VOID)close(fds[i][1]);
    }
    return LOS_OK;
}

VOID ItPosixPipe014(VOID)
{
    TEST_ADD_CASE("ItPosixPipe014", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
