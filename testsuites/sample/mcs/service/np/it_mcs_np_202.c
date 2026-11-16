/* 用例简要描述: ItMcsNp202 np 用例打 server 内置 echo（UDP 7000/TCP 7001——原 0 覆盖的
 * udp/tcp_echo_thread 路径；主核 IP 从从核 np 视角=对端协议栈可达（server 进程回显） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "np-srv-echo-23B!!!!!!!";
    uint8_t rxb[32];
    int fd, ret;

    /* server 的 UDP echo 监听 :7000（np_connect 由主核执行 → 打主核本机即达） */
    ItNpBuildAddr(addr, 127, 0, 0, 1, 7000);
    fd = np_socket(2, 2, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);

    ret = np_sendto(fd, buf, 23, 0, addr, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 23, ret, EXIT);

    {
        uint32_t fl = IT_NP_SOCKADDR_LEN;
        ret = np_recvfrom(fd, rxb, sizeof(rxb), 0, 3000, addr, &fl);
        ICUNIT_GOTO_EQUAL(ret, 23, ret, EXIT);
        ICUNIT_GOTO_EQUAL(memcmp(rxb, buf, 23), 0, 1, EXIT);
    }
    (void)np_close(fd);
    dprintf("[NpSrvEcho01] UDP :7000 echo roundtrip 23B ok\n");

    /* TCP echo :7001 */
    ItNpBuildAddr(addr, 127, 0, 0, 1, 7001);
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_connect(fd, addr, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = np_send(fd, buf, 23, 0);
    ICUNIT_GOTO_EQUAL(ret, 23, ret, EXIT);
    ret = np_recv(fd, rxb, sizeof(rxb), 0, 3000);
    ICUNIT_GOTO_EQUAL(ret, 23, ret, EXIT);
    ICUNIT_GOTO_EQUAL(memcmp(rxb, buf, 23), 0, 1, EXIT);
    (void)np_close(fd);
    dprintf("[NpSrvEcho01] TCP :7001 echo roundtrip 23B ok\n");

    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp202(VOID)
{
    TEST_ADD_CASE("ItMcsNp202", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
