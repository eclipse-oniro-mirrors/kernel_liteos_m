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

/* 多字节转换接口用例：mbtowc/wctomb/wcrtomb
 *
 * 实现依据（third_party/musl/porting/liteos_m/kernel/src/multibyte/）：
 * - liteos_m 内核态固定 UTF-8 编码：locale/c_locale.c:13-15 将
 *   __c_dot_utf8_locale.cat[LC_CTYPE] 初始化为非空，internal/locale_impl.h:43-46
 *   由此得 CURRENT_UTF8==1、MB_CUR_MAX==4（编译期常量），mbtowc/wcrtomb 均走
 *   UTF-8 分支（MB_CUR_MAX==1 的单字节码元分支为不可达代码）；
 * - mbtowc.c：src 为 NULL 返回 0（:12）、n 为 0/序列非法/长度不足返回 -1 并置
 *   EILSEQ（:13,:18,:24,:26,:33,:40 的 ilseq 路径）、首字节 <0xC2 或 >0xF4
 *   拒绝（:18，SA/SB 界定）、续字节经 OOB 界定检查（:26,:33,:40）；
 * - wctomb.c：s 为 NULL 返回 0（:6），其余透传 wcrtomb(s, wc, 0)；
 * - wcrtomb.c：s 为 NULL 返回 1 表示处于初始移位态（:8）、按码点区间输出
 *   1/2/3/4 字节（:9-:33）、代理区与超出 0x10FFFF 的码点返回 (size_t)-1 并置
 *   EILSEQ（:35）；UTF-8 无状态，st 参数不被使用（wctomb 传 0 亦合法）。
 */

#include <stdlib.h>
#include <wchar.h>
#include <errno.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixStdlibMultibyteTest
 */
LITE_TEST_SUIT(Posix, PosixStdlib, PosixStdlibMultibyteTest);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibMultibyteTestSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdlibMultibyteTestTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* *
 * @tc.number    : TEST_STDLIB_MBTOWC_001
 * @tc.name      : mbtowc basic contract on ASCII, NUL terminator and NULL arguments
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 mbtowc.c:16 的 ASCII 单字节分支（返回 1 且出参为该字符；'\0' 时
 * 经 !! 归一返回 0）、:12 的 src==NULL 返回 0 分支、:14 的 wc==NULL 仍完成
 * 解析并返回长度的分支。
 */
LITE_TEST_CASE(PosixStdlibMultibyteTest, testStdlibMbtowc001, Function | MediumTest | Level1)
{
    wchar_t wc = 0;
    int ret;

    ret = mbtowc(&wc, "A", 1);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL((int)wc, 'A', (int)wc);

    /* 终止符：*s < 0x80 分支中 !!(*wc = '\0') 归一为 0 */
    ret = mbtowc(&wc, "\0", 1);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* src 为 NULL：无状态编码返回 0（状态重置语义） */
    ret = mbtowc(&wc, NULL, 4);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* wc 出参为 NULL：仍解析首字符并返回其长度 */
    ret = mbtowc(NULL, "AB", 2);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    return 0;
}

/* *
 * @tc.number    : TEST_STDLIB_MBTOWC_002
 * @tc.name      : mbtowc decodes UTF-8 2/3/4-byte sequences and rejects malformed input with EILSEQ
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 mbtowc.c 的 UTF-8 多字节路径：U+00E9（0xC3 0xA9，:27-31 双字节
 * 返回 2）、U+20AC（0xE2 0x82 0xAC，:34-38 三字节返回 3）、U+1F600
 * （0xF0 0x9F 0x98 0x80，:40-42 四字节返回 4）；负路径覆盖 :24 的 n 不足
 * 检查、:18 的首字节越界（0x80<SA、0xC0 过长编码、>SB=0xF4）、:26 的
 * OOB 非法续字节，均返回 -1 并置 EILSEQ。
 */
LITE_TEST_CASE(PosixStdlibMultibyteTest, testStdlibMbtowcUtf8001, Function | MediumTest | Level1)
{
    wchar_t wc = 0;
    int ret;

    /* 双字节 U+00E9 (LATIN SMALL LETTER E WITH ACUTE) */
    ret = mbtowc(&wc, "\xC3\xA9", 2);
    ICUNIT_ASSERT_EQUAL(ret, 2, ret);
    ICUNIT_ASSERT_EQUAL((int)wc, 0xE9, (int)wc);

    /* 三字节 U+20AC (EURO SIGN) */
    ret = mbtowc(&wc, "\xE2\x82\xAC", 3);
    ICUNIT_ASSERT_EQUAL(ret, 3, ret);
    ICUNIT_ASSERT_EQUAL((int)wc, 0x20AC, (int)wc);

    /* 四字节 U+1F600 (GRINNING FACE) */
    ret = mbtowc(&wc, "\xF0\x9F\x98\x80", 4);
    ICUNIT_ASSERT_EQUAL(ret, 4, ret);
    ICUNIT_ASSERT_EQUAL((int)wc, 0x1F600, (int)wc);

    /* n 不足以容纳完整序列 */
    errno = 0;
    ret = mbtowc(&wc, "\xC3\xA9", 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);

    /* 非法首字节：0x80 落在 SA(0xC2) 之下 */
    errno = 0;
    ret = mbtowc(&wc, "\x80", 1);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);

    /* 过长编码 0xC0 0x80（'NUL' 的两字节形式）被拒绝 */
    errno = 0;
    ret = mbtowc(&wc, "\xC0\x80", 2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);

    /* 首字节超过 SB(0xF4) */
    errno = 0;
    ret = mbtowc(&wc, "\xF5\x80\x80\x80", 4);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);

    /* 续字节非法：0xC3 后跟 'A'(0x41) 非 0x80-0xBF 续字节范围 */
    errno = 0;
    ret = mbtowc(&wc, "\xC3\x41", 2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);
    return 0;
}

/* *
 * @tc.number    : TEST_STDLIB_WCTOMB_001
 * @tc.name      : wctomb encodes wchar to 1/2/3/4-byte UTF-8 and returns 0 for NULL buffer
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 wctomb.c:6 的 s==NULL 返回 0 分支；经 wcrtomb 的码点区间编码分支：
 * 'A' 单字节（wcrtomb.c:9-11）、U+00E9 双字节（:19-22）、U+20AC 三字节
 * （:23-27）、U+1F600 四字节（:28-33），逐一比对编码字节。
 */
LITE_TEST_CASE(PosixStdlibMultibyteTest, testStdlibWctomb001, Function | MediumTest | Level1)
{
    char buf[8];
    int ret;

    /* s 为 NULL：无状态编码返回 0 */
    ret = wctomb(NULL, 'A');
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = wctomb(buf, 'A');
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(buf[0], 'A', buf[0]);

    ret = wctomb(buf, 0xE9);
    ICUNIT_ASSERT_EQUAL(ret, 2, ret);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[0], 0xC3, buf[0]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[1], 0xA9, buf[1]);

    ret = wctomb(buf, 0x20AC);
    ICUNIT_ASSERT_EQUAL(ret, 3, ret);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[0], 0xE2, buf[0]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[1], 0x82, buf[1]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[2], 0xAC, buf[2]);

    ret = wctomb(buf, 0x1F600);
    ICUNIT_ASSERT_EQUAL(ret, 4, ret);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[0], 0xF0, buf[0]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[1], 0x9F, buf[1]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[2], 0x98, buf[2]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[3], 0x80, buf[3]);
    return 0;
}

/* *
 * @tc.number    : TEST_STDLIB_WCRTOMB_001
 * @tc.name      : wcrtomb initial-state/mbstate semantics and surrogate/range rejection
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 wcrtomb.c:8 的 s==NULL 返回 1（处于初始移位态）分支、:9-11 的
 * ASCII 单字节分支；st 传有效 mbstate_t 与 NULL 等价（UTF-8 无状态）；
 * 代理区码点 0xD800 与越界码点 0x110000 落入 :35 的 EILSEQ 返回
 * (size_t)-1 分支；并验证 wcrtomb 编码后 mbtowc 可解码回同一码点（往返）。
 */
LITE_TEST_CASE(PosixStdlibMultibyteTest, testStdlibWcrtomb001, Function | MediumTest | Level1)
{
    char buf[8];
    mbstate_t st;
    wchar_t wc = 0;
    size_t ret;

    (void)memset(&st, 0, sizeof(st));

    /* s 为 NULL：返回 1 表示处于初始移位态，不写出任何字节 */
    ret = wcrtomb(NULL, 'A', &st);
    ICUNIT_ASSERT_EQUAL((int)ret, 1, (int)ret);

    /* st 为 NULL 与有效状态对象等价（UTF-8 无状态，wctomb 内部即传 0） */
    ret = wcrtomb(buf, 'A', NULL);
    ICUNIT_ASSERT_EQUAL((int)ret, 1, (int)ret);
    ICUNIT_ASSERT_EQUAL(buf[0], 'A', buf[0]);

    ret = wcrtomb(buf, 0xE9, &st);
    ICUNIT_ASSERT_EQUAL((int)ret, 2, (int)ret);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[0], 0xC3, buf[0]);
    ICUNIT_ASSERT_EQUAL((unsigned char)buf[1], 0xA9, buf[1]);

    /* 往返一致性：wcrtomb 编码 U+20AC 后 mbtowc 解码回同一码点 */
    ret = wcrtomb(buf, 0x20AC, &st);
    ICUNIT_ASSERT_EQUAL((int)ret, 3, (int)ret);
    ICUNIT_ASSERT_EQUAL(mbtowc(&wc, buf, ret), 3, 3);
    ICUNIT_ASSERT_EQUAL((int)wc, 0x20AC, (int)wc);

    /* 代理区码点：非法，返回 (size_t)-1 并置 EILSEQ */
    errno = 0;
    ret = wcrtomb(buf, 0xD800, &st);
    ICUNIT_ASSERT_EQUAL(ret, (size_t)-1, (int)ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);

    /* 超出 Unicode 上限 0x10FFFF 的码点：同 EILSEQ 分支 */
    errno = 0;
    ret = wcrtomb(buf, 0x110000, &st);
    ICUNIT_ASSERT_EQUAL(ret, (size_t)-1, (int)ret);
    ICUNIT_ASSERT_EQUAL(errno, EILSEQ, errno);
    return 0;
}

/* 用例简要描述: MB_CUR_MAX 宏求值返回 4 */
LITE_TEST_CASE(PosixStdlibMultibyteTest, testStdlibMbCurMax001, Function | MediumTest | Level1)
{
    size_t maxCurMb;

    /* __ctype_get_mb_cur_max.c:4-6 返回 locale_impl.h 的编译期常量
     * CURRENT_UTF8 ? 4 : 1（__c_dot_utf8_locale.cat[LC_CTYPE] 恒非空） */
    maxCurMb = MB_CUR_MAX;
    ICUNIT_ASSERT_EQUAL(maxCurMb, 4, (int)maxCurMb);
    return 0;
}

RUN_TEST_SUITE(PosixStdlibMultibyteTest);

void PosixStdlibMultibyteFuncTest(void)
{
    LOG("begin PosixStdlibMultibyteFuncTest....");
    RUN_ONE_TESTCASE(testStdlibMbtowc001);
    RUN_ONE_TESTCASE(testStdlibMbtowcUtf8001);
    RUN_ONE_TESTCASE(testStdlibWctomb001);
    RUN_ONE_TESTCASE(testStdlibWcrtomb001);
    RUN_ONE_TESTCASE(testStdlibMbCurMax001);

    return;
}
