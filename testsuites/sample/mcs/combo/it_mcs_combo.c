/* it_mcs_combo.c — combo 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsCombo401(VOID);
extern VOID ItMcsCombo402(VOID);
extern VOID ItMcsCombo403(VOID);
extern VOID ItMcsCombo404(VOID);
extern VOID ItMcsCombo405(VOID);
extern VOID ItMcsCombo406(VOID);
extern VOID ItMcsCombo407(VOID);
extern VOID ItMcsCombo408(VOID);
extern VOID ItMcsCombo409(VOID);
extern VOID ItMcsCombo410(VOID);
extern VOID ItMcsCombo411(VOID);
extern VOID ItMcsCombo412(VOID);
extern VOID ItMcsCombo413(VOID);
extern VOID ItMcsCombo414(VOID);

const mcs_case_fn_t mcs_cases_combo[] = {
    ItMcsCombo401,
    ItMcsCombo402,
    ItMcsCombo403,
    ItMcsCombo404,
    ItMcsCombo405,
    ItMcsCombo406,
    ItMcsCombo407,
    ItMcsCombo408,
    ItMcsCombo409,
    ItMcsCombo410,
    ItMcsCombo411,
    ItMcsCombo412,
    ItMcsCombo413,
    ItMcsCombo414,
};
const int mcs_cases_combo_count = sizeof(mcs_cases_combo) / sizeof(mcs_cases_combo[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_combo = { "combo", mcs_cases_combo, sizeof(mcs_cases_combo) / sizeof(mcs_cases_combo[0]) };
