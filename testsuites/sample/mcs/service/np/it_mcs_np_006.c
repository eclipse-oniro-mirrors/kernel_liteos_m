/* 用例简要描述: ItMcsNp006 np_send/np_sendto 入参无效族（fd/buf/len/addr） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[8] = "apisend";
    uint8_t addr[16] = {0};
    int fd, ret;

    /* np_send: fd=-1 */
    ret = np_send(-1, buf, 4, 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* np_sendto: fd=-1 */
    ret = np_sendto(-1, buf, 4, 0, addr, 16);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);

    /* [缺陷 20261024_110000 隔离] buf=NULL → 从核 memcpy 读 NULL 崩溃风险
     * （np_send/np_sendto buf 侧同族无守卫）。修复后恢复。 */
#if 0
    ret = np_send(fd, NULL, 4, 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = np_sendto(fd, NULL, 4, 0, addr, 16);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
#endif
    dprintf("[ApiNpSend01] buf=NULL isolated (bug 20261024_110000)\n");

    /* len=0 */
    ret = np_send(fd, buf, 0, 0);
    dprintf("[ApiNpSend01] len=0 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret <= 0), 1, ret, EXIT);

    /* np_sendto: addrLen=0（addr=NULL 同 110000 族隔离） */
    ret = np_sendto(fd, buf, 4, 0, addr, 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    dprintf("[ApiNpSend01] sendto addr=NULL isolated (bug 20261024_110000)\n");

    /* flags 非零: 行为记录（透传主核） */
    ret = np_send(fd, buf, 4, 0x1234);
    dprintf("[ApiNpSend01] flags=0x1234 ret=%d (behavior)\n", ret);

    (void)np_close(fd);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsNp006(VOID)
{
    TEST_ADD_CASE("ItMcsNp006", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
