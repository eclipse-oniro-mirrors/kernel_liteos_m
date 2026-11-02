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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

#ifndef _ICUNIT_CONFIG_H
#define _ICUNIT_CONFIG_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#if defined(__XTENSA_LX6__) || defined(__CSKY_V2__) // system ram is low, reduce memory usage
#define ICUNIT_CASE_SIZE 200
#else
#define ICUNIT_CASE_SIZE 1500
#endif
#define ICUNIT_SUIT_SIZE 200

/* 用例执行耗时统计：Kconfig LOSCFG_TEST_CASE_TIME_STAT 为唯一开关（testsuites/Kconfig，默认 y）。
 * 未走 Kconfig 的构建默认开启；显式 -DICUNIT_CASE_TIME_STAT=0 可强制关闭。 */
#ifndef ICUNIT_CASE_TIME_STAT
#ifdef LOSCFG_TEST_CASE_TIME_STAT
#define ICUNIT_CASE_TIME_STAT 1
#else
#define ICUNIT_CASE_TIME_STAT 0
#endif
#endif

/* 耗时 TOP 榜条数 */
#ifndef ICUNIT_CASE_TIME_TOP_N
#define ICUNIT_CASE_TIME_TOP_N 10
#endif

/* 慢用例阈值（微秒）：单用例执行超过该时长，用例执行完成后立即打印一条 [SlowCase] 耗时记录 */
#ifndef ICUNIT_CASE_TIME_SLOW_US
#define ICUNIT_CASE_TIME_SLOW_US 1000000
#endif


#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif
