# 能力清单与 LLM 输出协议（agent 智能体编排）

> 本文是"串口文字 → 大模型 → 切表情/控舵机"功能的唯一权威说明。
> 整体架构、文件地图、二次开发（语音输入/自有 agent/改格式）见
> 《架构与二次开发指南.md》。
> 代码入口：`applications/app_test/service/agent_service.h/.c`（能力表、prompt、解析、分发）。
> 表情执行端：`applications/ui_display_sdk`（独立进程，经文件通道接收表情命令）。

---

## 1. 机器人能力总览（发给大模型的内容）

### 1.1 舵机动作（23 个，LLM 可直接点名）

全部执行完自动归位（handsup 除外，举手保持）；智能体侧由后台动作线程逐个
**阻塞执行**（模式同按键服务），多个动作严格排队不互踩；串口 `stop` 可中止
当前动作并归位。

| 动作名 | 中文（prompt 内展示，区分动不动腿） | 默认参数 | 动作名 | 中文 | 默认参数 |
|---|---|---|---|---|---|
| walk | 向前走步 | 4步 1000ms 不摆臂 | moonwalk | 太空步滑行 | 3步 900ms h=25 |
| turn | 原地转身踏步 | 4步 1000ms 左转 | whirlwind | 旋风踢腿 | 2步 700ms amp=30 |
| jump | 原地蹲站跳 | 1次 1000ms | handsup | 举手不归位 | 800ms 双手(不归位) |
| bend | 左右侧弯身 | 3次 1000ms 左 | handsdown | 把手放下 | 800ms 双手 |
| shakeleg | 抬起单腿摇 | 1次 2000ms 左腿 | handwave | 原地挥手(脚不动) | 左手 5次 |
| updown | 身体一起一落 | 4步 1000ms h=30 | windmill | 手臂大风车 | 5次 300ms amp=40 |
| swing | 左右摇摆身体 | 4步 1000ms h=30 | takeoff | 起飞扑扇舞步 | 5次 300ms amp=40 |
| tiptoe | 踮脚小幅摆动 | 4步 1000ms h=30 | fitness | 原地健身操 | 5次 1000ms amp=25 |
| jitter | 原地抖动 | 4步 1000ms h=20 | greeting | 转身挥手打招呼(会转身体) | 左 5次 |
| ascend | 螺旋升天转圈 | 4步 1000ms h=13 | shy | 侧身害羞扭捏 | 左 5次 |
| crusaito | 十字舞步(会位移) | 4步 1000ms h=25 | flapping | 拍翅膀舞步(会位移) | 4步 1000ms h=25 |
| sit | 坐下(保持坐姿不归位) | 无参数 | | | |

> 描述精确化的原因：六足上 `greeting` 的"转身"靠脚舵机枢轴实现（原地扭身），
> 用户看是"在走"；`handwave` 才是纯手臂摆动脚不动。描述里写明"脚不动/
> 会转身体/会位移"，模型才能对"挥挥手"这类指令选中真正原地的动作。

受控动作（**不在** LLM 能力表里，仅串口 `run/post` 可用）：
showcase（动作串 2~3 分钟）、magic（转圈约 28s）、calisthenics（体操约 32s）、
home（归位）。单舵机角度/脉宽同理只走串口命令。

### 1.2 表情（21 个，屏上 GIF 动画）

| 名 | 中文含义（prompt 内展示给模型） | 名 | 中文含义 |
|---|---|---|---|
| neutral | 平静/无表情（默认兜底） | winking | 眨眼/调皮 |
| happy | 开心/高兴 | cool | 得意/酷 |
| laughing | 大笑/笑出声 | relaxed | 放松/惬意 |
| funny | 滑稽/搞笑 | delicious | 美味/好吃 |
| sad | 难过/伤心 | kissy | 飞吻/亲亲 |
| angry | 生气/愤怒 | confident | 自信/笃定 |
| crying | 哭泣/流泪 | sleepy | 困倦/打瞌睡 |
| loving | 喜爱/爱心 | silly | 傻气/搞怪 |
| embarrassed | 尴尬/脸红 | confused | 困惑/不解 |
| surprised | 惊讶/意外 | thinking | 思考/犹豫 |
| shocked | 震惊/吓到 | | |

表情**语义化选择规则**（写死在 prompt 核心规则）：按括号里的中文含义选最
贴合用户语义的一个；**用户没提情绪就不切表情（none）**，禁止默认用 happy，
也不拿 neutral 当兜底——neutral 只在用户明确要"平静/无表情"时使用。

表情文件在 userfs `/storage/data/emoji/<名>.gif`，由 ui_display 进程播放；
app_test 与 ui_display 是**两个进程**，表情命令经 §4 文件通道转发。

---

## 2. LLM 输出协议（格式约束核心）

### 2.1 协议规定（v4.2：四行缺一不可）

LLM 回复**必须**包含下面四行，顺序固定，**没有操作的行写 `none`（空指令）**，
除这四行外不允许输出任何其它内容：

```
[SAY] 给用户看的中文回复（一行）
[EMOJI] 表情名 或 none
[ACT] 动作名 或 none
[END]
```

- 四行**一个都不能省**：没有表情就写 `[EMOJI] none`，没有动作就写
  `[ACT] none`——空指令让解析端能明确区分"模型没给"和"模型不想动"
- **语义严格对上，禁止补操作**：只执行用户明确要求的操作——只要表情就不写
  动作，只要动作就不写表情，都没要就都写 none；禁止自作主张补动作/补表情，
  **禁止用默认表情/动作兜底**（用户没提情绪就 none，不要硬凑 happy/neutral）
- **复合指令拆解**：用户要多个动作就写多行 `[ACT]`，顺序与用户说的先后一致，
  代码端排队逐个执行（如"走两步然后挥手"= 两行 [ACT]：walk、handwave）
- **反向动作**：在动作名后加 -1，如 `turn -1`=右转、`walk -1`=后退。
  代码端（`NormalizeAct`）会把首参 ±1 识别为方向并自动补默认步数/周期——
  因为底层签名是 `turn <steps> <periodMs> <dir> <arm>`，参数从左填，
  裸 ±1 若不桥接会落在 steps 上导致空转直接"完成"
- 动作名/表情名必须原样用 §1 表里的英文小写名，禁止自造或写中文
- 同一行的参数：`[ACT] walk 2 800`（名字+整数参数，省略=用默认参数）
- **prompt 布局（弱模型服从优化）**：四行模板放最前面 → 能力表作填空数据 →
  一条核心规则 → 三个示例（第一个就是"只要表情"用例）→ 用户文字
- 表情按中文含义选最贴合的一个，**不确定就不选（none）**，不设默认兜底

### 2.2 对话示例

```
用户: 换个开心的表情
[SAY] 好的, 给你一个开心的表情
[EMOJI] happy
[ACT] none
[END]

用户: 做个思考的表情，然后向左转
[SAY] 让我想想，这就向左转
[EMOJI] thinking
[ACT] turn
[END]

用户: 你会什么
[SAY] 我会21种表情和22个动作, 试试让我开心地走两步!
[EMOJI] none
[ACT] none
[END]

用户: 开心点，走两步再挥挥手
[SAY] 好嘞，开心地走两步再挥手！
[EMOJI] happy
[ACT] walk
[ACT] handwave
[END]
```

### 2.3 解析容错与审计（agent_service.c 实际实现）

容错（LLM 输出 slightly 不标准时仍能解析）：
- 标记大小写不敏感（`[say]` 等价 `[SAY]`），兼容全角括号 `【SAY】`
- 内容前的空白/中英冒号/引号自动跳过（`[EMOJI]： "happy"` 也能解析）
- 行首 markdown 装饰（`*`、`-`、反引号、代码围栏）自动清理
- 标记名误匹配保护（`[SAYX]` 不会当作 `[SAY]`）

审计（格式违规必留痕，便于发现模型跑偏）：
- 未知表情名/动作名：`未知表情已忽略: xxx`，不影响其它行
- 缺任何一行：`缺 [EMOJI] 行(无操作应写 none)` 逐行提示
- 缺 `[END]`：`缺 [END] 行, 已按已解析内容执行`
- 一个标记都没有：`回复未含任何标记(格式违规)`，随后自动跑**兜底扫描**：
  认得 `expression:/emotion:/emoji:/表情:/表情:`（表情）、
  `action:/act:/动作:/动作:`（动作，可多行按顺序排队）、`say:/回复:/想法:`
  （文本）等漂移字段，行首序号/装饰均可，救回任意一条即打
  `已按兜底格式救回指令`；同类型多条按出现顺序执行（表情以最后一条为准）；
  一条都救不回才提示 `无可用指令, 可重试`

### 2.4 能力清单的送达方式（双保险）

1. **首次连云上报一次**：app_test 启动连上 WiFi 后，`AgentSvcAnnounce()`
   把能力清单+格式说明发给 LLM 并等它确认（只发一次，成功后置标志）
2. **每次对话内嵌**：网关是无状态单轮问答（每条消息独立 HTTP），
   大模型不会真的"记住"上次内容——所以每次 `chat` 请求都会把
   能力清单+格式说明+用户文字拼成一条消息发出（单条上限 4KB，
   超长会截断并打 `[llm] !! 消息超发送上限` 告警——截断会让模型看不到
   格式约束和用户问题，输出必然跑偏）。
   当前能力块+格式+示例约 2KB，剩余约 2KB 给用户文字。

---

## 3. 对话流程（时序）

```
串口: chat 你好, 开心点并打个招呼
  └> AgentCmdChat(app_test.c 命令表)
       └> AgentSvcChat(text)
            ├> 组包: 能力prompt + 用户文字
            ├> LlmSvcChat() ──TLS──> 网关(/v1/chat/completions)
            ├> 解析回复行([SAY]/[EMOJI]/[ACT]/[END], 缺行审计)
            ├> [EMOJI] 校验表情名 → 写 /storage/data/emoji_cmd(§4)
            ├> [ACT]   校验动作名 → 入队(可多行) → 后台线程逐个阻塞执行
            └> [SAY]   串口打印 [agent] 回复
```

---

## 4. 跨进程表情命令通道（app_test → ui_display）

| 项 | 约定 |
|---|---|
| 载体 | userfs 文件 `/storage/data/emoji_cmd` |
| 写入方 | app_test（agent_service）：fopen("w") 写一行表情名后 fclose |
| 读取方 | ui_display 主循环：每 20 tick(约 200ms) fopen 读一次，**读到即 remove** |
| 内容 | 一行一个表情名（如 `happy`），≤23 字节；未知名 ui_display 忽略并打日志 |
| 线程安全 | ui_display 在主循环(UI 线程)里读取并切换，无并发问题 |

最后写入者生效（覆盖写）；ui_display 处理完删除文件，下次命令重新写入。

### 4.1 语音链路的云端指令（v4.3：阿里云 FC，同一套校验分发）

语音对话（喊 Luna）时云端指令不走四行文本协议，走 **Function Calling**：
云端音频模型按 `control_robot` 工具的枚举点名真名动作/表情
（枚举由 agent_service 能力表生成，见 `voice_dialog.c BuildSessionJson`，
能力表仍是唯一来源），arguments 形如 `{"action":"turn","dir":1}` 或
`{"expression":"happy"}`。板端 `voice_dialog.c fc_worker` 解析后调
`AgentSvcDispatchEmoji/DispatchAct` —— 校验、方向桥接（dir ±1 经
NormalizeAct 展开）、表情文件通道、动作队列全部与串口 `chat` 共用，
日志同样打 `[agent] 动作 -> ...` / `[agent] 表情 -> ...`。
Luna 的口头回复由云端二轮生成（不是 [SAY] 行）；`[You]/[AI]` 打印双向转写。

---

## 5. 串口指令与扩展方法

### 5.1 指令（app_test 控制台，LLM 分组）

| 指令 | 作用 |
|---|---|
| `chat <文字...>` | 和大模型对话（一行 UTF-8），回复自动解析并触发表情/动作 |
| `chatu <码点...>` | 同上，中文用 4 位 hex 码点，如 `chatu 4f60 597d 5f00 5fc3` |
| `ask` / `uni` | 原有裸问答（不走能力 prompt、不解析、不触发功能） |

### 5.2 加动作/加表情（各三步）

加动作：
1. `agent_service.c` 的 `g_actTable[]` 加一行 {名字, 中文}
2. （可选参数版）串口侧在 servo_service 已支持；智能体固定用默认参数
3. 重编 app_test

加表情：
1. GIF 放进 `applications/ui_display_sdk/assets/emoji/<名>.gif`
2. `ui_display_sdk/BUILD.gn` 的 emoji_assets sources 加一行
3. `agent_service.c` 的 `g_emojiTable[]` 加一行名字 → 重编两个程序

### 5.3 关键代码位置

| 内容 | 位置 |
|---|---|
| 能力表（动作/表情） | `agent_service.c` 顶部 g_actTable / g_emojiTable |
| 能力 prompt 文本 | `agent_service.c` BuildPrompt() |
| 协议解析与分发 | `agent_service.c` ParseReply() / Dispatch() |
| 表情通道读取 | `ui_display_sdk/ui_display.cpp` PollEmojiCmd() |
| LLM 传输层 | `llm_service.c` LlmSvcChat()（新增的带输出缓冲问答） |
