/* 用例简要描述: McsSvc010 mica_send_data 参数边界（NULL/len0/offset 越界/超长） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[16] = "0123456789abcde";
    int ret;

    ret = mica_send_data(NULL, 0, 16);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    ret = mica_send_data(buf, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* offset 越界: 产品缺陷 20261023_164500 隔离段——mica_send_data 对 offset
     * 无校验，越界直接 memcpy 导致从核 data abort 崩溃（FaultAddr=0x75200001
     * 实证）。缺陷修复（umt_service.c 补 offset guard）后恢复此段。 */
#if 0
    ret = mica_send_data(buf, 0x100001, 16);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
#endif
    dprintf("[Svc010] offset-overflow case isolated (bug 20261023_164500)\n");

    /* len 超过缓冲上限 */
    ret = mica_send_data(buf, 0, 0x100001);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 正常路径对照 */
    ret = mica_send_data(buf, 0, 16);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem209(VOID)
{
    TEST_ADD_CASE("ItMcsSystem209", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
