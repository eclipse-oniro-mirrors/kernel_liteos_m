/* 用例简要描述: ItMcsNp009 np_socket 入参无效族+组合（domain/type/protocol） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    int fd, ret;

    /* domain 非法 */
    ret = np_socket(-1, 1, 0);
    dprintf("[ApiNpSocket01] domain=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* type 非法 */
    ret = np_socket(2, -1, 0);
    dprintf("[ApiNpSocket01] type=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 组合: domain 对 + type 错 */
    ret = np_socket(2, 99, 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 组合: 全错 */
    ret = np_socket(-5, -5, -5);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 对照: 正常（AF_INET=2/SOCK_STREAM=1） */
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp009(VOID)
{
    TEST_ADD_CASE("ItMcsNp009", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
