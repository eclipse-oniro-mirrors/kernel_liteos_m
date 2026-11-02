/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder, nor the name of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* locale 信息查询接口用例：nl_langinfo
 *
 * 实现依据（third_party/musl/porting/liteos_m/kernel/src/locale/langinfo.c:32-70）：
 * - CURRENT_LOCALE 为 __c_locale（internal/locale_impl.h:41，全类目 NULL），
 *   故 CODESET 走 loc->cat[LC_CTYPE]==NULL 分支返回 "ASCII"（:35）；
 * - _NL_LOCALE_NAME(idx==0xffff) 扩展项：cat 类目为 NULL 时返回 "C"（:39-40）；
 * - LC_NUMERIC：RADIXCHAR/THOUSEP 取 c_numeric = ".\0" ""（:42-43）；
 * - LC_MONETARY：CRNCYSTR(idx>0) 返回 ""（:50-51）；
 * - LC_MESSAGES：YESEXPR/NOEXPR/YESSTR/NOSTR 取 c_messages
 *   = "^[yY]" "^[nN]" "yes" "no"（:54-55，__lctrans 对 NULL 映射原样返回）；
 * - LC_TIME：ABDAY_1 等取 c_time 前缀（:46-48，已由 asctime_r 覆盖，此处回归）。
 */

#define _GNU_SOURCE /* YESSTR/NOSTR 声明需 _GNU_SOURCE（langinfo.h:82-88） */

#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixLocaleFuncTestSuite
 */
LITE_TEST_SUIT(Posix, PosixLocale, PosixLocaleFuncTestSuite);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixLocaleFuncTestSuiteSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixLocaleFuncTestSuiteTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* 用例简要描述: nl_langinfo 各类信息项返回值比对 */
LITE_TEST_CASE(PosixLocaleFuncTestSuite, testLocaleNlLanginfo001, Function | MediumTest | Level1)
{
    /* LC_NUMERIC 分支（langinfo.c:42-43） */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(RADIXCHAR), ".", 0);
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(THOUSEP), "", 0);

    /* LC_MESSAGES 分支（langinfo.c:54-55） */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(YESEXPR), "^[yY]", 0);
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(NOEXPR), "^[nN]", 0);
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(YESSTR), "yes", 0);
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(NOSTR), "no", 0);

    /* LC_MONETARY 分支（langinfo.c:50-51，CRNCYSTR idx=15>0 返回 ""） */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(CRNCYSTR), "", 0);

    /* _NL_LOCALE_NAME 扩展项（langinfo.c:39-40）：__c_locale 类目为 NULL → "C" */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(_NL_LOCALE_NAME(LC_CTYPE)), "C", 0);

    /* CODESET（langinfo.c:35）：CURRENT_LOCALE(__c_locale) 的 LC_CTYPE 类目为
     * NULL，按源码返回 "ASCII"（多字节宽窄转换仍由 CURRENT_UTF8 常量控制为 4） */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(CODESET), "ASCII", 0);

    /* LC_TIME 分支回归（langinfo.c:46-48） */
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(ABDAY_1), "Sun", 0);
    ICUNIT_ASSERT_STRING_EQUAL(nl_langinfo(AM_STR), "AM", 0);
    return 0;
}

RUN_TEST_SUITE(PosixLocaleFuncTestSuite);

void PosixLocaleFuncTest(void)
{
    LOG("begin PosixLocaleFuncTest....");
    RUN_ONE_TESTCASE(testLocaleNlLanginfo001);

    return;
}
