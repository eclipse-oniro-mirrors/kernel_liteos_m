/* it_mcs_pfs.c — pfs 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsPfs001(VOID);
extern VOID ItMcsPfs002(VOID);
extern VOID ItMcsPfs201(VOID);
extern VOID ItMcsPfs202(VOID);
extern VOID ItMcsPfs203(VOID);
extern VOID ItMcsPfs204(VOID);

const mcs_case_fn_t mcs_cases_pfs[] = {
    ItMcsPfs001,
    ItMcsPfs002,
    ItMcsPfs201,
    ItMcsPfs202,
    ItMcsPfs203,
    ItMcsPfs204,
};
const int mcs_cases_pfs_count = sizeof(mcs_cases_pfs) / sizeof(mcs_cases_pfs[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_pfs = { "pfs", mcs_cases_pfs, sizeof(mcs_cases_pfs) / sizeof(mcs_cases_pfs[0]) };
