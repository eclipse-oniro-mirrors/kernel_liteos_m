/* 用例简要描述: McsNp010 np_* 参数负路径（非法 fd/NULL buf/非法 addrLen） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[8] = "nptest";
    uint8_t addr[16] = {0};
    int ret;

    /* 非法 fd */
    ret = np_send(-1, buf, 4, 0);
    dprintf("[Np010] send fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    ret = np_recv(-1, buf, sizeof(buf), 0, 100);
    dprintf("[Np010] recv fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* NULL buf */
    ret = np_recv(0, NULL, 16, 0, 100);
    dprintf("[Np010] recv NULL buf ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* connect 非法 addrLen */
    {
        int fd = np_socket(2, 1, 0);

        ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
        ret = np_connect(fd, addr, 0);
        dprintf("[Np010] connect addrLen=0 ret=%d\n", ret);
        ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
        (void)np_close(fd);
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp212(VOID)
{
    TEST_ADD_CASE("ItMcsNp212", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL2, TEST_FUNCTION);
}
