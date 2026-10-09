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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef KERNEL_TEST_H
#define KERNEL_TEST_H

#define TESTCOUNT_NUM_1 1
#define TESTCOUNT_NUM_2 2
#define TESTCOUNT_NUM_3 3
#define TESTCOUNT_NUM_4 4
#define TESTCOUNT_NUM_5 5

#define DELAY_TICKS_1 1
#define DELAY_TICKS_5 5
#define DELAY_TICKS_10 10

#define TEST_TIME 10
#define THREAD_COUNT_MIN    3
#define THREAD_COUNT_MAX    30
#define THREAD_STACK_SPACE_MAX    4096

#define RUN_ONE_TESTCASE(caseName) ADD_TEST_CASE(caseName)
#define AUTO_RUN_ONE_TESTCASEFUNC(func) UnityDefaultTestRun(func, __FILE__, __LINE__)

/*
 * 按套件挂/卸 TEST_ROOT 卷的板级钩子（posix 域 RUN_ONE_TESTCASE 走 iCunit
 * 立即执行路径，不经过 hctest 的 lite_setup/lite_teardown，故由
 * PosixStdioFuncTest()/PosixFsFuncTest() 首尾直接调用）。
 * 弱默认实现（posix_test.c）恒返回 0：语义为"卷已由启动流程挂载"
 * （qemu 产品的 LfsLowLevelInit 路径）。需要按需后端的板提供强符号覆盖
 * （hi3322_testsuits 的 ram_lfs_backend.c：RAM 盘 + mount("/littlefs")）。
 * Ensure 返回非 0 时调用方跳过整个套件，避免用例对未挂载路径连锁失败。
 */
extern int TestFsVolumeEnsure(void);
extern int TestFsVolumeRelease(void);

#ifdef __cplusplus
#if __cplusplus
#endif
#endif /* __cplusplus */
#endif
