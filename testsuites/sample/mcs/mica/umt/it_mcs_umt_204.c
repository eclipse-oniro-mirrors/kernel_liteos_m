/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the conditions of the Apache License, Version 2.0.
 */
/* ItMcsUmt204: 主核 receive_data_from_rtos 闭环（本用例发 → 远程收 → 数据一致）
 * 时序由用例自控（UMT 共享内存先写后读），不依赖从核其他用例配合。 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct {
        struct rpc_exec_umt_recv_args h;
        uint8_t data[64];
    } recv;
    struct rpc_exec_umt_recv_args idle = {2000, 64};
    struct rpc_exec_umt_recv_resp *pr;
    uint8_t tx[16];
    uint32_t rl = 0;
    int ret, i;

    /* 空闲超时路径：无数据时 2s 超时负返回 */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_RECV, &idle, sizeof(idle), &recv, sizeof(recv), &rl, 8000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    pr = (struct rpc_exec_umt_recv_resp *)&recv;
    ICUNIT_GOTO_EQUAL(pr->ret < 0, 1, pr->ret, EXIT);

    /* 正常闭环：从核先发 → 命令主核收 → 断言数据一致 */
    for (i = 0; i < (int)sizeof(tx); i++) {
        tx[i] = (uint8_t)(0xA0 + i);
    }
    ret = mica_send_data(tx, 0, sizeof(tx));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&recv, sizeof(recv), 0, sizeof(recv));
    recv.h.timeout_ms = 3000;
    recv.h.rlen = sizeof(recv.data);
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_RECV, &recv, sizeof(recv.h), &recv, sizeof(recv), &rl, 8000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    pr = (struct rpc_exec_umt_recv_resp *)&recv;
    ICUNIT_GOTO_EQUAL(pr->ret, 0, pr->ret, EXIT);
    ICUNIT_GOTO_EQUAL(pr->rlen, sizeof(tx), pr->rlen, EXIT);
    for (i = 0; i < (int)sizeof(tx); i++) {
        ICUNIT_GOTO_EQUAL(recv.data[i], tx[i], recv.data[i], EXIT);
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsUmt204(VOID)
{
    TEST_ADD_CASE("ItMcsUmt204", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL0, TEST_FUNCTION);
}
