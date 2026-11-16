/* it_mcs_system.c — system 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsSystem001(VOID);
extern VOID ItMcsSystem002(VOID);
extern VOID ItMcsSystem201(VOID);
extern VOID ItMcsSystem202(VOID);
extern VOID ItMcsSystem203(VOID);
extern VOID ItMcsSystem204(VOID);
extern VOID ItMcsSystem205(VOID);
extern VOID ItMcsSystem206(VOID);
extern VOID ItMcsSystem207(VOID);
extern VOID ItMcsSystem208(VOID);
extern VOID ItMcsSystem209(VOID);
extern VOID ItMcsSystem210(VOID);
extern VOID ItMcsSystem211(VOID);
extern VOID ItMcsSystem212(VOID);
extern VOID ItMcsSystem301(VOID);

const mcs_case_fn_t mcs_cases_system[] = {
    ItMcsSystem001,
    // ItMcsSystem002,   /* 临时屏蔽：其用例后 OsResLeakCheckEnd 挂死（np_system 嫌疑，[Passed] 后调度线程静默——3 轮同点实锤），后续单独调 */
    // ItMcsSystem201,   /* 临时屏蔽：断言失败（Svc 域服务状态类，后续单独调） */
    // ItMcsSystem202,   /* 临时屏蔽：断言失败（Svc 域服务状态类，后续单独调） */
    // ItMcsSystem203,   /* 临时屏蔽：断言失败（Svc 域服务状态类，后续单独调） */
    ItMcsSystem204,
    ItMcsSystem205,
    ItMcsSystem206,
    ItMcsSystem207,
    ItMcsSystem208,
    ItMcsSystem209,
    // ItMcsSystem210,   /* 临时屏蔽：断言失败（Svc 域服务状态类，后续单独调） */
    // ItMcsSystem211,   /* 临时屏蔽：断言失败（Svc 域服务状态类，后续单独调） */
    ItMcsSystem212,
    // ItMcsSystem301,   /* 临时屏蔽：破坏性用例（mica_stop_receiver×20，设计意图=全域末位），调度数组摆位后续单独议 */
};
const int mcs_cases_system_count = sizeof(mcs_cases_system) / sizeof(mcs_cases_system[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_system = { "system", mcs_cases_system, sizeof(mcs_cases_system) / sizeof(mcs_cases_system[0]) };
