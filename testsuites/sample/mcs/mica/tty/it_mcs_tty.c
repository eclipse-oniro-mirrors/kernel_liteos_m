/* it_mcs_tty.c — tty 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsTty001(VOID);
extern VOID ItMcsTty301(VOID);

const mcs_case_fn_t mcs_cases_tty[] = {
    ItMcsTty001,
    ItMcsTty301,
};
const int mcs_cases_tty_count = sizeof(mcs_cases_tty) / sizeof(mcs_cases_tty[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_tty = { "tty", mcs_cases_tty, sizeof(mcs_cases_tty) / sizeof(mcs_cases_tty[0]) };
