/* it_mcs_np.c — np 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsNp001(VOID);
extern VOID ItMcsNp002(VOID);
extern VOID ItMcsNp003(VOID);
extern VOID ItMcsNp004(VOID);
extern VOID ItMcsNp005(VOID);
extern VOID ItMcsNp006(VOID);
extern VOID ItMcsNp007(VOID);
extern VOID ItMcsNp008(VOID);
extern VOID ItMcsNp009(VOID);
extern VOID ItMcsNp201(VOID);
extern VOID ItMcsNp202(VOID);
extern VOID ItMcsNp203(VOID);
extern VOID ItMcsNp204(VOID);
extern VOID ItMcsNp205(VOID);
extern VOID ItMcsNp206(VOID);
extern VOID ItMcsNp207(VOID);
extern VOID ItMcsNp208(VOID);
extern VOID ItMcsNp209(VOID);
extern VOID ItMcsNp210(VOID);
extern VOID ItMcsNp211(VOID);
extern VOID ItMcsNp212(VOID);
extern VOID ItMcsNp301(VOID);

const mcs_case_fn_t mcs_cases_np[] = {
    ItMcsNp001,
    ItMcsNp002,
    ItMcsNp003,
    ItMcsNp004,
    ItMcsNp005,
    ItMcsNp006,
    ItMcsNp007,
    ItMcsNp008,
    ItMcsNp009,
    ItMcsNp201,
    ItMcsNp202,
    ItMcsNp203,
    ItMcsNp204,
    ItMcsNp205,
    ItMcsNp206,
    ItMcsNp207,
    ItMcsNp208,
    ItMcsNp209,
    ItMcsNp210,
    ItMcsNp211,
    ItMcsNp212,
    ItMcsNp301,
};
const int mcs_cases_np_count = sizeof(mcs_cases_np) / sizeof(mcs_cases_np[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_np = { "np", mcs_cases_np, sizeof(mcs_cases_np) / sizeof(mcs_cases_np[0]) };
