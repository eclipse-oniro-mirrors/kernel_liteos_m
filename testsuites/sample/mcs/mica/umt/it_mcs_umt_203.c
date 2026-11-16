/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the conditions of the Apache License, Version 2.0.
 */
/* ItMcsUmt203: 主核 send_data_to_rtos 正常 32B + 零长行为 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct {
        struct rpc_exec_umt_send_args h;
        uint8_t data[32];
    } sa;
    struct rpc_exec_umt_ret_resp rr;
    uint32_t rl = 0;
    int ret, i;

    (void)memset_s(&sa, sizeof(sa), 0, sizeof(sa));
    (void)memset_s(&rr, sizeof(rr), 0, sizeof(rr));
    sa.h.len = sizeof(sa.data);
    for (i = 0; i < (int)sizeof(sa.data); i++) {
        sa.data[i] = (uint8_t)(i + 1);
    }
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_SEND, &sa, sizeof(sa), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret, 0, rr.ret, EXIT);

    /* 零长——行为记录（不 crash 即可） */
    sa.h.len = 0;
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_SEND, &sa, sizeof(sa.h), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsUmt203(VOID)
{
    TEST_ADD_CASE("ItMcsUmt203", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL0, TEST_FUNCTION);
}
