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

/* 数值串解析接口用例：strtod/strtoull/strtoul/sscanf
 *
 * 实现依据（third_party/musl/porting/liteos_m/kernel/src/）：
 * - stdlib/strtod.c：strtox 以 sh_fromstring 构造 string 伪 FILE 后调
 *   __floatscan(f, prec, 1)（pok=1），endptr 由 shcnt 回退位置计算；
 * - internal/floatscan.c：hexfloat（:314-423）解析 "0x" 十六进制浮点，
 *   e2 > -emin 时置 ERANGE 并返回 ±Inf（:381-384）；"inf"/"infinity"/"nan"/
 *   "nan(...)" 字面量（:459-488），i 为 1..2 的 "i.../n..." 前缀错配走
 *   EINVAL 返回 0 路径（:491-495）；decfloat（:62-305）覆盖 ".5"/"5."/
 *   指数回退/次正规与长尾舍入；
 * - internal/intscan.c：64 位累计第二循环（:77-80）与超限耗尽 ERANGE
 *   （:82-86）；:48 的 pok==0 路径仅由 vfscanf 的 %x 触达（"0x" 回退）；
 * - internal/shgetc.c：__shlim 限额分支（:14）与 __shgetc 限额耗尽 EOF
 *   分支（:23-27）由 sscanf 字段宽度（"%3d"）触达（string 伪 FILE 下
 *   shend=rpos+3）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"
#include "bug_isolate.h"

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixStdlibStrtodTest
 */
LITE_TEST_SUIT(Posix, PosixStdlib, PosixStdlibStrtodTest);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibStrtodTestSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibStrtodTestTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* 用例简要描述: strtod 十六进制浮点串解析与溢出回退 */
LITE_TEST_CASE(PosixStdlibStrtodTest, testStdlibStrtodHex001, Function | MediumTest | Level1)
{
    const char *src = "0x1p3";
    char *end = NULL;
    double ret;

    ret = strtod(src, &end);
    ICUNIT_ASSERT_EQUAL(ret, 8.0, 0);
    ICUNIT_ASSERT_EQUAL((int)(end - src), 5, (int)(end - src));

    ret = strtod("0x1.8p1", &end);
    ICUNIT_ASSERT_EQUAL(ret, 3.0, 0);

    ret = strtod("0x1p-3", &end);
    ICUNIT_ASSERT_EQUAL(ret, 0.125, 0);

    /* 2^1024 超出 double 上界：hexfloat 经 scalbnl 放大得到 HUGE_VAL */
    errno = 0;
    ret = strtod("0x1p1024", &end);
    ICUNIT_ASSERT_EQUAL(ret, HUGE_VAL, 0);

    /* e2 > -emin 显式溢出分支（floatscan.c:381-384）：ERANGE + ±Inf */
    errno = 0;
    ret = strtod("0x1p1103", &end);
    ICUNIT_ASSERT_EQUAL(ret, HUGE_VAL, 0);
    ICUNIT_ASSERT_EQUAL(errno, ERANGE, errno);

    /* "0x" 后无十六进制数字：pok 回退，值 0 且 endptr 停在 'x' */
    errno = 0;
    ret = strtod("0xg", &end);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);
    ICUNIT_ASSERT_EQUAL(*end, 'x', *end);
    return 0;
}

/* 用例简要描述: strtod inf/nan 字面量及错配前缀解析 */
LITE_TEST_CASE(PosixStdlibStrtodTest, testStdlibStrtodLiteral001, Function | MediumTest | Level1)
{
    const char *srcInfo = "info";
    const char *srcIn = "in";
    char *end = NULL;
    double ret;

    ret = strtod("inf", &end);
    ICUNIT_ASSERT_EQUAL(ret, HUGE_VAL, 0);

    ret = strtod("-infinity", &end);
    ICUNIT_ASSERT_EQUAL(ret, -HUGE_VAL, 0);

    ret = strtod("nan", &end);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0); /* NaN 自不等 */

    ret = strtod("nan(abc)", &end);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0);

    /* "info" 取最长有效前缀 "inf"：返回 Inf 且 endptr 停在 'o'
     * （floatscan.c:460-468 的 i==3 分支，按 C 标准前缀语义） */
    ret = strtod(srcInfo, &end);
    ICUNIT_ASSERT_EQUAL(ret, HUGE_VAL, 0);
    ICUNIT_ASSERT_EQUAL(*end, 'o', *end);

    /* 真错配前缀（i==2，既非 inf 也非 nan）：EINVAL 路径返回 0，
     * endptr 回退到串首（floatscan.c:491-495 + shlim(f,0)） */
    errno = 0;
    ret = strtod(srcIn, &end);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);
    ICUNIT_ASSERT_EQUAL(end == srcIn, 1, 0);
    return 0;
}

/* 用例简要描述: strtod 缺省整数小数形态与次正规舍入 */
LITE_TEST_CASE(PosixStdlibStrtodTest, testStdlibStrtodForm001, Function | MediumTest | Level1)
{
    const char *src = ".5";
    char *end = NULL;
    double ret;

    ret = strtod(src, &end);
    ICUNIT_ASSERT_EQUAL(ret, 0.5, 0);
    ICUNIT_ASSERT_EQUAL((int)(end - src), 2, (int)(end - src));

    ret = strtod("5.", &end);
    ICUNIT_ASSERT_EQUAL(ret, 5.0, 0);
    ICUNIT_ASSERT_EQUAL((int)(end - "5."), 2, (int)(end - "5."));

    /* 指数后无数字：scanexp 回退，值取 "1"，endptr 停在 'e' */
    ret = strtod("1e", &end);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);
    ICUNIT_ASSERT_EQUAL(*end, 'e', *end);

    ret = strtod("1e+", &end);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);
    ICUNIT_ASSERT_EQUAL(*end, 'e', *end);

    /* 次正规结果：1e-320 落在 (0, DBL_MIN)，decfloat 次正规路径 */
    ret = strtod("1e-320", &end);
    ICUNIT_ASSERT_EQUAL(ret > 0.0, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret < DBL_MIN, 1, 0);

    /* 长尾舍入：精度外数字不影响结果，仍为 1.0 */
    ret = strtod("1.00000000000000000000000012345", &end);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);

    /* 十进制溢出快速路径（decfloat lrp > -emin/2）：ERANGE + HUGE_VAL */
    errno = 0;
    ret = strtod("1e9999", &end);
    ICUNIT_ASSERT_EQUAL(ret, HUGE_VAL, 0);
    ICUNIT_ASSERT_EQUAL(errno, ERANGE, errno);
    return 0;
}

/* 用例简要描述: strtoull 64 位累计与上界溢出语义 */
LITE_TEST_CASE(PosixStdlibStrtodTest, testStdlibStrtoullRange001, Function | MediumTest | Level1)
{
    unsigned long long ret;

    /* ULLONG_MAX 恰好可表示：intscan 第二循环 64 位累计（:77-80），
     * y==lim 时不置 ERANGE（lim 为奇数走 (y^neg)-neg 原值返回） */
    errno = 0;
    ret = strtoull("18446744073709551615", NULL, 10);
    ICUNIT_ASSERT_EQUAL(ret, 18446744073709551615ULL, 0);
    ICUNIT_ASSERT_NOT_EQUAL(errno, ERANGE, errno);

    /* 超上界 1：耗尽余位后 ERANGE 且返回 ULLONG_MAX（:82-86） */
    errno = 0;
    ret = strtoull("18446744073709551616", NULL, 10);
    ICUNIT_ASSERT_EQUAL(ret, 18446744073709551615ULL, 0);
    ICUNIT_ASSERT_EQUAL(errno, ERANGE, errno);

    /* 20 位数同样走 ERANGE 耗尽分支 */
    errno = 0;
    ret = strtoull("99999999999999999999", NULL, 10);
    ICUNIT_ASSERT_EQUAL(ret, 18446744073709551615ULL, 0);
    ICUNIT_ASSERT_EQUAL(errno, ERANGE, errno);

    /* 4294967296 在 64 位累计下正常表示，无 ERANGE */
    errno = 0;
    ret = strtoull("4294967296", NULL, 10);
    ICUNIT_ASSERT_EQUAL(ret, 4294967296ULL, 0);
    ICUNIT_ASSERT_NOT_EQUAL(errno, ERANGE, errno);

#if (ULONG_MAX <= 0xffffffffUL)
    /* 32 位 strtoul 超上界：ERANGE 且返回 ULONG_MAX */
    unsigned long retUlong;
    errno = 0;
    retUlong = strtoul("4294967296", NULL, 10);
    ICUNIT_ASSERT_EQUAL(retUlong, 0xffffffffUL, 0);
    ICUNIT_ASSERT_EQUAL(errno, ERANGE, errno);
#endif
    return 0;
}

/* 用例简要描述: sscanf 宽度截断跳空白与十六进制回退 */
/* BUG_13 隔离: vsscanf 栈上伪 FILE{.lock=-1} 经移植版 __lockfile 直接
 * pthread_mutex_lock((pthread_mutex_t *)-1) 硬故障(qemu Type=4,
 * FaultAddr 0xfffffffc), 修复后置 1 恢复(LIBC_BUG_13_VSSCANF_LOCK)。
 * 同族 __shgetc 覆盖由 testStdioFscanfFile001(fopen 真实 FILE, 锁有效)承接。 */
#if defined(LIBC_BUG_13_VSSCANF_LOCK) && LIBC_BUG_13_VSSCANF_LOCK == 1
LITE_TEST_CASE(PosixStdlibStrtodTest, testStdlibSscanfInt001, Function | MediumTest | Level1)
{
    int value = 0;
    int ret;

    /* "%3d" 宽度截断：shlim 限额下第 4 个数字经 __shgetc 限额 EOF 分支止读 */
    ret = sscanf("12345", "%3d", &value);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(value, 123, value);

    /* 前导空白跳过：intscan 内 isspace 循环消耗空白 */
    ret = sscanf("  42", "%d", &value);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(value, 42, value);

    /* "%x" 输入 "0x"：intscan pok==0 路径（:48）回退返回 0 */
    ret = sscanf("0x", "%x", &value);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(value, 0, value);

    /* "0xg"：'g' 非十六进制数字，同样回退为 0 */
    ret = sscanf("0xg", "%x", &value);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(value, 0, value);

    /* 赋值抑制 "*"：跳过 123 读 456 */
    ret = sscanf("123 456", "%*d %d", &value);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(value, 456, value);
    return 0;
}
#endif /* LIBC_BUG_13_VSSCANF_LOCK */

RUN_TEST_SUITE(PosixStdlibStrtodTest);

void PosixStdlibStrtodFuncTest(void)
{
    LOG("begin PosixStdlibStrtodFuncTest....");
    RUN_ONE_TESTCASE(testStdlibStrtodHex001);
    RUN_ONE_TESTCASE(testStdlibStrtodLiteral001);
    RUN_ONE_TESTCASE(testStdlibStrtodForm001);
    RUN_ONE_TESTCASE(testStdlibStrtoullRange001);
#if defined(LIBC_BUG_13_VSSCANF_LOCK) && LIBC_BUG_13_VSSCANF_LOCK == 1
    RUN_ONE_TESTCASE(testStdlibSscanfInt001);
#endif

    return;
}
