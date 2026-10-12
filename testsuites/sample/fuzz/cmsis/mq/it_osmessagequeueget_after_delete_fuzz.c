/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ItFuzzOsMqGetAfterDel: osMessageQueueGet 于 osMessageQueueDelete 之后
 *
 * 【现象】osMessageQueueDelete 后再 osMessageQueueGet 同一 id 触发 use-after-destroy（ws63）。
 * 【根因】队列 CB 已回池；osMessageQueueGet 直接读取其 queueID 并读队列 ——
 * 对已回收 CB 无失效校验（与 S07/S08 同类缺陷）。
 * 【修复方向】适配层在取 queueID 前校验 CB 是否仍处于已分配状态。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S09_OSMQ_GET_AFTER_DEL 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 * 【关联】同源: FUZZ_BUG_S07/S08
 */

#include "it_cmsis_mq_fuzz.h"

static void __attribute__((unused)) FzCmsisThreadEntry(void *arg) { (VOID)arg; osDelay(1); }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    osMessageQueueId_t mid;
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        mid = osMessageQueueNew(4, 8, NULL);
        if (mid != NULL) {
            (VOID)osMessageQueueDelete(mid);
            (VOID)osMessageQueueGet(mid, NULL, NULL, 3);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzOsMqGetAfterDel(VOID)
{
    TEST_ADD_CASE("ItFuzzOsMqGetAfterDel", TestCase, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
}
