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
 * 用例名称: ItPosixQueue055
 * 用例描述: mq_setattr 旧属性回读接口
 *
 * 覆盖目标:
 *   lib/posix/src/mqueue.c:
 *     OsMqSetAttr :417  mqOldAttr != NULL 时先回读旧属性再写入新属性
 *     (现有 4 个 setattr 用例 044/048/049/050 均传 old=NULL, 回读分支未覆盖)
 *
 * 断言口径:
 *   初始(阻塞模式): mq_setattr(设 O_NONBLOCK, &old) → 0 且 old.mq_flags
 *     不含 O_NONBLOCK 位
 *   非阻塞态:       mq_setattr(清 O_NONBLOCK, &old2) → 0 且 old2.mq_flags
 *     含 O_NONBLOCK 位
 *   再用 mq_getattr 交叉验证最终回到阻塞模式(不含 O_NONBLOCK 位)
 *
 * 实现语义偏离说明(对比初版):
 *   本实现 mq_personal->mq_flags 存储完整 openFlag(mqueue.c:221/253), 回读
 *   old.mq_flags == O_CREAT|O_RDWR(实测 0x42), 而 POSIX 规定 mq_flags 仅
 *   定义 O_NONBLOCK 位(记录为实现偏差)。故断言改为只校验 O_NONBLOCK 位,
 *   覆盖目标(回读旧属性分支 :416-418)不变。
 *   另: 初版失败路径只 close 不 unlink, 造成队列/内存泄漏(实测 ResLeak),
 *   已补齐清理。
 */

/* 用例简要描述: mq_setattr 旧属性回读接口 */
#include "It_posix_queue.h"

static UINT32 Testcase(VOID)
{
    CHAR mqname[MQUEUE_STANDARD_NAME_LENGTH] = "";
    mqd_t mqdes;
    struct mq_attr newAttr = {0};
    struct mq_attr oldAttr = {0};
    struct mq_attr chkAttr = {0};
    INT32 ret;

    ret = snprintf_s(mqname, MQUEUE_STANDARD_NAME_LENGTH, MQUEUE_STANDARD_NAME_LENGTH - 1,
                     "/mq055_%d", LOS_CurTaskIDGet());
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);

    mqdes = mq_open(mqname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_GOTO_NOT_EQUAL(mqdes, (mqd_t)-1, (UINTPTR)mqdes, EXIT1);

    /* 设非阻塞: 回读旧属性应不含 O_NONBLOCK(初始阻塞模式) */
    (VOID)memset_s(&newAttr, sizeof(newAttr), 0, sizeof(newAttr));
    (VOID)memset_s(&oldAttr, sizeof(oldAttr), 0, sizeof(oldAttr));
    newAttr.mq_flags = (long)O_NONBLOCK;
    ret = mq_setattr(mqdes, &newAttr, &oldAttr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((long)(oldAttr.mq_flags & (long)O_NONBLOCK), 0,
                      (long)oldAttr.mq_flags, EXIT);

    /* 清非阻塞: 回读旧属性应含 O_NONBLOCK */
    (VOID)memset_s(&newAttr, sizeof(newAttr), 0, sizeof(newAttr));
    (VOID)memset_s(&oldAttr, sizeof(oldAttr), 0, sizeof(oldAttr));
    newAttr.mq_flags = 0;
    ret = mq_setattr(mqdes, &newAttr, &oldAttr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((long)(oldAttr.mq_flags & (long)O_NONBLOCK), (long)O_NONBLOCK,
                      (long)oldAttr.mq_flags, EXIT);

    /* 交叉验证: 最终回到阻塞模式(不含 O_NONBLOCK) */
    (VOID)memset_s(&chkAttr, sizeof(chkAttr), 0, sizeof(chkAttr));
    ret = mq_getattr(mqdes, &chkAttr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((long)(chkAttr.mq_flags & (long)O_NONBLOCK), 0,
                      (long)chkAttr.mq_flags, EXIT);

    ret = mq_close(mqdes);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT2);
    ret = mq_unlink(mqname);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT1);
    return LOS_OK;
EXIT:
    (VOID)mq_close(mqdes);
    (VOID)mq_unlink(mqname); /* 失败路径同样要 unlink, 否则队列泄漏 */
    goto EXIT1;
EXIT2:
    (VOID)mq_unlink(mqname);
EXIT1:
    return LOS_NOK;
}

VOID ItPosixQueue055(VOID)
{
    TEST_ADD_CASE("ItPosixQueue055", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
