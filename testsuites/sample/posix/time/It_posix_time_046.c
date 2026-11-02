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
 * 用例名称: ItPosixTime046
 * 用例描述: 时区串格式归一化与拒绝矩阵
 *
 * 覆盖目标:
 *   lib/posix/src/tzdst.c:
 *     TimezoneFormat :102-103  缺省 +/- 号(第 4 位是数字) → 插入 '+'
 *     TimezoneFormat :116      单位分钟高位补 '0'
 *     TimezoneFormat :129      单位秒高位补 '0'
 *     settimezone    :162      buff==NULL → ERROR
 *     settimezone    :172      拷贝超长(>15) memcpy_s 失败 → ERROR
 *     settimezone    :176      TimezoneFormat FALSE → ERROR
 *   (034 只测了 "XXX+8"/"CST-8" 两种完全形态)
 *
 * 断言口径(时区全局量 timezone, 西正取负):
 *   - settimezone("UTC8")        → timezone == -28800(补 '+' + 时钟补零)
 *   - settimezone("UTC+8:5")     → timezone == -29100(分钟补零)
 *   - settimezone("UTC+8:00:5")  → timezone == -28805(秒补零)
 *   - NULL / 16 字节超长 / "UTC+x" → 拒绝且 timezone 保持不变
 *   - EXIT: 恢复原 timezone 值
 */

/* 用例简要描述: 时区串格式归一化与拒绝矩阵 */
#include "It_posix_time.h"
#include "tzdst.h"

extern long timezone;

static UINT32 Testcase(VOID)
{
    long tzSave = timezone;
    long tzBefore;

    /* 1. 缺省符号: "UTC8" → 补 '+' → "UTC+08:00:00" → -28800 */
    settimezone("UTC8");
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)-28800, (INT64)timezone, EXIT);

    /* 2. 单位分钟补零: "UTC+8:5" → "UTC+08:05:00" → -(8*3600+5*60) */
    settimezone("UTC+8:5");
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)-29100, (INT64)timezone, EXIT);

    /* 3. 单位秒补零: "UTC+8:00:5" → "UTC+08:00:05" → -(8*3600+5) */
    settimezone("UTC+8:00:5");
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)-28805, (INT64)timezone, EXIT);

    /* 4. NULL 拒绝且不改变当前值(:162) */
    tzBefore = timezone;
    settimezone(NULL);
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)tzBefore, (INT64)timezone, EXIT);

    /* 5. 超长拒绝(16 字节 > 标准 15, memcpy_s 失败, :172) */
    settimezone("aaaaaaaaaaaaaaaa"); /* 16: overlong */
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)tzBefore, (INT64)timezone, EXIT);

    /* 6. 格式错误拒绝("UTC+x": 符号后非数字, :176) */
    settimezone("UTC+x");
    ICUNIT_GOTO_EQUAL((INT64)timezone, (INT64)tzBefore, (INT64)timezone, EXIT);

EXIT:
    /* 恢复原时区(公开 POSIX 数据符号, 秒级精确恢复) */
    timezone = tzSave;
    return LOS_OK;
}

VOID ItPosixTime046(VOID)
{
    TEST_ADD_CASE("ItPosixTime046", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
