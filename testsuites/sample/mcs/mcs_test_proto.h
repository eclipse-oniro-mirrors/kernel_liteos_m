/*
 * mcs_test_proto.h — 从核测试调度服务协议（2026-11-09 新架构）
 *
 * 独立 rpmsg 服务（与 rpc_service 解耦）：端点名 rpmsg-mcs-test，
 * 注册进 mica 服务框架（mica_create_all_services 拉起）。
 * host 侧通过本端点下发用例调度命令；从核执行后应答。
 * 用例执行结果的逐例 Passed/Failed 仍走串口 iCunit 报告（swatch 采集），
 * 本协议只承载调度控制与轮次级汇总。
 */
#ifndef MCS_TEST_PROTO_H
#define MCS_TEST_PROTO_H

#include <stdint.h>


/* ── 模块注册表类型（原 mcs_cases.h 并入，2026-11-10）── */
typedef void (*mcs_case_fn_t)(void);

typedef struct {
    const char *name;       /* 模块名：tty/umt/rpc/np/pfs/system/combo */
    const mcs_case_fn_t *cases;
    int count;
} mcs_module_t;

extern const mcs_module_t *const g_mcs_modules[];
extern const int g_mcs_module_count;
const mcs_module_t *mcs_find_module(const char *name);

#ifdef __cplusplus
extern "C" {
#endif

#define MCS_TEST_EPT_NAME "rpmsg-mcs-test"
#define MCS_TEST_MAGIC    0x4D43 /* "MC" */

/* 命令（host -> slave）；应答 = cmd | MCS_TEST_RSP */
enum {
    MCS_TEST_CMD_PING = 1,   /* 探活：应答 status=0 */
    MCS_TEST_CMD_RUN_CASE,   /* module + index(1-based)：跑单用例 */
    MCS_TEST_CMD_RUN_MODULE, /* module：跑整个模块 */
    MCS_TEST_CMD_RUN_ALL,    /* 跑全部已注册模块 */
    MCS_TEST_CMD_STOP,       /* 停止当前执行（当前用例跑完后停） */
    MCS_TEST_CMD_STAT,       /* 查询状态 */
    MCS_TEST_RSP = 0x80,
};

/* 调度器状态（STAT 应答的 status 值） */
enum {
    MCS_TEST_ST_IDLE = 0,
    MCS_TEST_ST_RUNNING,
    MCS_TEST_ST_STOPPING,
};

#pragma pack(push, 1)
struct mcs_test_msg {
    uint16_t magic;          /* MCS_TEST_MAGIC */
    uint8_t cmd;             /* MCS_TEST_CMD_* 或 | MCS_TEST_RSP */
    uint8_t ver;             /* 协议版本，当前 1 */
    uint32_t seq;            /* 命令序号，应答原样回带 */
    int32_t status;          /* 命令:0；应答:0 成功 / -errno */
    uint32_t index;          /* RUN_CASE: 1-based 用例序号 */
    uint32_t total;          /* 应答 DONE: 本轮用例总数 */
    uint32_t failed;         /* 应答 DONE: 本轮失败数（iCunit 计数增量） */
    char module[12];         /* 模块名（RUN_CASE/RUN_MODULE），RUN_ALL 为空 */
};
#pragma pack(pop)

#define MCS_TEST_MSG_SIZE sizeof(struct mcs_test_msg)

/* 应答子类型（cmd 字段）：ACK=受理，DONE=本轮执行完成 */
#define MCS_TEST_RSP_ACK(cmd)  (((cmd) & 0x7f) | MCS_TEST_RSP)
/* DONE 仅在本轮全部用例执行完成后发送一次 */

/* 服务接口（从核侧实现，mica_service.c 挂载） */
int mcs_test_init_service(void);      /* 起服务线程（端点+调度） */
int mcs_test_service_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* MCS_TEST_PROTO_H */
