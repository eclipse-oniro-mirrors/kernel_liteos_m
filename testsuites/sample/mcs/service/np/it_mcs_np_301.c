/* 用例简要描述: ItMcsNp301 TCP echo 连续 100 次往返（echo server 环境依赖） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "stress-np-echo-23B!!!";
    uint8_t rxb[32];
    int fd, ret, i, fails = 0;

    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);

    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_connect(fd, addr, 16);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    for (i = 0; i < 100; i++) {
        ret = np_send(fd, buf, 23, 0);
        if (ret != 23) {
            dprintf("[StressNp01] #%d send=%d\n", i, ret);
            fails++;
            continue;
        }
        ret = np_recv(fd, rxb, sizeof(rxb), 0, 5000);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[StressNp01] #%d recv=%d\n", i, ret);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    (VOID)np_close(fd);
    dprintf("[StressNp01] done=%d fails=%d\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    (VOID)np_close(fd);
    return LOS_NOK;
}

VOID ItMcsNp301(VOID)
{
    TEST_ADD_CASE("ItMcsNp301", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
