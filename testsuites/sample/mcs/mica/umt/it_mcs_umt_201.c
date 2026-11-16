/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the conditions of the Apache License, Version 2.0.
 */
/* ItMcsUmt201: 主核 UMT ctx 生命周期（远程执行：create/destroy/复用） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct rpc_exec_umt_ctx_create_args ca = {0, 0} /* ped=0: baremetal（主核 MCS_KM_PED_BAREMETAL） */;
    struct rpc_exec_umt_ok_resp ok;
    uint32_t rl = 0;
    int ret;

    (void)memset_s(&ok, sizeof(ok), 0, sizeof(ok));
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_CREATE, &ca, sizeof(ca), &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(ok.ok, 1, ok.ok, EXIT);

    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(ok.ok, 1, ok.ok, EXIT);

    /* destroy 后再 create（ctx 复用路径） */
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_CREATE, &ca, sizeof(ca), &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(ok.ok, 1, ok.ok, EXIT);
    ret = mica_rpc_call(RPC_FUNC_EXEC_UMT_CTX_DESTROY, NULL, 0, &ok, sizeof(ok), &rl, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsUmt201(VOID)
{
    TEST_ADD_CASE("ItMcsUmt201", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL0, TEST_FUNCTION);
}
