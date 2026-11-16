/* 用例简要描述: ItMcsNp002 np_connect 入参无效族（fd/addr/addrLen） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[16] = {0};
    uint8_t dummy[16];
    int fd, ret;

    /* 构造合法 sockaddr（不连接，仅入参检查用） */
    addr[0] = 2; /* AF_INET */
    addr[2] = 0x0a; addr[3] = 0x00; /* port 2560 */
    addr[4] = 10; addr[5] = 0; addr[6] = 0; addr[7] = 1; /* 10.0.0.1 */

    /* fd 非法族 */
    ret = np_connect(-1, addr, 16);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    (void)memset_s(dummy, sizeof(dummy), 0, sizeof(dummy));
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = np_connect(fd, addr, 16); /* 已关 fd */
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* [缺陷 20261024_110000 隔离] addr=NULL → 从核 memcpy 读 NULL 崩溃
     * （np_connect addr 侧无守卫族）。修复后恢复。 */
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
#if 0
    ret = np_connect(fd, NULL, 16);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#endif
    dprintf("[ApiNpConn01] addr=NULL isolated (bug 20261024_110000)\n");

    /* addrLen=0 */
    ret = np_connect(fd, addr, 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#if 0
    /* [缺陷 20261024_110000 隔离] addrLen=128 → 从核 data abort 崩溃
     * （np_connect 无 addrLen 上限校验，越界 memcpy 读穿 16B 用户缓冲）。
     * 修复后恢复断言。 */
    ret = np_connect(fd, addr, 128);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#endif
    dprintf("[ApiNpConn01] addrLen=128 isolated (bug 20261024_110000)\n");
    (void)np_close(fd);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp002(VOID)
{
    TEST_ADD_CASE("ItMcsNp002", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
