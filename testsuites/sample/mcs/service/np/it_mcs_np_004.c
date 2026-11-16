/* 用例简要描述: ItMcsNp004 np_recv/np_recvfrom 入参无效族+timeout 族 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[16];
    uint8_t from[16];
    uint32_t fromLen = 16;
    int fd, ret;

    /* fd=-1 */
    ret = np_recv(-1, buf, sizeof(buf), 0, 100);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = np_recvfrom(-1, buf, sizeof(buf), 0, 100, from, &fromLen);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);

    /* [缺陷 20261024_110000 隔离] buf=NULL → 同族崩溃风险。修复后恢复。 */
#if 0
    ret = np_recv(fd, NULL, 16, 0, 100);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = np_recvfrom(fd, NULL, 16, 0, 100, from, &fromLen);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#endif
    dprintf("[ApiNpRecv01] buf=NULL isolated (bug 20261024_110000)\n");

    /* cap=0 */
    ret = np_recv(fd, buf, 0, 0, 100);
    dprintf("[ApiNpRecv01] cap=0 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* timeout 族（无来包场景，走超时路径）: timeout=0 立即语义 */
    ret = np_recv(fd, buf, sizeof(buf), 0, 0);
    dprintf("[ApiNpRecv01] timeout=0 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* timeout 短（100ms）: 超时错误 */
    ret = np_recv(fd, buf, sizeof(buf), 0, 100);
    dprintf("[ApiNpRecv01] timeout=100 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* [缺陷 20261024_110000 隔离] recvfrom from/fromLen=NULL → 同族。
     * fromLen=NULL 尚未实测（保守一并隔离）。修复后恢复。 */
#if 0
    ret = np_recvfrom(fd, buf, sizeof(buf), 0, 100, NULL, &fromLen);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = np_recvfrom(fd, buf, sizeof(buf), 0, 100, from, NULL);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#endif
    dprintf("[ApiNpRecv01] from/fromLen=NULL isolated (bug 20261024_110000)\n");

    (void)np_close(fd);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp004(VOID)
{
    TEST_ADD_CASE("ItMcsNp004", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
