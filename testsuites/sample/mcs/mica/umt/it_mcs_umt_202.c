/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the conditions of the Apache License, Version 2.0.
 */
/* ItMcsUmt202: 主核 UMT register/unregister 语义（NULL 拒绝/重复拒绝/复活） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct rpc_exec_umt_ctx_create_args ca = {0, 0} /* ped=0: baremetal（主核 MCS_KM_PED_BAREMETAL） */;
    struct rpc_exec_umt_register_args ra = {0};
    struct rpc_exec_umt_ret_resp rr;
    struct rpc_exec_umt_ok_resp ok;
    uint32_t rl = 0;
    int ret;

    (void)memset_s(&rr, sizeof(rr), 0, sizeof(rr));
    (void)memset_s(&ok, sizeof(ok), 0, sizeof(ok));
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_CREATE, &ca, sizeof(ca), &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(ok.ok, 1, ok.ok, EXIT);

    /* NULL 回调：预期负返回（拒绝） */
    ra.null_cb = 1;
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_REGISTER, &ra, sizeof(ra), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret < 0, 1, rr.ret, EXIT);

    /* 正常注册 */
    ra.null_cb = 0;
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_REGISTER, &ra, sizeof(ra), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret, 0, rr.ret, EXIT);

    /* 重复注册：预期负返回（-EALREADY 族） */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_REGISTER, &ra, sizeof(ra), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret < 0, 1, rr.ret, EXIT);

    /* 注销 */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_UNREGISTER, NULL, 0, &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret, 0, rr.ret, EXIT);

    /* 未注册时注销：预期负返回（错误路径） */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_UNREGISTER, NULL, 0, &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret < 0, 1, rr.ret, EXIT);

    /* 注销后再注册（ctx 复活路径） */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_REGISTER, &ra, sizeof(ra), &rr, sizeof(rr), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rr.ret, 0, rr.ret, EXIT);

    (void)mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    return LOS_OK;
EXIT:
    (void)mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    return LOS_NOK;
}

VOID ItMcsUmt202(VOID)
{
    TEST_ADD_CASE("ItMcsUmt202", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL0, TEST_FUNCTION);
}
