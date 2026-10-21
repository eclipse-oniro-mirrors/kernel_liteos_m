# 第 11 步实施设计：审批接入 Agent 执行循环

状态：已实施（2026-10-08，host 验证通过：30/30 CTest，含 31 项审批 E2E 断言）；待提交
对应计划：[安全能力渐进式实施计划](security_incremental_implementation_plan.md#412-第-11-步审批接入-agent-执行循环)
设计基线：2026-10-08，基于 main `05b9204`
需求来源：交互式审批评审结论（2026-10-08）——对话过程中弹窗确认，用户批准后继续执行，否则不执行

实现备注（2026-10-08）：
- `approval_gate` 在设计 §4.1 基础上增加一条：**默认禁用的工具不触发审批**
  （禁用工具本来就会被 Runtime 第④道闸拒绝，弹窗毫无意义且会污染体验）；
  该修正由既有 `disabled-tool` 测试场景在联调中暴露。
- Gateway 信封通过 `CrabApprovalFindWaitingBySession` 按会话装配 `interrupt`
  （内部惰性清扫过期条目），引导响应因此天然携带弹窗数据，重连 UI 可重新渲染。
- UI 传输层保持既有契约：`send_and_recv` 返回值与 payload 不变，新增
  `send_request` 返回完整信封（`ui_session_transport` 测试无需修改）。
- `agent_matrix` 的 exec 场景按 §9.1 在测试配置中显式关闭审批。

## 1. 本步骤目标

把第 10 步的审批存储接入 Agent 工具循环，实现端到端的交互式审批：

1. 非只读工具调用在执行前挂起，向客户端返回结构化审批请求。
2. 客户端弹窗，用户选择批准 / 本会话总是允许 / 拒绝。
3. 新请求携带决策恢复执行；未决期间上下文不丢失、重启不丢失。
4. 自然语言永不触发批准（原计划第 11 步明文红线）。
5. 默认配置下行为符合 secure-by-default（见 9.1 评审项）。

## 2. 范围与非目标

### 2.1 本步骤包含

- Kernel 工具循环的挂起与恢复（非阻塞两段式）。
- 触发判定函数（`readonly == 0` + 排除清单 + 白名单 + 请求来源）。
- Gateway 协议扩展：响应 `interrupt` 字段、请求 `decision` 字段。
- 配置块 `approval`（enabled / timeout_ms / exclude_tools）。
- Flask UI 审批卡片。
- 端到端测试。

### 2.2 本步骤不包含

- **告警（Alarm）来源的请求不接入审批**：无人值守的告警驱动修复是现有核心场景，弹窗无人应答会造成修复停滞。告警路径写操作的幂等治理仍按计划由后续 Mutation Ledger 承担。此为刻意的产品范围决策（见 13 评审重点 1）。
- **嵌入式 Agent API 路径不接线**（robot_hi3516 静态库形态）：v1 只接线 standalone server。嵌入式路径的非只读工具行为保持现状。
- 不实现 `edit`（修改后批准）决策。
- 不实现 Principal / Policy Engine：触发判定收敛为单一函数，第 7 步完成后整体替换该函数，不再改 Kernel。
- 不做 Transfer Station 客户端适配。
- 不做审批事件的对外推送（客户端重连后可通过下述"引导响应"重新拿到待审批信息）。

## 3. 当前代码状态（main `05b9204`）

1. 工具循环（`src/kernel/kernel.c` `run_round`）：`LlmToolAdapterParseCall` 在 :622，`CrabRuntimeCallTool` 在 :631——挂起判定插入在两者之间（解析成功、拿到类型化参数之后；执行之前）。
2. 引用字段全链路已通：`replyToInterruptId` 从 `tcp_line.c` 解析，经 `IngressOptions`/`LiteMsg` 传入 Kernel（当前唯一消费方是 SkillRouterRun）。
3. 响应信封：`responseFormat=json` 时 Gateway 返回 `{"responseFormat":"json","content":"..."}` 单行——`interrupt` 作为新增可选字段向后兼容。
4. 会话持久化（`session.c`）：原子写 + 0600 + 配额，挂起时保存上下文直接复用。
5. UI：`others/UI/app.py` Flask 反代 + `templates/index.html` 单页；`BoardSession` 每请求新建 TCP 连接，透传 JSON 字段即可。
6. `memory_remember` 的 `readonly == 0`（`src/runtime/tools/memory_tools.c:24-27`），按评审结论进默认排除清单。

## 4. 设计决策

### 4.1 触发判定：单一决策函数

```c
/* kernel.c 内静态函数——第 7 步 Policy Engine 的替换点 */
static int ApprovalRequired(const char* sessionId, const CrabToolSpec* tool,
                            int originIsGatewayChat);
/* 返回真当且仅当：
   originIsGatewayChat                    （告警/嵌入式路径不触发，见 2.2）
   && approval.enabled
   && tool->readonly == 0
   && toolName 不在 approval.exclude_tools
   && !CrabSessionAllowCheck(sessionId, toolName) */
```

所有触发条件集中在这一个函数。第 7 步建成后，函数体替换为 Policy 决策（ALLOW 直接放行 / REQUIRE_APPROVAL 进入本流程），Kernel 其余部分不动。

### 4.2 非阻塞两段式挂起

Agent 是单线程串行消费者，阻塞等待人工响应会冻结整个服务。挂起采用与 Skill Suspend 同构的两段式：

- 第一段（原请求）：判定需审批 → 写审批条目 → 保存上下文 → **请求正常结束**，响应携带 `interrupt` 结构。
- 第二段（新请求）：客户端发送 `replyToInterruptId + decision` → 匹配校验 → 恢复执行。

Agent 线程在两段之间完全释放，可服务其他会话。

### 4.3 挂起点的上下文保存（占位符方案）

结构性缺口：`run_round` 的消息缓冲只在函数尾部落盘，工具循环中途返回会丢失本轮 LLM 的 tool_calls 提案。解决方案——**占位符 + 原地替换**：

1. 挂起时（第 i 个调用需审批，前 i-1 个已执行）：把本轮完整 tool exchange 追加进消息缓冲——assistant 的 `tool_calls` 数组（全部）+ 已执行调用的真实结果 + 第 i 个调用使用**占位结果**：
   ```json
   {
     "role": "tool",
     "tool_call_id": "call_i",
     "content": "{\"status\":\"pending_approval\",\"interruptId\":\"apr-000001\",\"expiresAtMs\":1760000000000}"
   }
   ```
2. 立即 `AgentSessionStateSave(messages)`——重启后上下文完整。
3. 恢复时：定位末尾占位符（按 interruptId 匹配），用真实结果（批准→执行结果 / 拒绝→`{"status":"rejected_by_operator"}` / 过期→`{"status":"approval_expired"}`）**原地替换**，再继续处理本轮剩余调用与后续 LLM 迭代。
4. 占位符从未被任何 LLM 调用消费（见 4.5 的封锁规则保证），替换对模型不可见，不产生历史不一致。

多个非只读调用在同一轮（最多 8 个）：**顺序弹窗**——批准第 i 个后继续执行，遇到第 i+1 个再挂起、再弹窗，机制完全复用。每条审批独立持久化。

### 4.4 结构化中断响应（协议扩展）

`responseFormat=json` 信封新增可选 `interrupt` 字段（老客户端无视之，只读 `content`，不破坏兼容）：

```json
{
  "responseFormat": "json",
  "content": "好的，我来创建这个文件。（模型自然语言，无附带文本时兜底为"这个操作需要你的确认。"）",
  "interrupt": {
    "interruptId": "apr-00000001",
    "toolName": "exec_program",
    "args": {
      "command": "main",
      "args": ["set_freq_band", "3"]
    },
    "expiresAtMs": 1760000000000,
    "sessionId": "station-1"
  }
}
```

用户视角文案约定（2026-10-12 UX 修订）：对话流只出现模型自然语言或中性兜底句；
审批编号、内部工具名等细节仅存在于 interrupt 信封（UI 不展示编号）与 Trace。

请求侧新增 `decision` 字段（枚举字符串 `approve` / `reject` / `always_allow`），必须与 `replyToInterruptId` 成对出现。`decision` 需要从 Gateway 解析并经 Hub 透传（`IngressOptions`/`LiteMsg` 各加一个字段）。

### 4.5 未决期间的同会话封锁

会话存在 WAITING 审批时，该会话的**普通 chat 消息不进入 LLM 循环**，直接返回引导响应（重复携带 `interrupt` 信封，重连的 UI 可据此重新渲染弹窗）：

```
"有一个操作正在等待你的确认，请在提示卡片中批准或拒绝（长时间未确认会自动取消）。"
```

（UX 修订 2026-10-12：引导文案不暴露审批编号与内部工具名。）

理由：
1. 保证占位符永远处于消息末尾、从未被后续 LLM 调用消费——4.3 原地替换的正确性前提；
2. 产品语义清晰：一个危险动作等确认时，会话焦点应当是确认本身；
3. 避免大批复杂边界（审批与后续对话交错时的历史改写问题）。

封锁期间**其他会话不受影响**（两段式释放 Agent 线程的直接收益）。
过期解锁：下一条消息到达时惰性检测到 EXPIRED → 占位符替换为 `approval_expired` → 该消息正常进入 LLM 循环。

### 4.6 恢复路径与匹配优先级

`agent_main` 的处理顺序调整为：

```
1. 决策请求（replyToInterruptId + decision 同时存在）
   → CrabApprovalDecide（第 10 步 fail-closed 五连校验）
   → 成功：按 4.3 恢复执行（approve/always_allow 还需 Consume 后执行）
   → 失败：返回稳定错误 + 引导文本，不执行任何工具
2. 会话有 WAITING 审批的普通消息 → 引导响应（4.5）
3. 其余请求 → 现有 SkillRouterRun 路径不变
```

决策匹配**先于** Skill Router：审批恢复不经过技能路由，避免恢复消息被路由到 base/skill 分支造成二次挂起或误激活。

### 4.7 决策消费与工具结果注入

- `approve`：`CrabApprovalConsume` 取回条目 → 从 `argsJson` 重建 `CrabToolCall` → `CrabRuntimeCallTool` 执行 → 真实结果替换占位符。
- `always_allow`：同上，且 `CrabApprovalDecide` 内部已写白名单——本会话后续同工具调用在 4.1 判定时直接放行（Trace 记 `approval.session_allowed`）。
- `reject`：不执行；占位符替换为 `{"status":"rejected_by_operator"}`——**模型必须能感知拒绝并继续**（向用户解释原因、提出替代方案），而不是报错中断。
- 执行结果替换后，`run_round` 从第 i+1 个调用继续；全部调用解决后进入下一轮 LLM 迭代直至最终回答。

### 4.8 自然语言红线

`decision` 是唯一放行途径。Router 的自然语言恢复路径（"继续/resume"、explicit_resume_intent）在存在 WAITING 审批时**不适用**（被 4.5 封锁规则先行拦截）。用户回复"是的，执行吧"只会得到引导响应。测试矩阵含专项用例。

### 4.9 超时与 deadline 语义

- 审批有效期 `approval.timeout_ms`（默认 600000，10 分钟）来自**配置**，与请求 deadline 解耦——原请求在第一段就已正常结束，不占用请求 deadline，也不占用 Gateway worker。
- 挂起不消耗迭代/工具预算；恢复请求重置迭代计数（与现有"每请求预算"语义一致）。
- 过期由第 10 步的惰性 + 顺带清扫处理；过期后的解锁由 4.5 惰性路径完成。

### 4.10 配置契约

`base_config.json` 新增：

```json
"approval": {
  "enabled": true,
  "timeout_ms": 600000,
  "exclude_tools": ["memory_remember"]
}
```

- `enabled` 默认 **true**（secure-by-default，与第 2 步 loopback 默认同一原则；行为变化即本步骤目的，非回归——见 9.1）。
- `timeout_ms` 范围校验 [10000, 3600000]，非法值拒绝启动（fail-closed，沿用第 2 步配置纪律）。
- `exclude_tools` 严格字符串数组；未知工具名产生配置警告但不拒绝启动（容忍未来工具增删）。
- 不提供环境变量 / 命令行覆盖（与第 2 步风险开关同纪律：可审查的书面记录）。

## 5. 协议修改汇总

| 方向 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 响应 | `interrupt` | object（可选） | 见 4.4；仅在 `responseFormat=json` 信封中出现 |
| 请求 | `decision` | string（可选） | `approve`/`reject`/`always_allow`；必须与 `replyToInterruptId` 成对 |
| 请求 | `replyToInterruptId` | string（可选） | 复用现有字段，语义扩展为审批引用（与 Skill 挂起 ID 以前缀区分：`apr-` vs `int-`） |

非法组合的处理（fail-closed）：`decision` 无 `replyToInterruptId` → `ERROR: decision requires replyToInterruptId`；`replyToInterruptId` 无 `decision` 且命中审批 → 引导响应提示需要 decision。

## 6. 文件级修改说明

| 文件 | 类型 | 修改目的 |
|---|---|---|
| `src/kernel/kernel.c` | 生产 | 触发判定、挂起（占位符+保存）、恢复（决策匹配优先）、封锁规则、结果注入 |
| `src/kernel/kernel.c`（`run_round`） | 生产 | 支持从"挂起恢复点"继续本轮剩余调用（新增恢复入口参数） |
| `include/litecrab/hub.h` + `src/hub/hub.c` | 生产 | `IngressOptions`/`LiteMsg` 增加 `decision` 字段透传 |
| `src/gateway/tcp_line.c` | 生产 | 解析 `decision`；`responseFormat=json` 信封写 `interrupt` |
| `src/config/config.c` + `include/litecrab/config.h` | 生产 | approval 配置块（默认值、严格解析、范围校验） |
| `src/app/standalone_app.c` | 生产 | 启动时 `CrabApprovalInit(workspace, timeoutMs)`，停机时 Destroy |
| `others/UI/app.py` | 生产 | payload 透传 `decision`/`replyToInterruptId`；响应透传 `interrupt` |
| `others/UI/templates/index.html` | 生产 | 审批卡片（工具名/参数/倒计时/三按钮）与决策请求 |
| `tests/test_approval_e2e.py` | 新增 | 端到端测试（见第 8 节） |
| `tests/test_agent_matrix.py` | 修改 | 补充审批场景（伪造 LLM 返回 exec_program） |
| `tests/test_main.c` 或新 C 测试 | 修改/新增 | Kernel 挂起/恢复的函数级测试 |
| `config/base_config.example.json` | 修改 | approval 示例块 |
| `README.md` | 修改 | 审批使用说明 |

## 7. 端到端数据流

```
用户（UI）──chat："把频段调到 Band3"───────────────▶ Gateway ─▶ Hub ─▶ Agent
                                                              │ Router→LLM
                                                              │ LLM 提案 exec_program(Band3)
                                                              │ ④判定：非只读、未白名单 → 挂起
                                                              │   ├ 写审批 apr-000001（落盘）
                                                              │   ├ 消息追加占位 tool 结果
                                                              │   ├ AgentSessionStateSave
                                                              │   └ 请求结束（HANDLED）
用户（UI）◀──{"content":"...","interrupt":{...}}──────────────┘
   │ UI 渲染弹窗：[允许] [本会话总是允许] [拒绝]
   ▼ 用户点[允许]
用户（UI）──chat：replyToInterruptId=apr-000001, decision=approve──▶ ... ─▶ Agent
                                                              │ CrabApprovalDecide（五连校验）
                                                              │ Consume → 重建调用 → 执行
                                                              │ 占位符 ← 真实结果
                                                              │ 本轮剩余调用 → 后续 LLM 迭代
用户（UI）◀──"已完成：频段 Band2 → Band3"──────────────────────────┘
```

## 8. 测试矩阵（失败测试先行；mock LLM + mock 工具产物）

| # | 场景 | 预期 | 副作用断言 |
|---|---|---|---|
| 1 | 提案 exec_program → approve | interrupt 信封 → 执行 → 最终回答 | marker/参数变化出现 |
| 2 | 提案 exec_program → reject | 不执行；模型收到 rejected_by_operator 并向用户解释 | 无副作用 |
| 3 | always_allow 后再次提案同工具 | 第二次无 interrupt，直接执行 | 白名单 Trace 事件 |
| 4 | 超时未决（伪时钟/短 timeout）→ 新消息 | 占位符替换 approval_expired；新消息正常处理 | 无副作用 |
| 5 | 跨会话 decision | 稳定错误 + 引导，不执行 | 无副作用 |
| 6 | 自然语言"是的，执行吧" | 引导响应，不放行（红线） | 无副作用 |
| 7 | 告警来源请求提案非只读工具 | 不触发审批，行为与现状一致（范围排除） | 与今日相同 |
| 8 | `approval.enabled=false` | 全程无 interrupt，行为与现状一致 | 回归 |
| 9 | 挂起 → 重启 → approve | pending 恢复可决；执行成功 | 上下文完整 |
| 10 | 同一审批连发两次 decision | 恰一次成功，第二次 ERR_STATE | 单次副作用 |
| 11 | 只读工具（read/grep） | 永不触发 interrupt | — |
| 12 | memory_remember（默认排除） | 不触发 interrupt | — |
| 13 | 一轮多危险调用 | 顺序弹窗，逐个决议 | 顺序执行 |
| 14 | 挂起期间同会话普通消息 | 引导响应（含 interrupt 信封） | 不进 LLM |
| 15 | 挂起期间其他会话消息 | 正常处理（不互相阻塞） | — |
| 16 | decision 缺 replyToInterruptId | `ERROR: decision requires ...` | 无副作用 |
| 17 | pending.json 损坏后重启 + decision | ERR_NOT_FOUND + 引导 | 无副作用 |
| 18 | 老客户端（不支持 interrupt） | 只读 content 人读摘要，不崩溃 | 兼容 |

## 9. 兼容性影响

### 9.1 明确的行为变化（本步骤目的，非回归）

- `approval.enabled=true`（默认）时，网关来源的非只读工具调用从"直接执行"变为"挂起等待审批"——**这正是需求本身**。
- 涉工具场景的既有 E2E（agent_matrix 等）需要在测试配置中显式 `approval.enabled=false` 或走 always_allow 前置，测试基建随之调整。

### 9.2 保持兼容

- 协议向后兼容（interrupt/decision 均为可选字段）。
- 告警路径、嵌入式 API 路径行为不变。
- 审批关闭时零行为差异（用例 8 守护）。

## 10. 实施顺序

1. Hub/ Gateway：`decision` 透传 + `interrupt` 信封（先通管道，可独立测试）。
2. Kernel 挂起路径：触发判定 + 占位符 + 保存 + 中断响应（失败测试先行：用例 1 的挂起半段、11、12）。
3. Kernel 恢复路径：决策匹配 + 执行注入 + 继续本轮（用例 1 完整、2、10）。
4. 封锁规则与过期解锁（用例 4、6、14、15）。
5. always_allow 全链路（用例 3）。
6. 配置块 + 默认值 + 范围校验（用例 8、16）。
7. UI 卡片与透传（人工验收 + 用例 18）。
8. 重启恢复与损坏恢复（用例 9、17）。
9. 全量回归 + 更新总计划第 11 步状态。

## 11. 验证命令与通过标准

```sh
cmake -S . -B build-approval -DLITECRAB_BUILD_TESTS=ON
cmake --build build-approval -j
ctest --test-dir build-approval -R "litecrab_approval|litecrab_agent_matrix" --output-on-failure
ctest --test-dir build-approval --output-on-failure
```

通过标准：
1. 第 8 节 18 项场景全部通过。
2. 全量回归通过；`approval.enabled=false` 时与基线行为逐字节一致。
3. 拒绝、超时、封锁、损坏场景均有"无副作用"断言（marker 文件不存在等）。
4. 生产代码变更仅落在第 6 节清单内。

## 12. 回退边界

与第 10 步作为两个独立提交点，但**联动回退规则**：只回退第 11 步（接线）即可让产品回到无审批行为（第 10 步存储模块留存无害）；不允许只保留接线而回退存储。协议字段为可选，回退后老响应格式自动恢复。

## 13. 评审重点

1. **告警路径豁免审批**是否可接受（无人值守修复是核心场景 vs 高危动作无人把关的矛盾——本设计选择前者，治理留给 Mutation Ledger）。
2. **`approval.enabled` 默认 true** 是否可接受（改变所有网关部署的默认交互；secure-by-default 与"默认行为不变"纪律的取舍）。
3. **未决期间同会话封锁**（4.5）是否可接受——牺牲"边等审批边聊天"换取状态机简单与正确性。
4. 一轮多危险调用的顺序弹窗体验是否可接受（备选：一次性批量弹窗，协议更复杂）。
5. 嵌入式 API（robot_hi3516）路径暂不接线，是否需要提前到 v1。

## 14. 与开源实现的对照（设计验证）

完整分析（含 file:line 证据）见 [交互式审批机制开源对照分析](../others/交互式审批机制开源对照分析.md)。对照对象：packages_ai_agent（小米 openvela，零人工介入模型）与 SiteClaw（Rust，聊天式放行模型）。

| 维度 | packages_ai_agent | SiteClaw | 本设计 |
|---|---|---|---|
| 审批触发 | 无 | 仅 exec 高风险命令 | 所有非只读工具 |
| 决策通道 | 无 | 聊天关键词 | 结构化 decision 字段 + UI |
| 自然语言 | 能触发 | **能放行（含时序漏洞）** | **永不放行**（4.8 红线） |
| 授权绑定 | 不适用 | 命令字符串精确匹配 | 编号+会话+参数摘要 |
| 防重放/偷换 | 不适用 | 命令级正确；yes 扫描不校验时序 | 五连校验 + 单次消费 |
| 超时 | 无概念 | 无（清空函数死代码） | 自动拒绝（fail-closed） |
| 重启持久化 | 限流清零 | 全内存失效 | pending 持久化可恢复 |
| fail 方向 | 偏 open | 混合（链未设=放行） | 全链 fail-closed |

对照验证的关键设计决策：

1. **SiteClaw 的时序漏洞**（历史中任意 yes 类消息可放行最近待审批命令——用户从未表态）直接验证 4.8 自然语言红线的必要性。
2. **SiteClaw 无超时无拒绝状态**（审批不了了之、可被后续对话误触）验证超时自动拒绝 + 占位符惰性落地的设计。
3. **SiteClaw 的 WebUI 审批卡片是死代码**（组件存在但事件与 choice 均无后端处理）验证本仓"协议、实现、UI、E2E 同批落地"的做法。
4. **两家的 fail-open 模式**（安全链未设即放行、无认证入口直达工具、TLS 不验证书）验证本仓 fail-closed 全链选择。

对照发现的候选借鉴（已记入对照分析第 9 节，对齐计划步骤编号）：硬拒绝不可审批底线、风险分级触发（Step 7 / 闲置的 riskLevel 字段）、输入侧注入检测（增强 5.1）、无状态限流（Step 6 后）、日志脱敏（Step 3 未完成部分）。
