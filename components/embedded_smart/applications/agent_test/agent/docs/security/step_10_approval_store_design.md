# 第 10 步实施设计：持久化审批存储（Approval Store）

状态：已实施（2026-10-08，host 验证通过：29/29 CTest，含 128 项审批断言）；待提交
对应计划：[安全能力渐进式实施计划](security_incremental_implementation_plan.md#411-第-10-步持久化审批存储)
设计基线：2026-10-08，基于 main `05b9204`（已合入 progressive Kconfig 架构）
依赖：无（本步骤为纯新增模块，不依赖第 6/7 步；决策来源的简化说明见第 11 步）

## 0. 需求评审结论（本设计的输入）

2026-10-08 交互式审批需求评审确认：

1. 触发范围：所有 `readonly == 0` 的工具；配置排除清单默认排除 `memory_remember`。
2. 审批粒度：单次确认 + "本会话总是允许"（会话级免确认白名单）。
3. 持久化：完整持久化，服务重启后 pending 审批可恢复继续等待（对齐原计划验收标准）。
4. 决策类型：`approve` / `reject` / `always_allow`；不实现 `edit`（修改后批准）。
5. 客户端：Flask UI + 协议扩展（第 11 步）。

## 1. 本步骤目标

建立审批的独立数据层，暂不接入 Agent 对话循环：

1. 审批条目的创建、查询、状态转换、过期与防重放。
2. 会话级免确认白名单的授予、查询与失效。
3. 审批条目持久化：重启后 pending 状态可恢复；决策事件 append-only 记录。
4. 全部操作有界：内存队列、持久化文件、生命周期均受配额约束。
5. 与 Suspend Queue 一致的 fail-closed 匹配语义。

本步骤完成后，审批存储可以独立编译和测试，产品行为无任何变化。

## 2. 范围与非目标

### 2.1 本步骤包含

- `src/security/approval.c` 与 `include/litecrab/approval.h`。
- 审批条目与白名单条目的内存结构、状态机、互斥保护。
- 持久化文件格式、原子写、加载恢复、损坏 fail-closed。
- 过期清扫（惰性 + 队列操作时顺带清扫）。
- 独立单元测试 `tests/test_approval.c`。

### 2.2 本步骤不包含

- 不接入 Kernel 工具循环（第 11 步）。
- 不修改 Gateway 协议与 UI。
- 不实现 Principal / Policy Engine：条目绑定 `sessionId`，第 6/7 步完成后升级为 principalId 并由 Policy 决定 REQUIRE_APPROVAL。
- 不实现 `edit` 决策。
- 不为白名单提供持久化（见 4.4 安全权衡）。

## 3. 当前代码状态

1. `src/security/` 目录不存在，本步骤从零建立。
2. `src/kernel/skill.c:59-71` 的 Suspend Queue（固定 16 槽数组 + 互斥 + 顺序 ID）可整体平移为审批队列结构。
3. `src/session/session.c:312-379` 的持久化手法（临时文件 + fsync + 原子 rename + FNV 校验和 + 0600 + 配额）是本项目已验证的落盘模式，直接借用。
4. `src/kernel/skill.c:1101-1158` 的挂起匹配逻辑（同 Session 强制、未知引用 fail-closed、冲突检测）是审批匹配的语义模板。
5. Trace 事件形态参考 `skill.c:94-108` 的 `event=skill.run.state_changed`。

## 4. 设计决策

### 4.1 审批条目的身份与绑定

对齐[安全模块架构设计](security_module_architecture.md) §15.4：每条审批绑定唯一身份，防止跨调用、跨会话、跨参数重放。

```c
typedef struct {
    char interruptId[24];   /* "apr-%08lu"，单调递增，不复用 */
    char sessionId[64];     /* 产生审批的会话，匹配强制条件 */
    char toolCallId[80];    /* LLM 返回的 call id（透传） */
    char toolName[64];      /* 与 LlmToolCall.name 尺寸对齐 */
    char argsJson[2048];    /* LLM tool_call 的 input 原文（JSON object） */
    unsigned int argsHash;  /* argsJson 的 FNV-1a 摘要 */
    long long createdAtMs;  /* CLOCK_REALTIME 墙钟——条目跨重启存活，
                               单调钟重启归零不可用 */
    long long expiresAtMs;  /* createdAt + approval.timeout_ms */
    int state;              /* 见 4.3 */
    unsigned int revision;  /* 每次状态迁移 +1 */
} CrabApprovalEntry;
```

实现备注（2026-10-08）：
- `argsJson` 存 **LLM input 原文**而非重排序参数：第 11 步恢复执行时直接交给
  `LlmToolAdapterParseCall` 重建调用，完整复用现有"解析 + schema 校验"链。
- Decide/FindWaiting 的失败路径（过期/摘要不符）会回填 `entryOut` 快照，
  供调用方向用户解释拒绝原因。

约束：
- `interruptId` 全局单调，与 Suspend Queue 的 `int-%08lu` 风格一致，不与 skill 挂起 ID 冲突（前缀不同）。
- `sessionId` 为匹配的必要条件；跨会话消费一律拒绝。
- `argsJson` 上限 2048 字节，超限的调用直接按"参数过大无法审批"拒绝执行（fail-closed），不创建条目。

### 4.2 参数摘要与防偷换

- `argsHash` 为 `argsJson` 的 FNV-1a（与 Session 校验和同族，无新依赖）。
- 创建时计算一次；消费时对条目内 `argsJson` 重算比对——不一致说明持久化文件损坏或被篡改，按 fail-closed 拒绝并产生 anomaly Trace。
- v1 语义说明（诚实记录）：当前设计消费的是条目内存储的原始调用，模型无法在等待期间偷换参数；`argsHash` 的现值是完整性校验与防篡改，为第 11 步 `edit` 决策和第 7 步"恢复前重新决策"预留。此说明写入代码注释，防止后人误读其作用。

### 4.3 状态机

```
WAITING ──approve──────► APPROVED ──执行完成──► CONSUMED
   │                                                  
   ├──reject──────► DENIED（终态）                    
   ├──always_allow─► APPROVED（同时写白名单）→ CONSUMED
   └──过期清扫─────► EXPIRED（终态）
```

- 状态只沿图示方向迁移；`revision` 每次迁移 +1 并产生
  `event=approval.state_changed` Trace（字段：interruptId、from、to、revision、reason）。
- 决策事件 append-only：approve/reject/expire 都是不可逆的记录，不存在"撤销批准"。
- `CONSUMED` 表示"批准已被用于执行"，二次消费（同一 interruptId 再次 decision）返回
  `CRAB_APPROVAL_ALREADY_CONSUMED`，不执行。

### 4.4 会话级免确认白名单

```c
typedef struct {
    char sessionId[64];
    char toolName[32];
    long long grantedAtMs;
} CrabSessionAllowEntry;
```

- 容量 16 条/进程（满时新授予失败，按单次确认继续，不驱逐——防抖动）。
- 查询键：`(sessionId, toolName)`。
- **不持久化**：重启后清空，用户需重新授权。理由：白名单是对"本会话内该工具免确认"的授权，跨重启保留会放大 prompt 注入的时间窗口；pending 审批持久化（用户要求）与白名单不持久化（安全决策）并不矛盾——前者是"没答完的问题"，后者是"已给的特权"。
- 会话结束（Session 销毁路径可感知时）主动清理；不可感知时由容量上限与重启兜底。

### 4.5 持久化设计

布局：`<workspace>/.crab/approvals/pending.json`

- 单文件 JSON 数组，只保存 `state == WAITING` 的条目（≤16 条）。
- 写入时机：创建新条目、状态迁移离开 WAITING（approve/reject/expire）时全量重写；条目少（≤16）+ 单文件，全量重写成本可忽略。
- 写入手法：与 Session 一致——临时文件 + `fsync` + `rename` + 目录 fsync，文件权限 0600，目录 0700。
- 启动加载：解析数组，逐条校验 `argsHash` 自洽与字段合法性；任何条目损坏即整文件作废（fail-closed，全部 pending 视为不存在，产生 ERROR Trace），服务可启动但旧 pending 全部作废——宁可让用户重新发起，不可让损坏数据参与决策。
- 已离开 WAITING 的状态不落盘：终态只存在于内存与 Trace。pending.json 只回答一个问题："重启后还有哪些在等"。
- 配额：文件上限 64 KiB（16 条 × ~3 KiB 余量），超限拒新。

### 4.6 过期治理

- 惰性检查：任何 lookup / consume 前先做过期判定，过期即迁移 EXPIRED 并落盘。
- 顺带清扫：每次队列互斥锁临界区内扫描全部条目（≤16，成本恒定），迁移到期条目。
- 不引入后台线程、不加定时器：过期条目不影响正确性（consume 时必拒），清扫只影响 pending.json 的整洁度。

### 4.7 容量与降级

| 场景 | 行为 |
|---|---|
| pending 满 16 且无过期条目 | 新审批创建失败；第 11 步将该结果转换为工具级"审批队列已满，操作未执行"的合成结果（fail-closed，不是无限排队） |
| argsJson 超 2048 字节 | 不创建条目，按拒绝处理 |
| pending.json 损坏 | 全部作废，服务正常启动 |
| 磁盘写失败 | 创建审批失败 → 拒绝执行（内存与磁盘不一致时以"无法持久化就不放行"为准） |

### 4.8 fail-closed 汇总

未知 interruptId / 会话不匹配 / 状态非 WAITING / 已 CONSUMED / 已过期 / argsHash 不自洽 / 存储损坏 / 容量满 / 磁盘失败——**一律拒绝执行**，返回稳定错误码并记 Trace。

## 5. API 与数据结构

文件：`include/litecrab/approval.h`

```c
#define CRAB_APPROVAL_MAX_PENDING 16
#define CRAB_APPROVAL_MAX_SESSION_ALLOW 16
#define CRAB_APPROVAL_ARGS_LIMIT 2048

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
    CRAB_APPROVAL_ERR_NOT_FOUND = -2,       /* 未知 id 或会话不匹配 */
    CRAB_APPROVAL_ERR_STATE = -3,           /* 非 WAITING（已决策/已消费） */
    CRAB_APPROVAL_ERR_EXPIRED = -4,
    CRAB_APPROVAL_ERR_HASH = -5,            /* 参数摘要不自洽 */
    CRAB_APPROVAL_ERR_CAPACITY = -6,
    CRAB_APPROVAL_ERR_STORAGE = -7,         /* 磁盘失败或加载作废 */
    CRAB_APPROVAL_ERR_ARGS_TOO_LARGE = -8
} CrabApprovalResult;

int  CrabApprovalInit(const char* workspaceDir, long long timeoutMs);
void CrabApprovalDestroy(void);
/* 初始化时加载 pending.json；加载失败返回 OK 但全部 pending 作废并记日志 */

int  CrabApprovalCreate(const char* sessionId, const char* toolCallId,
                        const char* toolName, const char* argsJson,
                        char* interruptIdOut, size_t interruptIdSize);
/* 成功返回 OK 并落盘；失败返回对应错误码，不产生条目 */

int  CrabApprovalDecide(const char* sessionId, const char* interruptId,
                        CrabApprovalDecision decision,
                        CrabApprovalEntry* entryOut /* 可为 NULL */);
/* WAITING→APPROVED/DENIED；ALWAYS_ALLOW 同时写白名单；内部完成过期检查 */

int  CrabApprovalConsume(const char* sessionId, const char* interruptId,
                         CrabApprovalEntry* entryOut);
/* 仅 APPROVED→CONSUMED；供第 11 步执行前调用，返回条目快照 */

int  CrabApprovalFindWaiting(const char* sessionId, const char* interruptId,
                             CrabApprovalEntry* entryOut);
/* 只读查询，不改状态 */

void CrabApprovalSweepExpired(void);   /* 临界区内顺带清扫 + 落盘 */

int  CrabSessionAllowCheck(const char* sessionId, const char* toolName);
int  CrabSessionAllowGrant(const char* sessionId, const char* toolName);
void CrabSessionAllowClearSession(const char* sessionId);
```

实现要点：
- 进程级单例（与 Suspend Queue 同风格），`pthread_mutex_t` 全程保护。
- `CrabApprovalDecide` 的 `ALWAYS_ALLOW` 在同一临界区内完成审批迁移 + 白名单写入，保证原子性。
- 所有状态迁移调用 `AgentTraceEmit`（`approval.state_changed`）；失败路径记 anomaly。

## 6. 文件级修改说明

| 文件 | 类型 | 修改目的 |
|---|---|---|
| `include/litecrab/approval.h` | 新增 | 公共类型与 API |
| `src/security/approval.c` | 新增 | 实现（约 500 行） |
| `src/CMakeLists.txt` 或主 `CMakeLists.txt`（按 Kconfig 架构的 core_sources 注册方式） | 修改 | approval.c 进入 core 源集；`build/capabilities.yml` 的 core_sources 同步登记 |
| `tests/test_approval.c` | 新增 | 单元测试（见第 8 节） |
| `tests/CMakeLists.txt`（测试注册处） | 修改 | 注册 litecrab_approval_tests |

不修改：kernel / gateway / config / UI（全部属于第 11 步）。

## 7. 数据流与状态迁移

```
创建（第 11 步的挂起点调用）
  │ 校验参数与容量 → 计算 argsHash → 入队(WAITING) → 落盘 pending.json
  ▼
等待用户决策（可能跨重启；重启时从 pending.json 恢复 WAITING 条目）
  │
  ├─ approve        → APPROVED → (第 11 步执行工具) → Consume → CONSUMED
  ├─ always_allow   → APPROVED + 白名单[(session,tool)] → Consume → CONSUMED
  ├─ reject         → DENIED（终态）
  └─ 超时/清扫      → EXPIRED（终态）
```

## 8. 测试矩阵（失败测试先行）

| # | 场景 | 预期 | 副作用断言 |
|---|---|---|---|
| 1 | 创建→decide(approve)→consume | 两次迁移 OK，revision 递增 | pending.json 相应更新 |
| 2 | 重复 consume 同一 id | `ERR_STATE`，不返回条目 | 状态仍 CONSUMED |
| 3 | decide 已 DENIED 条目 | `ERR_STATE` | 无状态变化 |
| 4 | 跨会话 decide/consume | `ERR_NOT_FOUND` | 状态不变 |
| 5 | 未知 interruptId | `ERR_NOT_FOUND` | 无 |
| 6 | 伪时钟越过 expiresAt 后 decide | `ERR_EXPIRED`，状态迁移 EXPIRED | 落盘 |
| 7 | pending.json 手工篡改 argsJson 后重启加载 | 该条目（或整文件）作废 | consume 返回 `ERR_HASH` 或 `ERR_NOT_FOUND` |
| 8 | 重启恢复：创建 3 条→销毁→重新 Init | 3 条 WAITING 可查询、可决策 | — |
| 9 | pending.json 写入垃圾字节→Init | 加载作废，服务可继续 | 全部旧 id `ERR_NOT_FOUND` |
| 10 | 磁盘不可写目录 Init + Create | `ERR_STORAGE`，无条目 | — |
| 11 | 容量：创建 17 条 | 第 17 条 `ERR_CAPACITY` | — |
| 12 | argsJson 2500 字节 | `ERR_ARGS_TOO_LARGE` | 无条目 |
| 13 | always_allow：grant 后 Check 命中 | OK | 白名单大小 +1 |
| 14 | 白名单容量 17 条 | 第 17 条 grant 失败（单次路径不受影响） | — |
| 15 | ClearSession 后 Check | 未命中 | — |
| 16 | always_allow 不持久化：grant→Destroy→Init→Check | 未命中 | — |
| 17 | 并发：多线程同时 decide 同一条目 | 恰有一个成功，其余 `ERR_STATE` | revision 单调 |

## 9. 兼容性影响

- 纯新增模块：未接线前产品行为零变化。
- 不引入新配置（timeout 由 Init 参数传入，第 11 步才接入 config）。
- 不改变任何现有错误码与 API。

## 10. 实施顺序

1. 写 `tests/test_approval.c` 骨架（用例 1-6，此时全红）。
2. 实现 approval.h 类型 + 内存队列 + 状态机（不落盘），跑绿用例 1-6。
3. 实现持久化（写/加载/fail-closed），补用例 7-10。
4. 实现容量/参数上限/白名单，补用例 11-16。
5. 补并发用例 17。
6. CMake/capabilities.yml 注册 + 全量回归。
7. 验收通过后更新总计划第 10 步状态。

## 11. 验证命令与通过标准

```sh
cmake -S . -B build-approval -DLITECRAB_BUILD_TESTS=ON
cmake --build build-approval -j
ctest --test-dir build-approval -R litecrab_approval --output-on-failure
ctest --test-dir build-approval --output-on-failure
```

通过标准：
1. `litecrab_approval_tests` 17 组用例全部通过。
2. 全量回归通过（含 28 项既有基线）。
3. 重启恢复、过期、重放、篡改四类 fail-closed 场景均有断言。
4. 生产代码变更仅落在第 6 节清单内。

## 12. 回退边界

本步骤是纯新增文件 + 两处构建注册，可整体单提交回退。回退时无需迁移数据（pending.json 留在 workspace 不影响任何现有功能）。

## 13. 评审重点

1. 是否接受白名单不持久化（与"审批完整持久化"的不对称是刻意安全决策，见 4.4）。
2. 是否接受 pending.json 损坏时全部作废（而非跳过损坏条目保留其余）。
3. 是否接受 `argsHash` 在 v1 的定位是完整性校验而非防偷换（见 4.2 诚实说明）。
4. `CRAB_APPROVAL_*` 错误码是否需要进入统一 reason code 体系（第 6 步 security.h 建立前先独立）。
5. 核心源集注册位置：approval 属于 core（所有能力组合都需要审批约束），是否认可。
