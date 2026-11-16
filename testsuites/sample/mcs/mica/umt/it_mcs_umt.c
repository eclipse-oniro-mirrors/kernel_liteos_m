/* it_mcs_umt.c — umt 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsUmt001(VOID);
extern VOID ItMcsUmt201(VOID);
extern VOID ItMcsUmt202(VOID);
extern VOID ItMcsUmt203(VOID);
extern VOID ItMcsUmt204(VOID);
extern VOID ItMcsUmt205(VOID);
extern VOID ItMcsUmt301(VOID);

const mcs_case_fn_t mcs_cases_umt[] = {
    ItMcsUmt001,
    // ItMcsUmt201,   /* 临时屏蔽：UmtHost 域：EXEC_UMT 0x340-346 host 侧未实现（host_e2e 阶段补） */
    // ItMcsUmt202,   /* 临时屏蔽：同上 */
    // ItMcsUmt203,   /* 临时屏蔽：同上 */
    // ItMcsUmt204,   /* 临时屏蔽：同上 */
    // ItMcsUmt205,   /* 临时屏蔽：同上 */
    ItMcsUmt301,
};
const int mcs_cases_umt_count = sizeof(mcs_cases_umt) / sizeof(mcs_cases_umt[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_umt = { "umt", mcs_cases_umt, sizeof(mcs_cases_umt) / sizeof(mcs_cases_umt[0]) };
