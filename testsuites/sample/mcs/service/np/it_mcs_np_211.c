/* 用例简要描述: McsNp009 fd 失效负路径（重复 close/已关 fd 发送，D6 fd 归属语义） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    int fd, ret;

    fd = np_socket(2, 1, 0); /* AF_INET/SOCK_STREAM */
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);

    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 重复 close: 负 errno（-EBADF 类） */
    ret = np_close(fd);
    dprintf("[Np009] double close ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 已关 fd 发送: 负 errno */
    {
        uint8_t buf[4] = "x";

        ret = np_send(fd, buf, 1, 0);
        dprintf("[Np009] send on closed fd ret=%d\n", ret);
        ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp211(VOID)
{
    TEST_ADD_CASE("ItMcsNp211", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
