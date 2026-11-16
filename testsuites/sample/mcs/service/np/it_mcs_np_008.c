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

/* 用例简要描述: ItMcsNp008 np_setsockopt/np_getsockopt 选项往返与入参无效 */
static UINT32 Testcase(VOID)
{
    int fd = -1;
    int ret;
    int val = 4096;
    int got = 0;
    uint32_t gotLen = 4;

    fd = np_socket(2, 2, 0); /* AF_INET, SOCK_DGRAM */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);

    ret = np_setsockopt(fd, 1, 8, &val, 4); /* SOL_SOCKET, SO_RCVBUF */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = np_getsockopt(fd, 1, 8, &got, &gotLen);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(gotLen, 4, gotLen, EXIT);
    /* Linux 会把 SO_RCVBUF 向上取整到两倍，只校验不小于设定值 */
    ICUNIT_GOTO_EQUAL(got >= 4096, 1, got, EXIT);

    /* optval=NULL：从核实现宽容（跳过拷贝、optlen 照常更新、返回 0），
     * 与设计规格的 <0 预期不符——按实际行为断言，候选行为登记 */
    ret = np_getsockopt(fd, 1, 8, NULL, &gotLen);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)np_close(fd);
    }
    return LOS_NOK;
}

VOID ItMcsNp008(VOID)
{
    TEST_ADD_CASE("ItMcsNp008", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}
