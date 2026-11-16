/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the conditions of the Apache License, Version 2.0.
 */
/* ItMcsUmt205: 主核回调模式（注册后从核发 → 回调计数>0；回调模式下 receive 互斥行为记录） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct rpc_exec_umt_ctx_create_args ca = {0, 0} /* ped=0: baremetal（主核 MCS_KM_PED_BAREMETAL） */;
    struct rpc_exec_umt_register_args ra = {0};
    struct rpc_exec_umt_recv_args idle = {500, 32};
    struct rpc_exec_umt_cb_count_resp cnt;
    struct rpc_exec_umt_ret_resp rr;
    struct rpc_exec_umt_ok_resp ok;
    struct {
        struct rpc_exec_umt_recv_args h;
        uint8_t data[32];
    } recv;
    uint8_t tx[8];
    uint32_t rl = 0;
    int ret, i, spins;

    (void)memset_s(&cnt, sizeof(cnt), 0, sizeof(cnt));
    (void)memset_s(&ok, sizeof(ok), 0, sizeof(ok));
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_CREATE, &ca, sizeof(ca), &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_REGISTER, &ra, sizeof(ra), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret, 0, rr.ret, EXIT);

    /* 回调模式下 receive：文档说同 ctx 不得混用——行为记录（不 crash 即可） */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_RECV, &idle, sizeof(idle), &recv, sizeof(recv), &rl, 4000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 从核发一帧 → 主核回调应触发（计数>0） */
    for (i = 0; i < (int)sizeof(tx); i++) {
        tx[i] = (uint8_t)(0x50 + i);
    }
    ret = mica_send_data(tx, 0, sizeof(tx));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    for (spins = 0; spins < 25; spins++) { /* 200ms x 25 = 5s 窗口 */
        ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CB_COUNT, NULL, 0, &cnt, sizeof(cnt), &rl, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        if (cnt.count > 0) {
            break;
        }
        LOS_TaskDelay(20);
    }
    ICUNIT_GOTO_EQUAL(cnt.count > 0, 1, cnt.count, EXIT);

    (void)mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    return LOS_OK;
EXIT:
    (void)mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    return LOS_NOK;
}

VOID ItMcsUmt205(VOID)
{
    TEST_ADD_CASE("ItMcsUmt205", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL0, TEST_FUNCTION);
}
