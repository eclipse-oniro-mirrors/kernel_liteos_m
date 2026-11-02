/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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

/* random 状态管理接口用例：initstate/setstate
 *
 * 实现依据（third_party/musl/porting/liteos_m/kernel/src/prng/random.c）：
 * - initstate(seed, state, size)：size<8 返回 NULL（:75）；按 size 分档决定
 *   生成器阶数 n（<32→0、<64→7、<128→15、<256→31、>=256→63，:80-89）；
 *   返回指向进入前旧 state 缓冲的指针（:79 savestate 返回 x-1），供后续
 *   setstate 恢复；
 * - setstate(state)：保存并返回旧 state 指针（:102），装载新 state 的
 *   n/i/j 与数据区（:103 loadstate）；
 * - random()：n!=0 时为滞后斐波那契生成器（:117-122），n==0（size<32 档）
 * 退化为 31 位 LCG（:113-115）：x[0] = lcg31(x[0]) = (1103515245*x+12345)
 *   & 0x7fffffff，__srandom 在 n==0 时直接 x[0]=seed（:49-52），序列完全
 *   确定，可与手算常量比对；
 * - 同 seed 同 size 的两个 state 缓冲产生完全相同的序列（算法确定性），
 *   setstate 恢复后从该缓冲离开时的位置继续产出。
 *
 * 设计说明：state 缓冲使用文件级 static 存储。断言失败时 ICUNIT 宏会提前
 * 返回、跳过末尾的恢复动作，static 缓冲可保证内部状态指针 x 仍指向有效
 * 内存（栈缓冲则会悬垂），避免污染后续用例引发内存踩踏；正常路径末尾
 * setstate(saved) 恢复进入前状态。
 */

#include <stdlib.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

#define RANDOM_STATE_SIZE_128 128
#define RANDOM_STATE_SIZE_16  16
#define RANDOM_STATE_SIZE_TINY 4

/* 31 位 LCG 退化档（n==0）的期望常量，random.c:25-27 lcg31 手算：
 * lcg31(1) = (1103515245*1+12345) & 0x7fffffff = 1103527590
 * lcg31(1103527590) = 377401575 */
#define RANDOM_LCG31_SEED_1_STEP1 1103527590L
#define RANDOM_LCG31_SEED_1_STEP2 377401575L

/* state 缓冲取 static（见文件头设计说明），避免断言失败提前返回后悬垂 */
static char g_stateBufA[RANDOM_STATE_SIZE_128];
static char g_stateBufB[RANDOM_STATE_SIZE_128];
static char g_stateBufSmall[RANDOM_STATE_SIZE_16];
static char g_stateBufTiny[RANDOM_STATE_SIZE_TINY];

/* initstate 三档分档缓冲（random.c:83,85,89）：
 * size=64 → n=15（64<=size<128）、size=256 → n=63（size>=256）、
 * size=40 → n=7（32<=size<64） */
static char g_tierBuf64A[64];
static char g_tierBuf64B[64];
static char g_tierBuf256A[256];
static char g_tierBuf256B[256];
static char g_tierBuf40A[40];
static char g_tierBuf40B[40];

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixStdlibRandomStateTest
 */
LITE_TEST_SUIT(Posix, PosixStdlib, PosixStdlibRandomStateTest);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibRandomStateTestSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibRandomStateTestTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* *
 * @tc.number    : TEST_STDLIB_INITSTATE_001
 * @tc.name      : initstate returns the old state pointer and same seed yields identical sequences
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 random.c:71-95 的 initstate 主路径与 :75 的 size<8 返回 NULL 分支；
 * 返回值应为进入前旧 state 指针（非 NULL）；同 seed 同 size 的两个缓冲
 * （size=128，n=31 档）产生的 random() 序列逐个一致（算法确定性）；
 * random() 输出落在 [0, 0x7fffffff]（31 位截断，:118）。
 */
LITE_TEST_CASE(PosixStdlibRandomStateTest, testStdlibInitstate001, Function | MediumTest | Level1)
{
    char *saved = NULL;
    char *old = NULL;
    long a1, a2, a3;
    long b1, b2, b3;

    /* size<8：直接拒绝返回 NULL */
    old = initstate(1, g_stateBufTiny, sizeof(g_stateBufTiny));
    ICUNIT_ASSERT_EQUAL(old, NULL, 0);

    saved = initstate(100, g_stateBufA, sizeof(g_stateBufA));
    ICUNIT_ASSERT_NOT_EQUAL(saved, NULL, 0);

    a1 = random();
    a2 = random();
    a3 = random();
    ICUNIT_ASSERT_WITHIN_EQUAL(a1, 0, 0x7fffffff, (int)a1);
    ICUNIT_ASSERT_WITHIN_EQUAL(a2, 0, 0x7fffffff, (int)a2);
    ICUNIT_ASSERT_WITHIN_EQUAL(a3, 0, 0x7fffffff, (int)a3);

    /* 同 seed 同 size：序列应与 g_stateBufA 完全一致 */
    old = initstate(100, g_stateBufB, sizeof(g_stateBufB));
    ICUNIT_ASSERT_NOT_EQUAL(old, NULL, 0);

    b1 = random();
    b2 = random();
    b3 = random();
    ICUNIT_ASSERT_EQUAL(b1, a1, (int)b1);
    ICUNIT_ASSERT_EQUAL(b2, a2, (int)b2);
    ICUNIT_ASSERT_EQUAL(b3, a3, (int)b3);

    /* 恢复进入前状态，避免影响其他用例 */
    (void)setstate(saved);
    return 0;
}

/* *
 * @tc.number    : TEST_STDLIB_SETSTATE_001
 * @tc.name      : setstate returns the previous state buffer and resumes the restored sequence
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 random.c:97-106 的 setstate 主路径：返回切走前的当前 state 指针
 * （:102 savestate 返回 x-1，与切走前的缓冲为同一地址）；setstate(g_stateBufA)
 * 恢复后 random() 从该缓冲离开处继续，产出等于同 seed 的 g_stateBufB 路径
 * 的第 3 个值（两路序列一致、恢复点正确）。
 */
LITE_TEST_CASE(PosixStdlibRandomStateTest, testStdlibSetstate001, Function | MediumTest | Level1)
{
    char *saved = NULL;
    char *cur = NULL;
    long a1, a2, b1, b2, r3, s3;

    saved = initstate(200, g_stateBufA, sizeof(g_stateBufA));
    ICUNIT_ASSERT_NOT_EQUAL(saved, NULL, 0);
    a1 = random();
    a2 = random();

    /* 同 seed 切到 g_stateBufB：initstate 返回的旧 state 应为 g_stateBufA，
     * 且前两个输出与 g_stateBufA 路径一致 */
    cur = initstate(200, g_stateBufB, sizeof(g_stateBufB));
    ICUNIT_ASSERT_EQUAL(cur, g_stateBufA, 0);
    b1 = random();
    b2 = random();
    ICUNIT_ASSERT_EQUAL(b1, a1, (int)(b1 - a1));
    ICUNIT_ASSERT_EQUAL(b2, a2, (int)(b2 - a2));

    /* 恢复 g_stateBufA：setstate 返回切走前的当前 state（g_stateBufB） */
    cur = setstate(g_stateBufA);
    ICUNIT_ASSERT_EQUAL(cur, g_stateBufB, 0);

    /* 恢复点续跑：A 路径第 3 值应与 B 路径第 3 值一致 */
    r3 = random();
    (void)setstate(g_stateBufB);
    s3 = random();
    ICUNIT_ASSERT_EQUAL(r3, s3, (int)(r3 - s3));

    /* 恢复进入前状态 */
    (void)setstate(saved);
    return 0;
}

/* *
 * @tc.number    : TEST_STDLIB_INITSTATE_002
 * @tc.name      : initstate with size<32 degenerates to deterministic 31-bit LCG
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 random.c:80-81 的 size<32→n=0 档与 :113-115 的 n==0 退化路径：
 * __srandom 直接 x[0]=seed（:49-52），random() 即 lcg31(x[0])。seed=1
 * 的前两个输出为手算常量（公式见 RANDOM_LCG31_* 定义处注释），序列确定
 * 可精确比对；此为 musl 实现特有行为锁定，若随机算法变更该用例应同步更新。
 */
LITE_TEST_CASE(PosixStdlibRandomStateTest, testStdlibInitstateDegenerate001, Function | MediumTest | Level1)
{
    char *saved = NULL;
    long r1, r2;
    char *retNull = NULL;

    saved = initstate(1, g_stateBufSmall, sizeof(g_stateBufSmall));
    ICUNIT_ASSERT_NOT_EQUAL(saved, NULL, 0);

    r1 = random();
    ICUNIT_ASSERT_EQUAL(r1, RANDOM_LCG31_SEED_1_STEP1, (int)r1);

    r2 = random();
    ICUNIT_ASSERT_EQUAL(r2, RANDOM_LCG31_SEED_1_STEP2, (int)r2);

    /* size<8 拒绝分支在退化档边界复核（size=4 < 8） */
    retNull = initstate(1, g_stateBufTiny, sizeof(g_stateBufTiny));
    ICUNIT_ASSERT_EQUAL(retNull, NULL, 0);

    /* 恢复进入前状态 */
    (void)setstate(saved);
    return 0;
}

/* 用例简要描述: initstate 状态缓冲三档分位与序列复现 */
LITE_TEST_CASE(PosixStdlibRandomStateTest, testStdlibInitstateTiers001, Function | MediumTest | Level1)
{
    char *saved = NULL;
    char *old = NULL;
    long a1, a2, a3;
    long b1, b2, b3;

    /* size=64 档（random.c:85，n=15）：返回旧 state 非 NULL，同 seed 同 size 序列一致 */
    saved = initstate(7, g_tierBuf64A, sizeof(g_tierBuf64A));
    ICUNIT_ASSERT_NOT_EQUAL(saved, NULL, 0);
    a1 = random();
    a2 = random();
    a3 = random();
    ICUNIT_ASSERT_WITHIN_EQUAL(a1, 0, 0x7fffffff, (int)a1);
    ICUNIT_ASSERT_WITHIN_EQUAL(a2, 0, 0x7fffffff, (int)a2);
    ICUNIT_ASSERT_WITHIN_EQUAL(a3, 0, 0x7fffffff, (int)a3);
    old = initstate(7, g_tierBuf64B, sizeof(g_tierBuf64B));
    ICUNIT_ASSERT_EQUAL(old, g_tierBuf64A, 0);
    b1 = random();
    b2 = random();
    b3 = random();
    ICUNIT_ASSERT_EQUAL(b1, a1, (int)b1);
    ICUNIT_ASSERT_EQUAL(b2, a2, (int)b2);
    ICUNIT_ASSERT_EQUAL(b3, a3, (int)b3);

    /* size=256 档（random.c:89，n=63） */
    (void)initstate(9, g_tierBuf256A, sizeof(g_tierBuf256A));
    a1 = random();
    a2 = random();
    ICUNIT_ASSERT_WITHIN_EQUAL(a1, 0, 0x7fffffff, (int)a1);
    ICUNIT_ASSERT_WITHIN_EQUAL(a2, 0, 0x7fffffff, (int)a2);
    (void)initstate(9, g_tierBuf256B, sizeof(g_tierBuf256B));
    b1 = random();
    b2 = random();
    ICUNIT_ASSERT_EQUAL(b1, a1, (int)b1);
    ICUNIT_ASSERT_EQUAL(b2, a2, (int)b2);

    /* size=40 档（random.c:83，n=7） */
    (void)initstate(11, g_tierBuf40A, sizeof(g_tierBuf40A));
    a1 = random();
    a2 = random();
    ICUNIT_ASSERT_WITHIN_EQUAL(a1, 0, 0x7fffffff, (int)a1);
    ICUNIT_ASSERT_WITHIN_EQUAL(a2, 0, 0x7fffffff, (int)a2);
    old = initstate(11, g_tierBuf40B, sizeof(g_tierBuf40B));
    ICUNIT_ASSERT_NOT_EQUAL(old, NULL, 0);
    b1 = random();
    b2 = random();
    ICUNIT_ASSERT_EQUAL(b1, a1, (int)b1);
    ICUNIT_ASSERT_EQUAL(b2, a2, (int)b2);

    /* 恢复进入前状态，避免影响其他用例 */
    (void)setstate(saved);
    return 0;
}

/* 用例简要描述: srand/srandom 播种序列确定可复现 */
LITE_TEST_CASE(PosixStdlibRandomStateTest, testStdlibSrand001, Function | MediumTest | Level1)
{
    static const int loopCount = 8;
    int seqA[8] = {0};
    int seqB[8] = {0};
    long randA[3] = {0};
    long randB[3] = {0};
    int i;

    /* srand 转发 srandom（rand.c:4-6），rand 转发 random：
     * 双次重播种序列逐个一致（补 ws63 srand/srandom 未覆盖） */
    srand(42);
    for (i = 0; i < loopCount; i++) {
        seqA[i] = rand();
        ICUNIT_ASSERT_WITHIN_EQUAL(seqA[i], 0, RAND_MAX, seqA[i]);
    }
    srand(42);
    for (i = 0; i < loopCount; i++) {
        seqB[i] = rand();
        ICUNIT_ASSERT_EQUAL(seqB[i], seqA[i], seqB[i]);
    }

    /* srandom/random 双次重播种同样确定可复现（random.c:63-69） */
    srandom(7);
    randA[0] = random();
    randA[1] = random();
    randA[2] = random();
    ICUNIT_ASSERT_WITHIN_EQUAL(randA[0], 0, 0x7fffffff, (int)randA[0]);
    ICUNIT_ASSERT_WITHIN_EQUAL(randA[1], 0, 0x7fffffff, (int)randA[1]);
    ICUNIT_ASSERT_WITHIN_EQUAL(randA[2], 0, 0x7fffffff, (int)randA[2]);
    srandom(7);
    randB[0] = random();
    randB[1] = random();
    randB[2] = random();
    ICUNIT_ASSERT_EQUAL(randB[0], randA[0], (int)randB[0]);
    ICUNIT_ASSERT_EQUAL(randB[1], randA[1], (int)randB[1]);
    ICUNIT_ASSERT_EQUAL(randB[2], randA[2], (int)randB[2]);
    return 0;
}

RUN_TEST_SUITE(PosixStdlibRandomStateTest);

void PosixStdlibRandomStateFuncTest(void)
{
    LOG("begin PosixStdlibRandomStateFuncTest....");
    RUN_ONE_TESTCASE(testStdlibInitstate001);
    RUN_ONE_TESTCASE(testStdlibSetstate001);
    RUN_ONE_TESTCASE(testStdlibInitstateDegenerate001);
    RUN_ONE_TESTCASE(testStdlibInitstateTiers001);
    RUN_ONE_TESTCASE(testStdlibSrand001);

    return;
}
