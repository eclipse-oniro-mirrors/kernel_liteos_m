/* 用例简要描述: McsSvc012 tty_printf 长行与格式化边界（128B 行不截断） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    char line[136];
    int i, ret, expect;

    for (i = 0; i < 128; i++) {
        line[i] = 'A' + (i % 26);
    }
    line[128] = '\n';
    line[129] = '\0';
    expect = 129 + 12 + 1; /* 前缀12 + 128字符 + 换行 + 终结符（mica_tty_printf 实现发送 len+1） */

    ret = mica_tty_printf("E2E-TTY-012:%s", line);
    ICUNIT_GOTO_EQUAL(ret, expect, ret, EXIT); /* 实测语义: 返回值含终结符 */

    /* 混合格式化 */
    ret = mica_tty_printf("E2E-TTY-012:x=%08x,c=%c\n", 0xdeadbeef, 'Z');
    ICUNIT_GOTO_EQUAL((ret > 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem211(VOID)
{
    TEST_ADD_CASE("ItMcsSystem211", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
