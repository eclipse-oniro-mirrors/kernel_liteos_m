#ifndef LITECRAB_APPROVAL_H
#define LITECRAB_APPROVAL_H

#include <stddef.h>

/*
 * 审批存储（安全计划第 10 步）。
 *
 * 进程级单例：有界审批队列 + 会话级免确认白名单 + pending.json 持久化。
 * 全部操作 fail-closed：未知编号、会话不匹配、非等待状态、已消费、
 * 已过期、参数摘要不自洽、存储损坏、容量满、磁盘失败——一律拒绝。
 *
 * 状态机：
 *   WAITING --approve--> APPROVED --执行完成--> CONSUMED
 *     |--reject--> DENIED（终态）
 *     |--always_allow--> APPROVED（同时写会话白名单）--> CONSUMED
 *     +--过期清扫--> EXPIRED（终态）
 */

#define CRAB_APPROVAL_MAX_PENDING 16
#define CRAB_APPROVAL_MAX_SESSION_ALLOW 16
#define CRAB_APPROVAL_ARGS_LIMIT 2048

#define CRAB_APPROVAL_ID_SIZE 24
#define CRAB_APPROVAL_SESSION_ID_SIZE 64
#define CRAB_APPROVAL_TOOL_CALL_ID_SIZE 80
#define CRAB_APPROVAL_TOOL_NAME_SIZE 64

typedef enum {
    CRAB_APPROVAL_WAITING = 0,
    CRAB_APPROVAL_APPROVED = 1,
    CRAB_APPROVAL_DENIED = 2,
    CRAB_APPROVAL_EXPIRED = 3,
    CRAB_APPROVAL_CONSUMED = 4
} CrabApprovalState;

typedef enum {
    CRAB_APPROVAL_DECISION_APPROVE = 0,
    CRAB_APPROVAL_DECISION_REJECT = 1,
    CRAB_APPROVAL_DECISION_ALWAYS_ALLOW = 2
} CrabApprovalDecision;

typedef enum {
    CRAB_APPROVAL_OK = 0,
    CRAB_APPROVAL_ERR_INVALID_ARG = -1,
    CRAB_APPROVAL_ERR_NOT_FOUND = -2,   /* 未知编号或会话不匹配 */
    CRAB_APPROVAL_ERR_STATE = -3,       /* 状态非本次操作所需（已决策/已消费等） */
    CRAB_APPROVAL_ERR_EXPIRED = -4,
    CRAB_APPROVAL_ERR_HASH = -5,        /* 参数摘要不自洽（存储损坏或篡改） */
    CRAB_APPROVAL_ERR_CAPACITY = -6,
    CRAB_APPROVAL_ERR_STORAGE = -7,     /* 磁盘失败或存储不可用 */
    CRAB_APPROVAL_ERR_ARGS_TOO_LARGE = -8
} CrabApprovalResult;

/*
 * argsJson 约定：存储 LLM tool_call 的 input 原文（JSON object 字符串）。
 * 恢复执行时直接交给 LlmToolAdapterParseCall 重建，复用现有解析与
 * schema 校验链；argsHash 为该原文的 FNV-1a 摘要。
 */
typedef struct {
    char interruptId[CRAB_APPROVAL_ID_SIZE];          /* "apr-%08lu"，全局单调 */
    char sessionId[CRAB_APPROVAL_SESSION_ID_SIZE];    /* 匹配的必要条件 */
    char toolCallId[CRAB_APPROVAL_TOOL_CALL_ID_SIZE]; /* LLM 返回的 call id */
    char toolName[CRAB_APPROVAL_TOOL_NAME_SIZE];
    char argsJson[CRAB_APPROVAL_ARGS_LIMIT];
    unsigned int argsHash;
    long long createdAtMs;   /* CLOCK_REALTIME，跨重启可比 */
    long long expiresAtMs;
    int state;
    unsigned int revision;
} CrabApprovalEntry;

/* 初始化：加载 <workspace>/.crab/approvals/pending.json；任何损坏按整文件作废
 * （fail-closed）并记日志，服务可继续启动。目录不可用时返回 OK 但后续
 * Create 按存储失败拒绝。timeoutMs 为审批有效期（毫秒），必须大于 0。 */
int CrabApprovalInit(const char* workspaceDir, long long timeoutMs);
void CrabApprovalDestroy(void);

/* 创建 WAITING 审批并落盘；成功时 interruptIdOut 收到新编号。 */
int CrabApprovalCreate(const char* sessionId,
                       const char* toolCallId,
                       const char* toolName,
                       const char* argsJson,
                       char* interruptIdOut,
                       size_t interruptIdSize);

/* 决策：WAITING -> APPROVED / DENIED；ALWAYS_ALLOW 同时写会话白名单
 * （白名单满时降级为单次批准并记警告）。失败返回稳定错误码，状态不变。 */
int CrabApprovalDecide(const char* sessionId,
                       const char* interruptId,
                       CrabApprovalDecision decision,
                       CrabApprovalEntry* entryOut);

/* 消费：仅 APPROVED -> CONSUMED；执行前调用，返回条目快照供重建调用。 */
int CrabApprovalConsume(const char* sessionId,
                        const char* interruptId,
                        CrabApprovalEntry* entryOut);

/* 只读查询 WAITING 条目，不改变状态。 */
int CrabApprovalFindWaiting(const char* sessionId,
                            const char* interruptId,
                            CrabApprovalEntry* entryOut);

/* 按会话查询最早的 WAITING 条目（Gateway 响应信封用它装配 interrupt 字段）；
 * 内部顺带清扫过期条目。 */
int CrabApprovalFindWaitingBySession(const char* sessionId,
                                     CrabApprovalEntry* entryOut);

/* 审批门是否可用：未初始化（如嵌入式 API 路径）视为门关闭。 */
int CrabApprovalIsReady(void);

/* 清扫过期 WAITING 条目（EXPIRED）并落盘；可随时安全调用。 */
void CrabApprovalSweepExpired(void);

/* 会话级免确认白名单（仅内存，重启即失效——刻意安全决策，见设计文档 4.4）。 */
int CrabSessionAllowCheck(const char* sessionId, const char* toolName);
int CrabSessionAllowGrant(const char* sessionId, const char* toolName);
void CrabSessionAllowClearSession(const char* sessionId);

#endif
