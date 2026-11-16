/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "it_mcs.h"

/* 用例简要描述: ItMcsPfs203 pfs_getcwd/pfs_chdir 工作目录往返与绝对路径恢复 */
static UINT32 Testcase(VOID)
{
    char base[256];
    char now[256];
    int ret;

    (void)memset(base, 0, sizeof(base));
    (void)memset(now, 0, sizeof(now));

    /* 记住初始 cwd。实现契约：成功返回 cwd 字符串长度（>=0）而非 0 */
    ret = pfs_getcwd(base, sizeof(base));
    ICUNIT_GOTO_EQUAL(ret >= 0, 1, ret, EXIT);
    ret = pfs_chdir("/mnt");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_getcwd(now, sizeof(now));
    ICUNIT_GOTO_EQUAL(ret >= 0, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(strcmp(now, "/mnt"), 0, 1, EXIT);

    /* 用记录的绝对路径恢复原 cwd 并复核 */
    ret = pfs_chdir(base);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_getcwd(now, sizeof(now));
    ICUNIT_GOTO_EQUAL(ret >= 0, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(strcmp(now, base), 0, 1, EXIT);

    /* path=NULL：从核侧 -EINVAL */
    ret = pfs_chdir(NULL);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    if (base[0] != '\0') {
        (void)pfs_chdir(base); /* best-effort 恢复，避免残留影响后续用例 */
    }
    return LOS_NOK;
}

VOID ItMcsPfs203(VOID)
{
    TEST_ADD_CASE("ItMcsPfs203", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
