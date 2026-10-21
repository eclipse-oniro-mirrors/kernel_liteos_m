# 产品 Wiki 编写规范与可执行诊断知识模板

## 1. 文档目的

本文指导产品、领域专家和研发共同编写可被转换 Skill 稳定转换为诊断知识库的 Wiki。

目标不是只描述“可能是什么问题、建议尝试什么”，而是形成接近 `PLC_Diagnosis` 的可执行知识：

```text
问题识别
→ 入口证据核验
→ 原因分支选择
→ 诊断动作
→ 修复动作
→ 参数/动作结果验证
→ 问题恢复验证
→ 明确结束状态
```

转换 Skill 可以做格式转换、字段归一化、工具绑定检查和生成测试，但不能替产品补充以下内容：

- 不能猜测根因判断条件。
- 不能猜测命令参数或设备映射。
- 不能把“查看一下”“调整参数”“观察是否恢复”补成具体动作。
- 不能替产品决定是否允许自动修改。
- 不能把缺失日志、接口失败或没有新数据解释为问题恢复。

缺少关键字段时，转换结果必须标记为“不可自动执行”，而不是生成看似完整的修复流程。

## 2. 当前知识库的主要问题

现有 `docs/rag` 已经包含问题分类、根因、阶段动作、通过标准和失败回退，具备良好基础。但要自动转换为可靠 Skill，还存在以下问题：

1. **动作标识不稳定**  
   使用 `ACT:log`、`ACT:web`、`CMD:setfreq` 等文本标签，但没有统一的 capability ID 和参数契约。

2. **部分动作不够具体**  
   例如“查看日志”“调整功率”“升级修复版本”“联系研发”，没有说明输入、操作目标、输出字段和完成标准。

3. **多个动作写在同一步**  
   例如“执行 pktstart + 等待 + pktinfo + 对比”，转换器无法判断某一步失败后从哪里继续。

4. **分支不完整**  
   表格中的“失败回退”为 `—`，没有区分成功、失败、未知、无权限、数据不足和工具不可用。

5. **验证方式和触发来源耦合不清**  
   ASP 告警、日志异常和人工反馈的入口身份及恢复验证方式不同，不能统一使用活动告警复查。

6. **参数来源不明确**  
   `dtei`、设备 ID、设备类型、目标版本、白名单文件等值，常未说明来自用户输入、前序查询还是固定映射。

7. **风险和授权缺失**  
   写参数、重启、升级、导入白名单和物理操作没有统一标记风险等级、是否可自动执行及是否需要授权。

8. **工具索引不完整**  
   部分文档引用尚不存在的工具文档或问题分类，转换时无法完成能力绑定。

9. **通过标准不完全可判定**  
   “通信恢复”“效果改善”“升级成功”等描述没有对应字段、比较方式、时间窗口或阈值。

10. **缺少动作验证与问题验证的分离**  
    命令返回成功不等于参数已经生效，参数生效也不等于设备问题已经解决。

本文模板用于消除上述不确定性。

## 3. 编写职责划分

### 3.1 产品/领域专家负责

- 问题边界、现象、排除条件。
- 根因及根因之间的优先级。
- 根因成立所需的正向和反向证据。
- 哪个动作允许自动执行，哪个必须人工执行。
- 修复成功、失败和未知的业务标准。
- 验证需要等待多久、观察多少次。
- 不成功时进入哪个分支或何时停止。
- 适用设备、版本和场景。
- 已知风险、回滚原则和升级条件。

### 3.2 研发负责

- 建立稳定的 `capability_id`。
- 将 capability 绑定到 API、命令、脚本或 UI 自动化。
- 定义参数类型、返回 JSON 字段和错误码。
- 标明读操作、写操作、幂等性和超时。
- 提供可测试的成功、失败和异常样本。
- 保证敏感信息不进入 Wiki 和 Agent 上下文。

### 3.3 转换 Skill 负责

- 校验 Wiki 结构和必填字段。
- 将问题索引转换为 Skill 路由表。
- 将原因与流程转换为 reference 文档。
- 根据 capability registry 生成准确工具调用说明。
- 生成停止条件、结果矩阵和总结字段。
- 对缺失或冲突内容生成阻断错误。
- 生成基础结构测试，但不发明业务事实。

## 4. 基本编写规则

### 4.1 一页只描述一个问题类型

推荐：

```text
P001 丢包率高
P002 组网不全
P003 白名单配置异常
```

不要在同一页混合多个没有共同恢复标准的问题。

### 4.2 所有对象使用稳定 ID

| 对象 | ID 示例 | 说明 |
|---|---|---|
| 问题 | `P001` | 用户可观察的问题类型 |
| 根因 | `C001` | 某问题下的原因 |
| 流程 | `F001` | 针对根因的执行流程 |
| 步骤 | `S001` | 流程中的原子步骤 |
| 能力 | `plc.monitor.read` | 稳定语义能力 |
| 验证器 | `asp.active_alarm` | 入口或恢复验证方式 |
| 指标 | `packet_loss_rate` | 可计算指标 |
| 人工动作 | `manual.check_wiring` | 无法自动执行的动作 |

ID 一旦发布不能改变语义。标题可以修改，ID 不应复用。

### 4.3 一个步骤只做一个原子动作

错误：

```text
设置频段，等待10分钟，然后查询丢包率，如果没恢复就换下一个频段
```

正确拆分：

```text
S040 写入候选频段
S050 读回频段
S060 建立修复后验证检查点
S070 等待一个新观察窗口
S080 判断问题是否恢复
S090 选择下一个候选
```

### 4.4 所有判断必须可判定

错误：

```text
丢包率明显改善
通信基本恢复
日志看起来正常
```

正确：

```text
packet_loss_rate <= 5%
且 sample_count >= 100
且连续3个完整窗口满足条件
```

如果业务上只能人工判断，应明确写成 `manual_confirmation`，不能伪装成自动判断。

### 4.5 所有失败都必须有去向

每个步骤必须分别定义：

- `on_success`
- `on_failure`
- `on_unknown`
- `on_unavailable`（能力尚未实现或当前环境不可用）
- 必要时增加 `on_denied`（未获得写操作授权）

不能使用 `—` 表示分支。

### 4.6 不把来源材料中的建议直接当成已验证规则

案例、DTS、经验和推测必须标明证据等级：

| 等级 | 含义 |
|---|---|
| `verified` | 已由正式接口、测试或多个现场案例验证 |
| `case_supported` | 有明确案例支持，但适用范围有限 |
| `expert_hypothesis` | 专家假设，需要现场验证 |
| `unknown` | 仅收录信息，不能自动执行 |

只有 `verified` 或明确评审通过的 `case_supported` 规则才能进入自动修复流程。

## 5. Capability 与具体命令解耦

产品 Wiki 不应直接依赖容易变化的脚本路径，而应引用稳定的 `capability_id`：

```yaml
capability_id: plc.frequency.set
operation: write
```

研发维护独立 capability registry：

```yaml
capability_id: plc.frequency.set
implementation:
  tool: exec_program
  command: main
  path: skills/PLC_Diagnosis/scripts
  args_mapping:
    operation: set_freq_band
result_schema:
  success_field: success
  write_accepted_field: data.write.accepted
  readback_field: data.after
```

这样脚本路径或 API 改变时，只更新 registry，不需要产品重写全部 Wiki。

如果尚无实现，应写：

```yaml
capability_id: plc.whitelist.import
availability: unavailable
executor: manual
```

转换 Skill 必须保留为人工步骤或阻止自动执行，不能自行生成命令。

## 6. Wiki 必填结构

每个问题 Wiki 必须依次包含：

1. 文档元数据
2. 问题定义
3. 触发来源与入口验证
4. 必需输入及对象映射
5. 根因目录
6. Capability 依赖
7. 执行上下文
8. 原因选择规则
9. 原子执行流程
10. 动作结果验证
11. 问题恢复验证
12. 回滚、停止和升级人工条件
13. 结果矩阵
14. 最终输出要求
15. 测试用例
16. 来源与变更记录

章节名和顺序应保持稳定，以便转换 Skill 解析。

## 7. 完整 Wiki 模板

以下模板可直接复制。尖括号内容必须替换；`TBD` 表示尚未明确，包含关键 `TBD` 的文档不能标记为 executable。

---

# <Pxxx：问题名称>

```yaml
schema_version: 1
problem_id: Pxxx
title: <问题名称>
status: draft                 # draft | review | executable | deprecated
owner: <产品或领域负责人>
technical_owner: <研发负责人>
reviewers:
  - <评审人>
last_reviewed_at: YYYY-MM-DD
knowledge_version: 1.0.0
evidence_level: verified      # verified | case_supported | expert_hypothesis | unknown
automation_level: assisted    # read_only | assisted | auto
risk_level: medium            # low | medium | high | critical
applies_to:
  products: [<产品>]
  device_types: [<设备类型>]
  hardware_versions: [<范围或 all>]
  software_versions: [<范围或 all>]
  regions: [<范围或 all>]
trigger_sources:
  - <alarm:asp | alarm:log-health | user | inspection>
tags:
  - <检索关键词>
supersedes: []
source_documents:
  - id: <DTS/案例/规范编号>
    title: <来源名称>
    location: <页码/章节/链接>
```

## 1. 问题定义

### 1.1 用户可见现象

- <现象1>
- <现象2>

### 1.2 机器识别条件

```yaml
match:
  any:
    - field: <标准字段>
      operator: <eq | contains | regex | gt | gte | lt | lte>
      value: <值>
  all: []
exclude:
  any:
    - field: <字段>
      operator: <操作符>
      value: <排除值>
```

### 1.3 不属于本问题的情况

| 条件 | 应路由的问题/动作 |
|---|---|
| <排除条件> | <Pxxx 或停止说明> |

### 1.4 问题范围

- 影响对象：<设备/链路/子阵>
- 影响程度：<判定方式>
- 是否允许仅凭用户描述进入修复：<是/否；通常为否>

## 2. 触发来源与入口验证

每种触发来源单独定义入口验证器。

### 2.1 <来源名称>

```yaml
source: <alarm:asp | alarm:log-health | user | inspection>
trigger_validator: <asp.active_alarm | log_health.issue | manual.confirmation>
identity_fields:
  - <字段>
required_state: <ACTIVE | OPEN | CONFIRMED>
stale_after_seconds: <秒或不适用>
on_recovered_before_action: finish_no_change
on_not_found: stop_unknown
on_stale: stop_unknown
```

入口验证步骤：

| 顺序 | capability_id | 输入 | 成功判据 | 失败/未知处理 |
|---|---|---|---|---|
| 1 | <只读能力> | <参数来源> | <字段和值> | <停止或下一步> |

## 3. 必需输入与对象映射

### 3.1 输入字段

| 字段 | 类型 | 必填 | 来源 | 示例 | 缺失处理 |
|---|---|---:|---|---|---|
| `trigger_source` | enum | 是 | 事件 | `alarm:asp` | 停止 |
| <字段> | <类型> | <是/否> | <事件/用户/查询/映射表> | <示例> | <处理> |

### 3.2 对象映射

明确区分“问题上报对象”和“修复目标对象”。

| 上报对象条件 | 修复目标 | 映射来源 | 有效范围 | 无法映射时 |
|---|---|---|---|---|
| <equip/type/location> | <target id/type> | <接口/配置/固定映射> | <站点/版本> | 停止写入 |

禁止把测试环境的固定 ID 当作所有站点通用映射。

## 4. 根因目录

| cause_id | 根因名称 | 优先级 | 证据等级 | 自动修复 | 对应流程 |
|---|---|---:|---|---|---|
| C001 | <根因> | 1 | verified | 是/否 | F001 |
| C002 | <根因> | 2 | case_supported | 是/否 | F002 |

## 5. 根因详细定义

### C001：<根因名称>

#### 5.1 适用条件

- <设备/版本/场景条件>

#### 5.2 必须同时满足的证据

```yaml
evidence_all:
  - evidence_id: E001
    capability_id: <只读能力>
    field: <返回字段>
    operator: <操作符>
    expected: <值>
```

#### 5.3 满足任一即可的证据

```yaml
evidence_any:
  - evidence_id: E002
    source: <alarm/log/user/query>
    field: <字段>
    operator: <操作符>
    expected: <值>
```

#### 5.4 排除证据

```yaml
negative_evidence:
  - evidence_id: E003
    field: <字段>
    operator: <操作符>
    expected: <值>
    route_to: <Cxxx/Pxxx/stop>
```

#### 5.5 证据不足时

```yaml
insufficient_evidence:
  result: unknown
  allow_mutation: false
  next_action: <补充查询/人工收集/停止>
```

#### 5.6 根因机理

<说明为什么这些证据支持该根因。不要只写解决措施。>

## 6. Capability 依赖

| capability_id | 用途 | 操作性质 | executor | availability | 幂等性 | 授权 |
|---|---|---|---|---|---|---|
| <id> | <说明> | read/write/wait/manual | agent/manual | implemented/unavailable | yes/no/conditional | none/explicit/preauthorized |

每个 capability 还必须在 registry 中定义参数和返回结构。

## 7. 执行上下文

流程开始后必须保存且贯穿所有步骤的变量：

| 变量 | 类型 | 初始化来源 | 更新时机 | 用途 |
|---|---|---|---|---|
| `problem_before` | object | Trigger Validator | 不覆盖 | 修复前证据 |
| `repair_target` | object | 对象映射 | 映射完成 | 所有读写动作 |
| `parameter_before` | value | 修复前查询 | 首次读取 | 参数对比 |
| `attempted_candidates` | set | 空集合 | 每次写入前 | 防止重复写 |
| `failed_candidates` | set | 当前失败项 | 验证失败后 | 候选过滤 |
| `verification_checkpoint` | object | 每次修复后 | 每个候选重建 | 隔离修复后证据 |
| <变量> | <类型> | <来源> | <时机> | <用途> |

## 8. 原因选择规则

按顺序评估，不允许将所有修复动作无条件执行一遍。

| 顺序 | 条件 | 选择根因/流程 | 不满足时 |
|---:|---|---|---|
| 1 | <可判定表达式> | C001/F001 | 继续规则2 |
| 2 | <表达式> | C002/F002 | 进入未知/人工 |

多个根因同时命中时：

```yaml
multiple_causes:
  policy: <highest_priority | sequential_non_conflicting | manual_decision>
  conflict_groups:
    - [C001, C002]
```

## 9. 执行流程

### F001：<流程名称>

#### 9.1 流程策略

```yaml
flow_id: F001
cause_id: C001
mutation_allowed_when:
  - <授权条件>
max_total_duration_seconds: <值>
max_mutations: <值>
rollback_policy: <automatic | manual | none>
resume_policy: <从检查点恢复/重新诊断>
```

#### 9.2 原子步骤

| step_id | 类型 | capability_id | 输入参数及来源 | 前置条件 | 成功判据 | on_success | on_failure | on_unknown |
|---|---|---|---|---|---|---|---|---|
| S010 | read | <id> | `target_id <- repair_target.id` | <条件> | `success=true` 且字段存在 | S020 | STOP_QUERY_FAILED | STOP_UNKNOWN |
| S020 | decide | — | <上下文变量> | <条件> | <表达式> | S030 | F002:S010 | STOP_UNKNOWN |
| S030 | write | <id> | <参数及来源> | 已授权且未尝试 | accepted=true | S040 | S035 | STOP_MUTATION_UNKNOWN |
| S035 | readback | <id> | <目标> | 写结果失败或不明 | <明确实际值> | <分支> | STOP_UNKNOWN | STOP_UNKNOWN |
| S040 | readback | <id> | <目标> | 写入已返回 | 实际值=目标值 | S050 | NEXT_CANDIDATE | STOP_UNKNOWN |
| S050 | checkpoint | <verifier> | 当前generation/time/cursor | 参数验证成功 | checkpoint创建 | S060 | STOP_UNKNOWN | STOP_UNKNOWN |
| S060 | wait | <verifier> | checkpoint之后 | 未到总期限 | 新观察结果 | S070 | STOP_UNKNOWN | STOP_UNKNOWN |
| S070 | decide | — | verifier result | <状态判断> | <表达式> | FINISH_SUCCESS | NEXT_CANDIDATE | STOP_UNKNOWN |

步骤类型限定为：

```text
read
write
readback
wait
decide
manual
checkpoint
rollback
finish
escalate
```

### 9.3 写操作要求

每个 write 步骤必须补充：

```yaml
write_contract:
  side_effect: <副作用>
  authorization: <explicit | preauthorized>
  idempotency: <yes | no | conditional>
  duplicate_prevention_key: <组成字段>
  pre_write_read: required
  readback: required
  retry:
    allowed: false
    reason: <写入结果不明时不得重复副作用>
```

### 9.4 人工动作要求

```yaml
manual_action:
  role: <现场工程师/产品支持/研发>
  exact_path_or_checklist:
    - <明确步骤1>
    - <明确步骤2>
  required_evidence:
    - <照片/导出文件/字段值/确认项>
  timeout_or_followup: <多久后跟进>
  on_no_response: pause
```

不能只写“现场检查”“联系研发”。

## 10. 动作结果验证

动作结果验证回答“动作是否执行成功”，不回答“问题是否解决”。

| action | 验证方式 | 成功 | 失败 | 未知 |
|---|---|---|---|---|
| 写参数 | 写响应 + 独立读回 | accepted 且 readback=target | 明确拒绝或读回不等 | 无读回/响应不完整 |
| 升级 | 查询运行版本 | 运行版本=目标版本 | 仍是旧版本/回滚 | 设备离线无法查询 |
| 导入白名单 | 导出并比对 | 内容、目标对象均一致 | 内容不一致 | 无法导出 |
| 人工接线 | 现场证据 + 状态查询 | 检查表完成且读数符合 | 检查不通过 | 无反馈 |

不能只依据“接口返回成功”判定参数生效。

## 11. 问题恢复验证

问题恢复验证回答“原问题是否已经恢复”，必须独立于动作结果验证。

### 11.1 验证器配置

每种触发来源单独定义：

```yaml
recovery_verifiers:
  alarm:asp:
    verifier_id: asp.active_alarm
    checkpoint: after_action
    interval_seconds: 60
    max_attempts: 6
    recovered_when: <目标范围不再存在同类活动告警>
    still_open_when: <目标范围仍存在同类活动告警>
    unknown_when: <查询失败或身份不完整>

  alarm:log-health:
    verifier_id: log_health.issue
    checkpoint: generation_after_action
    max_duration_seconds: 360
    recovered_when:
      state: RECOVERED
      minimum_good_windows: 3
    still_open_when:
      state: OPEN
    unknown_when:
      state_in: [STALE, ERROR, NOT_FOUND]

  user:
    verifier_id: manual.confirmation
    required_evidence:
      - <用户确认项>
```

### 11.2 检查点

只允许使用修复完成后的新证据：

| 字段 | 说明 |
|---|---|
| `action_finished_at` | 动作完成时间 |
| `generation` | 指标/issue状态版本 |
| `file_cursor` | 日志游标，可选 |
| `baseline_id` | 修复前基线 |
| `candidate_id` | 当前候选修复 |

每次尝试新候选都必须重建检查点。

### 11.3 结果状态

统一使用：

```text
RECOVERED
STILL_PRESENT
UNKNOWN
NOT_APPLICABLE
```

日志静默、工具失败、数据过期和没有足够样本必须是 `UNKNOWN`，不能是 `RECOVERED`。

## 12. 候选与重试策略

如果修复存在候选值，必须明确：

```yaml
candidates:
  source: <固定表/接口返回/产品规则>
  order: <升序/优先级/动态评分>
  exclude:
    - current_value
    - failed_candidates
    - attempted_candidates
  max_candidates: <值>
  per_candidate:
    write_once: true
    readback_required: true
    recovery_verification_required: true
  exhausted_result: still_present
```

需要同时列出业务值和接口值映射：

| 业务值 | 接口值 | 适用平台/版本 | 禁止条件 |
|---|---|---|---|
| <Band 1> | <1> | <范围> | <条件> |

## 13. 停止、回滚和人工升级

### 13.1 强制停止条件

- 目标对象无法唯一映射。
- 入口问题已经恢复。
- 写操作未授权。
- 写入结果不明且无法只读核实。
- 读回值与目标不一致。
- 验证数据 stale 或来源不可用。
- 达到最大修改次数或总时长。
- 命中高风险或未覆盖分支。
- 当前版本不在适用范围。
- 需要物理操作、升级或人工决策但无人确认。

### 13.2 回滚

```yaml
rollback:
  supported: <true | false>
  capability_id: <id或null>
  target: <原值/安全值>
  trigger_conditions:
    - <何时回滚>
  prohibited_conditions:
    - <何时禁止自动回滚>
  verification: <读回及问题验证>
```

如果没有经过验证的回滚策略，写 `supported: false`，不能让转换 Skill自行生成。

### 13.3 人工升级

| 条件 | 升级对象 | 必须携带的证据 |
|---|---|---|
| <条件> | <现场/产品/研发> | <日志摘要、版本、已执行步骤、读回值> |

## 14. 结果矩阵

必须分别报告动作/参数结果和问题结果：

| 参数/动作结果 | 问题结果 | 结论 |
|---|---|---|
| success | RECOVERED | 闭环通过，但不推断唯一根因或长期稳定性 |
| success | STILL_PRESENT | 动作成功，问题未解决，进入下一已定义分支或停止 |
| success | UNKNOWN | 动作成功，问题结果待确认 |
| failed | RECOVERED | 问题已恢复，但不能归因于本次动作 |
| failed/unknown | STILL_PRESENT | 闭环未通过 |
| not_executed | RECOVERED | 问题在操作前已恢复 |
| not_executed | UNKNOWN | 未能完成诊断或缺少证据 |

## 15. 最终输出要求

最终总结必须包含：

```yaml
summary_fields:
  - problem_id_and_title
  - selected_cause_and_evidence_level
  - trigger_source_and_identity
  - problem_evidence_before
  - executed_steps
  - skipped_steps_and_reasons
  - parameter_or_action_result
  - recovery_verification_rounds
  - problem_result
  - rollback_result
  - stop_reason
  - remaining_risk
  - next_action
```

禁止：

- 把计划执行写成已经执行。
- 把写入成功写成问题恢复。
- 隐藏失败和未知状态。
- 输出密码、Token、Cookie 或敏感完整日志。
- 声称没有证据支持的唯一根因。

## 16. 测试用例模板

每个 executable Wiki 至少包含以下用例：

| case_id | 场景 | 输入/前置状态 | 期望根因 | 期望步骤 | 期望结束状态 |
|---|---|---|---|---|---|
| T001 | 正常成功 | <数据> | C001 | S010→... | RECOVERED |
| T002 | 入口已恢复 | <数据> | 无 | 只执行入口验证 | 不修改 |
| T003 | 根因证据不足 | <数据> | unknown | 只读 | UNKNOWN |
| T004 | 写入明确失败 | <数据> | C001 | 停止或下一候选 | failed |
| T005 | 写入结果不明 | <数据> | C001 | 只读核实，不重复写 | UNKNOWN |
| T006 | 参数成功问题仍在 | <数据> | C001 | 完成验证 | STILL_PRESENT |
| T007 | 验证来源 stale | <数据> | C001 | 停止写入 | UNKNOWN |
| T008 | 未授权写操作 | <数据> | C001 | 只读诊断 | not_executed |
| T009 | 对象映射不明 | <数据> | unknown | 不写入 | UNKNOWN |
| T010 | 重启/重试 | <状态> | C001 | 不重复副作用 | <预期> |

## 17. 来源与变更记录

### 17.1 来源

| source_id | 类型 | 标题/编号 | 位置 | 支持的规则 | 可信等级 |
|---|---|---|---|---|---|
| SRC001 | DTS/案例/规范 | <名称> | <页码> | <C001/S030> | verified |

### 17.2 变更记录

| 版本 | 日期 | 修改人 | 修改内容 | 是否需要重新验证 |
|---|---|---|---|---|
| 1.0.0 | YYYY-MM-DD | <姓名> | 初版 | 是 |

---

## 8. 示例：频段不匹配根因片段

下面只演示模板的具体程度，不替代完整问题 Wiki。

```yaml
cause_id: C001
name: 网络频段不匹配
evidence_level: verified
applicable_when:
  - trigger problem is communication anomaly
  - repair target mapping is unique
evidence_all:
  - capability_id: plc.monitor.read
    field: data.devices[].signals[sigId=8201].sigValue
    operator: in
    expected: [1, 3, 23, 7, 13]
insufficient_evidence:
  result: unknown
  allow_mutation: false
```

候选映射：

| Band | 接口值 |
|---:|---:|
| 1 | 1 |
| 2 | 3 |
| 3 | 23 |
| 4 | 7 |
| 5 | 13 |

流程片段：

| step_id | 类型 | capability_id | 输入 | 成功判据 | on_success | on_failure | on_unknown |
|---|---|---|---|---|---|---|---|
| S010 | read | `plc.monitor.read` | target=repair_target | 找到8201且值合法 | S020 | STOP | STOP |
| S020 | decide | — | 当前Band与候选表 | 生成未尝试候选 | S030 | FINISH_STILL_PRESENT | STOP |
| S030 | write | `plc.frequency.set` | value=next_candidate.interface_value | write.accepted=true | S040 | S035 | S035 |
| S035 | readback | `plc.monitor.read` | target=repair_target | 能确定实际Band | S040或STOP | STOP | STOP |
| S040 | readback | `plc.monitor.read` | target=repair_target | actual=planned | S050 | NEXT_CANDIDATE | STOP |
| S050 | checkpoint | `source.recovery_verifier` | action完成后的状态 | 检查点创建 | S060 | STOP | STOP |
| S060 | wait | `source.recovery_verifier` | checkpoint之后 | 获得新证据 | S070 | STOP | STOP |
| S070 | decide | — | recovery result | RECOVERED/STILL/UNKNOWN | FINISH/NEXT/STOP | STOP | STOP |

关键约束：

- 当前 Band 直接加入 `failed_candidates`，不重复写入。
- 写入前将候选加入 `attempted_candidates`。
- 同一候选的写命令最多执行一次。
- 写入结果不明时先只读核实，不自动重复写。
- 参数读回成功后才允许进入问题恢复验证。
- ASP 触发使用活动告警验证器；日志触发使用 log-health issue 验证器。
- 每个候选都建立新的验证检查点。

## 9. Wiki 发布门禁

Wiki 只有通过以下检查才能标记为 `status: executable`：

### 9.1 结构检查

- [ ] problem/cause/flow/step/capability ID 唯一且稳定
- [ ] 所有必填章节存在
- [ ] 没有关键 `TBD`
- [ ] 没有指向不存在文档的链接
- [ ] 每个根因都有证据和流程
- [ ] 每个流程都有结束状态

### 9.2 动作检查

- [ ] 每个步骤只有一个原子动作
- [ ] 所有 capability 在 registry 中存在
- [ ] 参数来源明确
- [ ] 返回字段明确
- [ ] success/failure/unknown 分支完整
- [ ] 写操作的授权、幂等性和读回已定义
- [ ] 人工动作有角色、清单和反馈证据

### 9.3 验证检查

- [ ] 动作结果验证和问题恢复验证分开
- [ ] 每种 trigger source 都有对应验证器
- [ ] 有修复后检查点
- [ ] 时间窗口、次数和阈值明确
- [ ] stale/数据不足被定义为 UNKNOWN
- [ ] 候选耗尽后的结论明确

### 9.4 安全检查

- [ ] 自动化等级明确
- [ ] 高风险动作不默认自动执行
- [ ] 停止条件完整
- [ ] 回滚能力没有被夸大
- [ ] 敏感信息不进入 Wiki
- [ ] 版本和设备适用范围明确

### 9.5 测试检查

- [ ] 至少覆盖成功、失败、未知和入口已恢复
- [ ] 覆盖写入结果不明
- [ ] 覆盖数据 stale
- [ ] 覆盖对象映射失败
- [ ] 覆盖重试不重复副作用

## 10. 转换 Skill 的推荐输出

通过门禁后，转换 Skill 应生成：

```text
skills/<Domain_Diagnosis>/
├── SKILL.md
├── references/
│   ├── <Pxxx-问题名称>.md
│   └── capability-bindings.md
└── tests/
    ├── <Pxxx>-routing-cases.yaml
    ├── <Pxxx>-flow-cases.yaml
    └── <Pxxx>-safety-cases.yaml
```

生成规则：

1. 元数据和问题索引生成 `SKILL.md` 的路由表。
2. 每个问题的根因、上下文、流程和结果矩阵生成独立 reference。
3. capability registry 生成准确调用格式，不从自然语言猜命令。
4. trigger source 生成来源相关入口验证器。
5. recovery verifier 生成来源相关复查规则。
6. 测试用例生成路由、分支和安全回归测试。
7. 任何阻断缺失项都生成转换报告，不生成可自动修改的 Skill。

## 11. 最小可用模板

如果完整模板对早期录入过重，至少填写以下内容；少于这些字段只能作为检索资料，不能作为可执行知识：

```yaml
problem_id: Pxxx
title: <问题>
trigger_sources: [<来源>]
match_conditions: <可判定条件>
required_inputs: [<字段>]
causes:
  - cause_id: C001
    evidence: <成立证据>
    negative_evidence: <排除证据>
    capability_flow: F001
flows:
  - flow_id: F001
    steps:
      - step_id: S010
        capability_id: <稳定ID>
        inputs: <参数来源>
        success: <字段判据>
        on_success: <下一步>
        on_failure: <下一步>
        on_unknown: <下一步>
mutation:
  authorization: <要求>
  readback: required
recovery_verifier:
  by_source:
    <source>: <验证器、窗口、恢复/仍存在/未知标准>
stop_conditions: [<条件>]
result_matrix: <动作结果 × 问题结果>
tests: [<至少成功、失败、未知>]
```

## 12. 产品编写时的快速自检

写完一个动作后，逐项询问：

1. Agent 如何知道什么时候执行它？
2. Agent 从哪里取得每个参数？
3. 这个动作当前是否真的有工具支持？
4. 它是只读、写入、等待还是人工动作？
5. 返回哪个字段才算动作成功？
6. 如果响应丢失，能否安全重试？
7. 如何独立读回确认动作生效？
8. 如何确认原问题恢复，而不只是动作成功？
9. 验证使用的是告警、日志、指标还是人工反馈？
10. 没有新数据时是恢复还是未知？
11. 失败、未知、无权限时下一步是什么？
12. 哪个条件下必须停止自动操作？
13. 是否需要回滚，回滚是否真的实现并验证？
14. 最终报告需要保存哪些证据？

如果任一问题没有明确答案，应继续补充 Wiki，或将该动作标记为人工/不可自动执行。

