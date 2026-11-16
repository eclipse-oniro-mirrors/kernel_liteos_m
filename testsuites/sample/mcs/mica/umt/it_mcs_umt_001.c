/* 用例简要描述: ItMcsUmt001 umt 回调注册族（重复注册/空态注销/cb NULL）+rcv_data 互斥 */
#include "it_mcs.h"

static void ApiUmtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)data_len; (void)priv;
}

static UINT32 Testcase(VOID)
{
    uint8_t buf[16];
    size_t len = sizeof(buf);
    int ret;

    /* cb=NULL */
    ret = mica_umt_register_rcv_cb(NULL, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 正常注册 */
    ret = mica_umt_register_rcv_cb(ApiUmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 重复注册 → -EALREADY */
    ret = mica_umt_register_rcv_cb(ApiUmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, -114, ret, EXIT);

    /* 回调模式中调 mica_rcv_data → -EBUSY（§4.2.4 互斥） */
    ret = mica_rcv_data(buf, &len);
    dprintf("[ApiUmt01] rcv in cb-mode ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, -16, ret, EXIT);

    /* 注销后 rcv_data 不再 -EBUSY（未就绪/阻塞语义恢复） */
    ret = mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsUmt001(VOID)
{
    TEST_ADD_CASE("ItMcsUmt001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL1, TEST_FUNCTION);
}
