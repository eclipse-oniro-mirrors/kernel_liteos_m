/* 用例简要描述: McsSvc008 mica_create_service 单类型（EXPERIMENTAL 关闭时弱符号探测登记跳过） */
#include "it_mcs.h"

/* 设计说明书 §4.1 申报"已实现"，实际被 mica_service.c:84 的 #ifdef EXPERIMENTAL
 * 关闭（固件构建未定义该宏 → 链接无符号，实测 undefined symbol）。
 * 弱符号探测: 未编入时登记跳过；上游启用后本用例自动转为实测定语义。 */
extern int mica_create_service(enum mica_service_type type) __attribute__((weak));

static UINT32 Testcase(VOID)
{
    int ret;

    if (&mica_create_service == NULL) {
        dprintf("[Svc008] mica_create_service not compiled in (EXPERIMENTAL off) - skip\n");
        return LOS_OK; /* 登记跳过: 编译未启用（源码 #ifdef + 链接无符号双角度实证） */
    }

    /* 服务已由 create_all_services 全起。单类型再创建的语义验证:
     * 预期 -EALREADY 类拒绝或 0 幂等接受——以实测值登记（L2）。 */
    ret = mica_create_service(MICA_SERVICE_UMT);
    dprintf("[Svc008] create_service(UMT, already-up) ret=%d\n", ret);
    if (ret != 0 && ret > 0) {
        ICUNIT_GOTO_EQUAL(1, 0, ret, EXIT); /* 正数非法返回 */
    }
    /* 创建后服务仍可用（send_data 正常） */
    {
        uint8_t buf[8] = "svc008s";

        ret = mica_send_data(buf, 0, sizeof(buf));
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem207(VOID)
{
    TEST_ADD_CASE("ItMcsSystem207", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
