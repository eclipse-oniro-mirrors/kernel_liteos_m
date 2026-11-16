/* 用例简要描述: ItMcsSystem001 mica_service_is_ready 入参边界（type 非法值族） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    int ret;

    /* type 越界族: -1 / MAX / 超范围（实测记录行为，不崩溃为底线） */
    ret = mica_service_is_ready((enum mica_service_type)-1);
    dprintf("[ApiSvc01] type=-1 ret=%d\n", ret);

    ret = mica_service_is_ready(MICA_SERVICE_MAX);
    dprintf("[ApiSvc01] type=MAX ret=%d\n", ret);

    ret = mica_service_is_ready((enum mica_service_type)99);
    dprintf("[ApiSvc01] type=99 ret=%d\n", ret);

    /* 对照: 合法三类型均为 0/1 二值 */
    ret = mica_service_is_ready(MICA_SERVICE_RPC);
    ICUNIT_GOTO_EQUAL((ret == 0 || ret == 1), 1, ret, EXIT);
    ret = mica_service_is_ready(MICA_SERVICE_TTY);
    ICUNIT_GOTO_EQUAL((ret == 0 || ret == 1), 1, ret, EXIT);
    ret = mica_service_is_ready(MICA_SERVICE_UMT);
    ICUNIT_GOTO_EQUAL((ret == 0 || ret == 1), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem001(VOID)
{
    TEST_ADD_CASE("ItMcsSystem001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
