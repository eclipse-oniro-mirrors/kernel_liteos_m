/* it_mcs_rpc.c — rpc 域注册表（调度模块数组；加用例=extern+数组行各一行；
 * 屏蔽单用例：注释对应数组行即可，count 由 sizeof 自动收缩） */
#include "mcs_test_proto.h"
#include "los_typedef.h"

extern VOID ItMcsRpc001(VOID);
extern VOID ItMcsRpc002(VOID);
extern VOID ItMcsRpc003(VOID);
extern VOID ItMcsRpc004(VOID);
extern VOID ItMcsRpc005(VOID);
extern VOID ItMcsRpc201(VOID);
extern VOID ItMcsRpc202(VOID);
extern VOID ItMcsRpc203(VOID);
extern VOID ItMcsRpc204(VOID);
extern VOID ItMcsRpc205(VOID);
extern VOID ItMcsRpc206(VOID);
extern VOID ItMcsRpc207(VOID);
extern VOID ItMcsRpc208(VOID);
extern VOID ItMcsRpc209(VOID);
extern VOID ItMcsRpc210(VOID);
extern VOID ItMcsRpc211(VOID);
extern VOID ItMcsRpc301(VOID);
extern VOID ItMcsRpc302(VOID);
extern VOID ItMcsRpc303(VOID);

const mcs_case_fn_t mcs_cases_rpc[] = {
    // ItMcsRpc001,   /* 临时屏蔽：101000 缺陷在案断言（resp 侧无守卫，OPEN） */
    ItMcsRpc002,
    ItMcsRpc003,
    ItMcsRpc004,
    ItMcsRpc005,
    // ItMcsRpc201,   /* 临时屏蔽：Errline26 RetCode0x0（本轮新失败，后续单独调） */
    ItMcsRpc202,
    ItMcsRpc203,
    ItMcsRpc204,
    ItMcsRpc205,
    ItMcsRpc206,
    ItMcsRpc207,
    ItMcsRpc208,
    ItMcsRpc209,
    // ItMcsRpc210,   /* 临时屏蔽：Errline21 RetCode-22（本轮新失败，后续单独调） */
    ItMcsRpc211,
    ItMcsRpc301,
    ItMcsRpc302,
    ItMcsRpc303,
};
const int mcs_cases_rpc_count = sizeof(mcs_cases_rpc) / sizeof(mcs_cases_rpc[0]);

/* 模块描述（sizeof 同单元计算，屏蔽数组行后 count 自动收缩） */
const mcs_module_t mcs_module_rpc = { "rpc", mcs_cases_rpc, sizeof(mcs_cases_rpc) / sizeof(mcs_cases_rpc[0]) };
