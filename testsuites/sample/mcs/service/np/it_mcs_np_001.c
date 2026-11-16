/* 用例简要描述: ItMcsNp001 np_close 入参无效族（已关/-1/极端值）+正常对照 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    int fd, ret;

    /* fd=-1 */
    ret = np_close(-1);
    dprintf("[ApiNpClose01] fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 极端值 */
    ret = np_close(0x7FFFFFFF);
    dprintf("[ApiNpClose01] fd=INT_MAX ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 正常开→关 */
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 双重 close */
    ret = np_close(fd);
    dprintf("[ApiNpClose01] double close ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp001(VOID)
{
    TEST_ADD_CASE("ItMcsNp001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
