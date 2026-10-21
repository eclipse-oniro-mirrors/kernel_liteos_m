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

#include <float.h>
#include <math.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

#define RET_OK 0
#define TEST_VALUE_X 0
#define TEST_VALUE_Y 1
#define TEST_EXPECTED 2

int DoubleEquals(double a, double b)
{
    if (a == INFINITY && b == INFINITY) {
        return 1;
    }
    if (a == -INFINITY && b == -INFINITY) {
        return 1;
    }
    if (a == INFINITY && b != INFINITY) {
        return 0;
    }
    if (a == -INFINITY && b != -INFINITY) {
        return 0;
    }
    if (isnan(a) && isnan(b)) {
        return 1;
    }
    if (isnan(a) || isnan(b)) {
        return 0;
    }

    return fabs(a - b) < DBL_EPSILON;
}

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixMathFuncTestSuite
 */
LITE_TEST_SUIT(Posix, Posixmath, PosixMathFuncTestSuite);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixMathFuncTestSuiteSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixMathFuncTestSuiteTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* *
 * @tc.number     SUB_KERNEL_MATH_ABS_001
 * @tc.name       test abs api
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathAbs001, Function | MediumTest | Level1)
{
    const int testCount = 3;
    int testValues[] = {-3, 0, 3};
    int expected[] = {3, 0, 3};
    int ret;
    for (int i = 0; i < testCount; ++i) {
        ret = abs(testValues[i]);
        LOG("\n [POSIXTEST][abs]abs(%d) = %d, expected is %d", testValues[i], ret, expected[i]);
        ICUNIT_ASSERT_EQUAL(ret, expected[i], ret);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_ABS_002
 * @tc.name       test abs api for boundary value
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathAbs002, Function | MediumTest | Level1)
{
    const int testCount = 3;
    int testValues[] = {-2147483648, -2147483647, 2147483647};
    int expected[] = {-2147483648, 2147483647, 2147483647};
    int ret;
    for (int i = 0; i < testCount; ++i) {
        ret = abs(testValues[i]);
        LOG("\n [POSIXTEST][abs]abs(%d) = %d, expected is %d", testValues[i], ret, expected[i]);
        ICUNIT_ASSERT_EQUAL(ret, expected[i], ret);
    }
    return 0;
};

#if !defined(LOSCFG_ARCH_FPU_DISABLE)
/* *
 * @tc.number     SUB_KERNEL_MATH_LOG_001
 * @tc.name       log basic function test
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathLog001, Function | MediumTest | Level1)
{
    const int testCount = 3;
    double testValues[] = { 0.5, 5.5, 1};
    double expected[] = { -0.69314718055994528623, 1.70474809223842527217, 0.00000000000000000000};
    double ret;
    dprintf("GZHTESTLOG PRINT_EMG: %lf, %lf, %lf", testValues[0], testValues[1], testValues[2]);
    LOG("GZHTESTLOG LOG: %lf, %lf, %lf", testValues[0], testValues[1], testValues[2]);
    for (int i = 0; i < testCount; ++i) {
        ret = log(testValues[i]);
        LOG("\n [POSIXTEST][log]log(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(expected[i], ret), TRUE, 0);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_LOG_002
 * @tc.name       log function test for invalid input
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathLog002, Function | MediumTest | Level1)
{
    const int testCount = 4;
    double testValues[] = { 0, -INFINITY, -2.0, NAN};
    double expected[] = { -INFINITY, NAN, NAN, NAN};
    double ret;

    LOG("\n (math_errhandling & MATH_ERRNO) = %d", (math_errhandling & MATH_ERRNO));
    LOG("\n (math_errhandling & MATH_ERREXCEPT) = %d", (math_errhandling & MATH_ERREXCEPT));

    for (int i = 0; i < testCount; ++i) {
        ret = log(testValues[i]);
        LOG("\n [POSIXTEST][log]log(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_SQRT_001
 * @tc.name       sqrt basic function test
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathSqrt001, Function | MediumTest | Level1)
{
    const int testCount = 3;
    double testValues[] = { 4, 3, 10 };
    double expected[] = { 2.00000000000000000000, 1.73205080756887719318, 3.16227766016837952279 };
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = sqrt(testValues[i]);
        LOG("\n [POSIXTEST][sqrt]sqrt(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(expected[i], ret), TRUE, 0);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_SQRT_002
 * @tc.name       sqrt function test for invalid input
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathSqrt002, Function | MediumTest | Level1)
{
    const int testCount = 5;
    double testValues[] = { 0, INFINITY, -2.0, -INFINITY, NAN };
    double expected[] = { 0.00000000000000000000, INFINITY, NAN, NAN, NAN};
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = sqrt(testValues[i]);
        LOG("\n [POSIXTEST][sqrt]sqrt(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_POW_001
 * @tc.name       pow basic function test
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathPow001, Function | MediumTest | Level1)
{
    double testValues[][TEST_EXPECTED + 1] = {
        {1, 1.12, 1.00000000000000000000},
        {2.8, 2.14, 9.0556199394902243682},
        {3.6, 3.16, 57.26851244563656706532},
        {678.88, 0, 1.00000000000000000000}
    };

    const int testCount = sizeof(testValues) / (sizeof(double) * 3);
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = pow(testValues[i][TEST_VALUE_X], testValues[i][TEST_VALUE_Y]);
        LOG("\n [POSIXTEST][pow]pow1(%lf,%lf) = %lf, expected is %lf", testValues[i][TEST_VALUE_X],
            testValues[i][TEST_VALUE_Y], ret, testValues[i][TEST_EXPECTED]);
        TEST_ASSERT_EQUAL_FLOAT(testValues[i][TEST_EXPECTED], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(testValues[i][TEST_EXPECTED], ret), TRUE, 0);
    }
    return 0;
};


/* *
 * @tc.number     SUB_KERNEL_MATH_POW_002
 * @tc.name       pow basic function test for range input
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathPow002, Function | MediumTest | Level1)
{
    double testValues[][TEST_EXPECTED + 1] = {
        {1, NAN, 1.00000000000000000000},
        {-1, -INFINITY, 1.00000000000000000000},
        {-0.5, -INFINITY, INFINITY},
        {3.5, -INFINITY, 0},
        {0.5, INFINITY, 0},
        {-3.5, INFINITY, INFINITY},
        {-INFINITY, -5, -0.00000000000000000000},
        {-INFINITY, -4.37, 0.00000000000000000000},
        {-INFINITY, 7, -INFINITY},
        {-INFINITY, 4, INFINITY},
        {INFINITY, -4, 0.00000000000000000000},
        {INFINITY, 4, INFINITY}
    };

    const int testCount = sizeof(testValues) / (sizeof(double) * 3);
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = pow(testValues[i][TEST_VALUE_X], testValues[i][TEST_VALUE_Y]);
        TEST_ASSERT_EQUAL_FLOAT(testValues[i][TEST_EXPECTED], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(testValues[i][TEST_EXPECTED], ret), TRUE, 0);
        LOG("\n [POSIXTEST][pow]pow1(%lf,%lf) = %lf, expected is %lf", testValues[i][TEST_VALUE_X],
            testValues[i][TEST_VALUE_Y], ret, testValues[i][TEST_EXPECTED]);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_POW_003
 * @tc.name       pow basic function test for invalid input
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathPow003, Function | MediumTest | Level1)
{
    double testValues[][TEST_EXPECTED + 1] = {
        {0.0, -7, HUGE_VAL},
        {-0.0, -6.22, HUGE_VAL},
        {-7.23, 3.57, NAN},
        {121223, 5674343, HUGE_VAL}
    };

    const int testCount = sizeof(testValues) / (sizeof(double) * 3);
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = pow(testValues[i][TEST_VALUE_X], testValues[i][TEST_VALUE_Y]);
        TEST_ASSERT_EQUAL_FLOAT(testValues[i][TEST_EXPECTED], ret);
        LOG("\n [POSIXTEST][pow]pow1(%lf,%lf) = %lf, expected is %lf", testValues[i][TEST_VALUE_X],
            testValues[i][TEST_VALUE_Y], ret, testValues[i][TEST_EXPECTED]);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(testValues[i][TEST_EXPECTED], ret), TRUE, 0);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_ROUND_001
 * @tc.name       round basic function test
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathRound001, Function | MediumTest | Level1)
{
    double testValues[] = { -0.5, 2.5, -3.812345679812345, -0.125, 0.125};
    double expected[] = { -1.00000000000000000000, 3.00000000000000000000, -4.00000000000000000000,
        0.00000000000000000000, 0.00000000000000000000};
    const int testCount = sizeof(testValues) / sizeof(double);
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = round(testValues[i]);
        LOG("\n [POSIXTEST][round]round1(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(expected[i], ret), TRUE, 0);
    }
    return 0;
};

/* *
 * @tc.number     SUB_KERNEL_MATH_ROUND_002
 * @tc.name       round basic function test
 * @tc.desc       [C- SOFTWARE -0100]
 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathRound002, Function | MediumTest | Level1)
{
    double testValues[] = { NAN, -INFINITY, INFINITY, 0};
    double expected[] = { NAN, -INFINITY, INFINITY, 0.00000000000000000000};
    const int testCount = sizeof(testValues) / sizeof(double);
    double ret;
    for (int i = 0; i < testCount; ++i) {
        ret = round(testValues[i]);
        LOG("\n [POSIXTEST][round]round1(%lf) = %lf, expected is %lf", testValues[i], ret, expected[i]);
        TEST_ASSERT_EQUAL_FLOAT(expected[i], ret);
        ICUNIT_ASSERT_EQUAL(DoubleEquals(expected[i], ret), TRUE, 0);
    }
    return 0;
};

/* 用例简要描述: pow 极端指数溢出下溢与次正规结果 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathPowExtreme001, Function | MediumTest | Level1)
{
    double ret;

    /* 注：本移植 math_errhandling==2（仅异常），__math_oflow/__math_uflow
     * 不写 errno（__math_xflow.c 无 errno 写入），故只断言值 */

    /* 指数超 1024：__math_oflow → +Inf（exp_inline 大参量分支） */
    ret = pow(2.0, 1100.0);
    ICUNIT_ASSERT_EQUAL(ret, INFINITY, 0);

    /* 深度下溢：__math_uflow → 0 */
    ret = pow(2.0, -2000.0);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    /* 次正规域结果：1.5^-1750 ≈ 6.9e-309 ∈ (0, DBL_MIN)，
     * 走 specialcase k<0 次正规侧（含双舍入保护） */
    ret = pow(1.5, -1750.0);
    ICUNIT_ASSERT_EQUAL(ret > 0.0, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret < DBL_MIN, 1, 0);

    /* 巨大 |y| 快速分支（pow.c:304-318） */
    ret = pow(2.0, 1e300);
    ICUNIT_ASSERT_EQUAL(ret, INFINITY, 0);

    ret = pow(2.0, -1e300);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    /* specialcase k>0 溢出侧（scale 指数补偿 128-133）：2^1023 可精确表示 */
    ret = pow(2.0, 1023.0);
    ICUNIT_ASSERT_EQUAL(ret, 8.98846567431158e307, 0);

    /* specialcase k<0 次正规底：2^-1074 为最小次正规数 */
    ret = pow(2.0, -1074.0);
    ICUNIT_ASSERT_EQUAL(ret, 4.9406564584124654e-324, 0);
    return 0;
};

/* 用例简要描述: pow 特殊底数指数组合的域规则 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathPowDomain001, Function | MediumTest | Level1)
{
    double ret;

    /* y 为 0/Inf 的域规则（pow.c:278-293） */
    ret = pow(1.0, INFINITY);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);

    ret = pow(-2.0, INFINITY);
    ICUNIT_ASSERT_EQUAL(ret, INFINITY, 0);

    ret = pow(0.5, INFINITY);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    ret = pow(NAN, 0.0);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);

    /* 负底整数/非整数指数（pow.c:294-302） */
    ret = pow(-2.0, 3.0);
    ICUNIT_ASSERT_EQUAL(ret, -8.0, 0);

    ret = pow(-2.0, -3.0);
    ICUNIT_ASSERT_EQUAL(ret, -0.125, 0);

    ret = pow(-2.0, 0.5);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0); /* NaN 自不等 */

    /* x 次正规分支（pow.c:320-324 规格化）：结果下溢为 0 */
    ret = pow(5e-324, 2.0);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);
    return 0;
};

/* 用例简要描述: fmod 零除无穷与次正规边界行为 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathFmodEdge001, Function | MediumTest | Level1)
{
    double ret;

    /* y 为 0/NaN 或 x 为 Inf：走 (x*y)/(x*y) NaN 传播（fmod.c:16-17 触发条件
     * uy==0/isnan(y)/x 为 Inf 三者，y 为 Inf 不在其列） */
    ret = fmod(1.0, 0.0);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0);

    ret = fmod(1.0, NAN);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0);

    ret = fmod(INFINITY, 2.0);
    ICUNIT_ASSERT_NOT_EQUAL(ret, ret, 0);

    /* y 为 Inf 且 |x|<|y|：非 NaN 路径, 原样返回 x（fmod.c:18-21）。
     * C 标准 fmod(x,±Inf)==x（x 有限）, 与下方 5e-324 同走 return x 分支 */
    ret = fmod(1.0, INFINITY);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);

    /* x==y 整除余 0 / |x|<|y| 返回 x（fmod.c:21-23） */
    ret = fmod(3.0, 3.0);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    ret = fmod(2.0, 5.0);
    ICUNIT_ASSERT_EQUAL(ret, 2.0, 0);

    /* x 为次正规且 |x|<|y|：原样返回（fmod.c:22） */
    ret = fmod(5e-324, 2.0);
    ICUNIT_ASSERT_EQUAL(ret, 5e-324, 0);

    /* y 为次正规：y 规格化分支（fmod.c:33-34），
     * 2 为 2^-1074 的整数倍 → 余 0 */
    ret = fmod(2.0, 5e-324);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);
    return 0;
};

/* 用例简要描述: log 接近 1 与次正规输入路径 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathLogEdge001, Function | MediumTest | Level1)
{
    double ret;

    /* x==1 特判精确返回 0（log.c:59-62 近 1 路径入口） */
    ret = log(1.0);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    /* 1 的下一个可表示数：log1p 式近 1 路径（log.c:63-76） */
    ret = log(1.0 + DBL_EPSILON);
    ICUNIT_ASSERT_EQUAL(ret > 0.0, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret < 1e-15, 1, 0);

    /* 常规近 1 值 */
    ret = log(1.5);
    ICUNIT_ASSERT_EQUAL(ret > 0.40546, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret < 0.40547, 1, 0);

    /* 次正规输入规格化（log.c:87-88）：log(2^-1074) = -1074*ln2 ≈ -744.44 */
    ret = log(4.9406564584124654e-324);
    ICUNIT_ASSERT_EQUAL(ret < -744.0, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret > -745.0, 1, 0);
    return 0;
};

/* 用例简要描述: sqrt 次正规输入规格化与舍入 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathSqrtSubnormal001, Function | MediumTest | Level1)
{
    double ret;

    /* 次正规输入规格化分支（sqrt.c:106-115）：sqrt(2^-1074)=2^-537 精确 */
    ret = sqrt(4.9406564584124654e-324);
    ICUNIT_ASSERT_EQUAL(ret, 0x1p-537, 0);

    /* 一般次正规：sqrt(1e-310) ≈ 1e-155 */
    ret = sqrt(1e-310);
    ICUNIT_ASSERT_EQUAL(ret > 0.999e-155, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret < 1.001e-155, 1, 0);
    return 0;
};

/* 用例简要描述: scalbn 指数搬运与 frexp 次正规分解 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathScalbnFrexp001, Function | MediumTest | Level1)
{
    double ret;
    int exp = 0;

    /* n>1023 多段放大（scalbn.c:10-16） */
    ret = scalbn(1.0, 3000);
    ICUNIT_ASSERT_EQUAL(ret, INFINITY, 0);

    /* n<-1022 次正规段：2^-1074 恰为最小次正规（scalbn.c:24-25） */
    ret = scalbn(1.0, -1074);
    ICUNIT_ASSERT_EQUAL(ret, 4.9406564584124654e-324, 0);

    /* 更深下溢经二次缩放舍入为 0 */
    ret = scalbn(2.0, -1080);
    ICUNIT_ASSERT_EQUAL(ret, 0.0, 0);

    /* 次正规输入分解：frexp(2^-1074) == 0.5 * 2^-1073（frexp.c:11-12） */
    ret = frexp(4.9406564584124654e-324, &exp);
    ICUNIT_ASSERT_EQUAL(ret, 0.5, 0);
    ICUNIT_ASSERT_EQUAL(exp, -1073, exp);

    /* 常规值回归：frexp(8) == 0.5*2^4 */
    ret = frexp(8.0, &exp);
    ICUNIT_ASSERT_EQUAL(ret, 0.5, 0);
    ICUNIT_ASSERT_EQUAL(exp, 4, exp);
    return 0;
};

/* 用例简要描述: round 半值远离零方向舍入 */
LITE_TEST_CASE(PosixMathFuncTestSuite, testMathRoundHalf001, Function | MediumTest | Level1)
{
    double ret;

    /* 半值远离 0（round.c:27 负向半值分支） */
    ret = round(-2.5);
    ICUNIT_ASSERT_EQUAL(ret, -3.0, 0);

    ret = round(2.5);
    ICUNIT_ASSERT_EQUAL(ret, 3.0, 0);

    ret = round(-0.5);
    ICUNIT_ASSERT_EQUAL(ret, -1.0, 0);

    ret = round(0.5);
    ICUNIT_ASSERT_EQUAL(ret, 1.0, 0);
    return 0;
};
#endif

RUN_TEST_SUITE(PosixMathFuncTestSuite);

void PosixMathFuncTest()
{
    LOG("begin PosixMathFuncTest....");
    RUN_ONE_TESTCASE(testMathAbs001);
    RUN_ONE_TESTCASE(testMathAbs002);
#if !defined(LOSCFG_ARCH_FPU_DISABLE)
    RUN_ONE_TESTCASE(testMathLog001);
    RUN_ONE_TESTCASE(testMathLog002);
    RUN_ONE_TESTCASE(testMathSqrt001);
    RUN_ONE_TESTCASE(testMathSqrt002);
    RUN_ONE_TESTCASE(testMathPow001);
    RUN_ONE_TESTCASE(testMathPow002);
    RUN_ONE_TESTCASE(testMathPow003);
    RUN_ONE_TESTCASE(testMathRound001);
    RUN_ONE_TESTCASE(testMathRound002);
    RUN_ONE_TESTCASE(testMathPowExtreme001);
    RUN_ONE_TESTCASE(testMathPowDomain001);
    RUN_ONE_TESTCASE(testMathFmodEdge001);
    RUN_ONE_TESTCASE(testMathLogEdge001);
    RUN_ONE_TESTCASE(testMathSqrtSubnormal001);
    RUN_ONE_TESTCASE(testMathScalbnFrexp001);
    RUN_ONE_TESTCASE(testMathRoundHalf001);
#endif

    return;
}