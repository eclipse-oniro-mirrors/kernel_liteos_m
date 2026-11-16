/* 用例简要描述: ItMcsNp201 server 侧 np opcode 负参数族（覆盖主核 handler 校验分支）
 * 目标: mcs_test_server.c L616-770 的 plen/EINVAL/EBADF 分支——正向用例打不到的负路径 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[8] = "negtest";
    uint8_t rxb[16];
    int fd, fd2, ret;

    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);

    /* 1. np_socket 负 domain → 主核 socket() 失败 → 从核收负 errno */
    ret = np_socket(-1, 1, 0);
    dprintf("[NpNeg01] socket domain=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 2. np_socket 负 type */
    ret = np_socket(2, -1, 0);
    dprintf("[NpNeg01] socket type=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 3. np_connect 负 fd（未 socket 的 fd → 主核 connect() EBADF） */
    ret = np_connect(-1, addr, IT_NP_SOCKADDR_LEN);
    dprintf("[NpNeg01] connect fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 4. np_send 负 fd → EBADF 路径 */
    ret = np_send(-1, buf, 4, 0);
    dprintf("[NpNeg01] send fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 5. np_recv 负 fd → EBADF */
    ret = np_recv(-1, rxb, sizeof(rxb), 0, 100);
    dprintf("[NpNeg01] recv fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 6. np_close 负 fd → EBADF */
    ret = np_close(-1);
    dprintf("[NpNeg01] close fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 7. 双关 fd: close 后再 send → EBADF（server 端 fd 表已释放） */
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = np_send(fd, buf, 4, 0);
    dprintf("[NpNeg01] send closed-fd ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 8. sendto 负 fd */
    ret = np_sendto(-1, buf, 4, 0, addr, IT_NP_SOCKADDR_LEN);
    dprintf("[NpNeg01] sendto fd=-1 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 9. recvfrom 负 fd */
    {
        uint32_t fl = IT_NP_SOCKADDR_LEN;
        ret = np_recvfrom(-1, rxb, sizeof(rxb), 0, 100, addr, &fl);
        dprintf("[NpNeg01] recvfrom fd=-1 ret=%d\n", ret);
        ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    }

    /* 10. connect 到本机未监听端口 → ECONNREFUSED（server 侧 poll+SO_ERROR 路径） */
    fd2 = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd2, -1, fd2, EXIT);
    {
        uint8_t bad[IT_NP_SOCKADDR_LEN];
        ItNpBuildAddr(bad, 127, 0, 0, 1, 1); /* port 1: 未监听 */
        ret = np_connect(fd2, bad, IT_NP_SOCKADDR_LEN);
        dprintf("[NpNeg01] connect refused ret=%d (expect <0)\n", ret);
        ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    }
    (void)np_close(fd2);

    /* 11. recv 超时路径: 合法 fd 无数据 → -ETIMEDOUT（server poll 空转） */
    fd2 = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd2, -1, fd2, EXIT);
    ret = np_recv(fd2, rxb, sizeof(rxb), 0, 500);
    dprintf("[NpNeg01] recv idle-timeout ret=%d (expect <0)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    (void)np_close(fd2);

    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp201(VOID)
{
    TEST_ADD_CASE("ItMcsNp201", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
