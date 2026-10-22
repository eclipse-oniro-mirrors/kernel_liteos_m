# Agent 舵机 Tool 补齐分析报告

> 对比对象：当前 3516 agent（`agent_core.c`）vs [OttoClaw](https://github.com/FlashCat-Jordan/OttoClaw) vs [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)
> 范围：仅限需下游舵机执行的 tool（不含记忆/查询/文件等非舵机类）
> 日期：2026-09-28

---

## 1. 当前 agent 舵机工具现状

当前 agent 在 `agent_core.c:30-48` 硬编码了 3 个机器人工具，通过 `tool_call_cb` 回调下发到下游舵机执行：

| 工具名 | 参数 | 能力范围 |
|---|---|---|
| `robot.move` | `direction`(enum 4值) + `steps`(1-10) | 仅前后左右移动，步数上限 10 |
| `robot.speak` | `text` | 语音播报 |
| `robot.stop` | 无 | 停止当前动作 |

**主要不足**：
- 只有 4 方向移动，无转向角度控制
- 无速度、幅度、过渡时间等运动参数
- 无预定义动作库（跳舞、挥手、坐下等）
- 无直接舵机角度控制，LLM 无法即兴创作姿态
- 无手部舵机控制
- 无 stop 之外的复位/归位动作

---

## 2. 两个开源项目的舵机 tool 体系

### 2.1 OttoClaw（ESP32 + 6 路舵机机器人）

工具定义位置：`main/tools/tool_otto.c`，注册入口：`main/tools/tool_registry.c:tool_otto_register()`

**舵机工具（2 个）**：

#### `self.otto.action` — 预定义动作

```json
{
  "action": "walk",        // 必填，21 种动作之一
  "steps": 3,              // 可选，1-100，默认 3
  "speed": 1000,           // 可选，100-3000，越小越快，默认 1000
  "direction": 1,          // 可选，1=前进/左转，-1=后退/右转，0=双手
  "amount": 25             // 可选，幅度 0-170，默认 25
}
```

支持 21 种 action（`tool_otto.c:82-160`）：

| 类别 | 动作名 | 说明 |
|---|---|---|
| 移动 | `walk` / `walk_backward` / `turn` / `moonwalk` | 前进/后退/转向/太空步 |
| 姿态 | `bend` / `shake_leg` / `sit` / `updown` / `swing` | 弯腰/抖腿/坐下/上下/摇摆 |
| 跳跃 | `jump` / `takeoff` | 跳跃/起飞 |
| 手部 | `hands_up` / `hands_down` / `hand_wave` | 举手/放手/挥手 |
| 花式 | `windmill` / `fitness` | 风车/健身操 |
| 情感 | `greeting` / `shy` | 打招呼/害羞 |
| 编舞 | `radio_calisthenics` / `magic_circle` / `showcase` | 广播体操/魔力转圈/表演 |
| 复位 | `home` | 回到初始位置 |

底层动作函数定义在 `main/otto/otto_movements.h`，6 路舵机索引：

```c
#define LEFT_LEG   0
#define RIGHT_LEG  1
#define LEFT_FOOT  2
#define RIGHT_FOOT 3
#define LEFT_HAND  4
#define RIGHT_HAND 5
#define SERVO_COUNT 6
```

#### `self.otto.pose` — 直接舵机角度控制（AI 即兴创作）

```json
{
  "left_leg": 90,      // 左腿舵机角度 0-180，默认 90
  "right_leg": 90,     // 右腿 0-180，默认 90
  "left_foot": 90,     // 左脚 0-180，默认 90
  "right_foot": 90,    // 右脚 0-180，默认 90
  "left_hand": 45,     // 左手 0-180，默认 45
  "right_hand": 135,   // 右手 0-180，默认 135
  "time": 700          // 过渡时间毫秒 100-3000，默认 700
}
```

**核心价值**：让 LLM 直接控制 6 路舵机各自的角度，可以组合出任意姿态（求婚、拥抱、祈祷、跳舞姿势等），可多次调用编排动作序列。这是当前 agent 完全没有的能力。

底层执行（`tool_otto.c:tool_otto_pose_execute`）：解析各字段 → 边界校验 0-180 → 调 `otto_move_servos(&g_otto, time_ms, targets)` 在 `time_ms` 时间内插值到目标角度。

### 2.2 xiaozhi-esp32-server（IoT 设备服务器）

工具定义位置：`main/xiaozhi-server/core/providers/tools/device_iot/iot_executor.py`

**核心机制**：不硬编码工具，用 `IotDescriptor` 描述符在运行时动态生成工具 schema。机器人作为一类 IoT 设备，其移动/转向等方法由设备描述符 JSON 声明，自动生成对应的控制工具。

**控制工具命名规则**：`<device>_<method>`

```json
{
  "name": "robot_walk",
  "parameters": {
    "speed": {"type":"number","description":"移动速度"},
    "steps": {"type":"number","description":"步数"},
    "response_success": "已移动 {steps} 步",
    "response_failure": "移动失败"
  }
}
```

执行时通过 WebSocket 下发到设备：
```json
{"type":"iot","commands":[{"name":"robot","method":"walk","parameters":{...}}]}
```

**特点**：
- 新增动作只需改描述符 JSON，不用改代码
- 支持 `response_success` / `response_failure` 模板，让 LLM 控制对用户的回复话术
- 工具参数随设备能力动态变化，适应不同型号机器人

---

## 3. 舵机能力对比矩阵

| 能力 | 当前 agent | OttoClaw | xiaozhi |
|---|---|---|---|
| 基础移动 | `robot.move`（4方向，1-10步） | `action=walk`（1-100步，速度可调，幅度可调） | `robot_walk`（动态参数） |
| 转向 | ❌（只有 left/right 方向） | `action=turn`（带方向和角度） | 动态方法 |
| 速度控制 | ❌ | ✅ `speed` 100-3000 | ✅ |
| 幅度控制 | ❌ | ✅ `amount` 0-170 | ✅ |
| 预定义动作库 | ❌ | ✅ 21 种动作 | ✅ 动态方法 |
| **直接舵机角度控制** | ❌ 完全没有 | ✅ `self.otto.pose`（6路独立） | ❌ |
| **手部舵机** | ❌ | ✅ hands_up/down/wave | ❌ |
| 复位/归位 | `robot.stop` | `action=home` | — |
| 转向角度量化 | ❌ | ✅ steps×90 度 | — |
| 跳跃/坐下/弯腰 | ❌ | ✅ | — |
| 编舞/表演动作 | ❌ | ✅ 3 种编排动作 | — |
| 情感表达动作 | ❌ | ✅ greeting/shy | — |
| 动作参数丰富度 | direction + steps | steps + speed + direction + amount | 任意参数 |
| 动态工具生成 | ❌ 硬编码 | ❌ 硬编码 | ✅ 描述符驱动 |

---

## 4. 需要补齐的舵机 tool 详细说明

### 4.1 P0 — `robot.pose`：直接舵机角度控制

**价值**：让 LLM 能即兴创作任意姿态，是当前最大的能力缺口。OttoClaw 的核心卖点之一。

#### 参数详细介绍

| 参数名 | 类型 | 必填 | 取值范围 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `left_leg` | integer | 否 | 0-180 | 90 | 左腿舵机目标角度（度）。90 通常为中位（直立），<90 向前倾，>90 向后倾 |
| `right_leg` | integer | 否 | 0-180 | 90 | 右腿舵机目标角度（度）。90 通常为中位（直立），与 left_leg 镜像 |
| `left_foot` | integer | 否 | 0-180 | 90 | 左脚舵机目标角度（度）。控制脚掌前后摆动，影响步幅与平衡 |
| `right_foot` | integer | 否 | 0-180 | 90 | 右脚舵机目标角度（度）。与 left_foot 镜像 |
| `left_hand` | integer | 否 | 0-180 | 45 | 左手舵机目标角度（度）。45 为自然下垂，180 为举过头顶（如有手部舵机） |
| `right_hand` | integer | 否 | 0-180 | 135 | 右手舵机目标角度（度）。135 为自然下垂，0 为举过头顶，与 left_hand 镜像 |
| `time_ms` | integer | 否 | 100-3000 | 700 | 从当前姿态过渡到目标姿态的时间（毫秒）。值越小动作越快，建议 500-1000 平滑自然 |

> 注：如实际硬件只有 4 路舵机（无手部），删除 `left_hand` / `right_hand` 两个字段即可。角度含义（中位值、正反方向）需根据实际舵机安装方向校准。

#### 使用示例

**示例 1：复位到标准直立姿态**
```json
{
  "left_leg": 90,
  "right_leg": 90,
  "left_foot": 90,
  "right_foot": 90,
  "left_hand": 45,
  "right_hand": 135,
  "time_ms": 700
}
```
场景：用户说"站好"，机器人从任意姿态回到直立。

**示例 2：举双手欢呼**
```json
{
  "left_hand": 180,
  "right_hand": 0,
  "time_ms": 500
}
```
场景：用户说"赢了，庆祝一下"，机器人快速举起双手。未传字段保持当前角度。

**示例 3：单腿前倾（准备踢球姿势）**
```json
{
  "left_leg": 70,
  "left_foot": 110,
  "right_leg": 90,
  "right_foot": 90,
  "time_ms": 1000
}
```
场景：用户说"做出踢球准备动作"，机器人左腿前倾、脚掌上翘。

**示例 4：双手合十祈祷**
```json
{
  "left_hand": 90,
  "right_hand": 90,
  "time_ms": 800
}
```
场景：用户说"祈祷一下"，机器人双手举到胸前合十（角度需按实际安装校准）。

**示例 5：编排动作序列（多次调用）**
```json
{"left_hand": 180, "right_hand": 0, "time_ms": 500}
```
随后调用：
```json
{"left_hand": 45, "right_hand": 135, "time_ms": 500}
```
场景：用户说"挥挥手"，机器人先举手再放下，形成挥手动作。LLM 通过两次 `pose` 调用编排动作序列。

#### 下游执行要求
- 解析各字段（未传字段用默认值，建议保持当前角度而非强制复位）
- 边界校验：角度限制在 0-180，时间限制在 100-3000
- 驱动对应路数舵机在 `time_ms` 时间内插值到目标角度
- 返回 JSON 结果，如 `{"success":true,"action":"pose","servos":{"left_leg":90,"right_leg":90,...}}`

---

### 4.2 P0 — `robot.action`：预定义动作（升级现有 `robot.move`）

**价值**：把现有只能 4 方向走的 `robot.move` 升级为多种动作，覆盖跳跃/转向/手部/情感/编舞。

#### 参数详细介绍

| 参数名 | 类型 | 必填 | 取值范围 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `action` | string | 是 | enum（见下表） | — | 动作名称，决定执行哪种预定义动作 |
| `steps` | integer | 否 | 1-100 | 3 | 动作重复次数。如走 3 步、跳 3 下。对部分动作（如 `home`/`stop`）无效 |
| `speed` | integer | 否 | 100-3000 | 1000 | 动作速度（毫秒周期）。**值越小越快**。100=极快，3000=极慢。建议行走 800-1200，跳跃 500-800 |
| `direction` | integer | 否 | -1 / 0 / 1 | 1 | 动作方向。1=前进/左转，-1=后退/右转，0=双手（仅手部动作用）。对不同 action 含义见下 |
| `amount` | integer | 否 | 0-170 | 25 | 动作幅度（度）。影响摆动范围，如步幅大小、跳跃高度、弯腰深度。0=最小幅度，170=最大幅度 |

**`direction` 在不同 action 中的含义**：

| action 类别 | direction=1 | direction=-1 | direction=0 |
|---|---|---|---|
| 移动类（walk/walk_backward） | 前进 | 后退 | — |
| 转向类（turn） | 左转 | 右转 | — |
| 手部类（hands_up/hands_down/hand_wave） | 左手 | 右手 | 双手 |
| 弯腰/抖腿（bend/shake_leg） | 左侧 | 右侧 | — |

#### action enum 列表（按优先级）

| 优先级 | action | 说明 | steps | speed | direction | amount | 对应 OttoClaw 函数 |
|---|---|---|---|---|---|---|---|
| 必上 | `walk` | 前进行走 | ✅ 步数 | ✅ | 1=前 | ✅ 步幅 | `otto_walk` |
| 必上 | `walk_backward` | 后退行走 | ✅ 步数 | ✅ | -1=后 | ✅ 步幅 | `otto_walk(BACKWARD)` |
| 必上 | `turn` | 原地转向 | ✅ 次数 | ✅ | 1=左/-1=右 | ✅ 幅度 | `otto_turn` |
| 必上 | `home` | 复位归位 | ❌ | ❌ | ❌ | ❌ | `otto_home` |
| 必上 | `stop` | 急停 | ❌ | ❌ | ❌ | ❌ | — |
| 建议 | `jump` | 跳跃 | ✅ 次数 | ✅ | ❌ | ❌ | `otto_jump` |
| 建议 | `sit` | 坐下 | ❌ | ❌ | ❌ | ❌ | `otto_sit` |
| 建议 | `hand_wave` | 挥手 | ❌ | ❌ | 1=左/-1=右/0=双 | ❌ | `otto_hand_wave` |
| 建议 | `hands_up` | 举手 | ❌ | ✅ | 1=左/-1=右/0=双 | ❌ | `otto_hands_up` |
| 建议 | `hands_down` | 放手 | ❌ | ✅ | 1=左/-1=右/0=双 | ❌ | `otto_hands_down` |
| 建议 | `bend` | 弯腰 | ✅ 次数 | ✅ | 1=左/-1=右 | ❌ | `otto_bend` |
| 建议 | `swing` | 摇摆 | ✅ 次数 | ✅ | ❌ | ✅ 幅度 | `otto_swing` |
| 可选 | `moonwalk` | 太空步 | ✅ 步数 | ✅ | 1=左/-1=右 | ✅ 幅度 | `otto_moonwalker` |
| 可选 | `shake_leg` | 抖腿 | ✅ 次数 | ✅ | 1=左/-1=右 | ❌ | `otto_shake_leg` |
| 可选 | `updown` | 上下运动 | ✅ 次数 | ✅ | ❌ | ✅ 幅度 | `otto_updown` |
| 可选 | `greeting` | 打招呼 | ❌ | ❌ | 1=左/-1=右 | ❌ | `otto_greeting` |
| 可选 | `shy` | 害羞 | ❌ | ❌ | 1=左/-1=右 | ❌ | `otto_shy` |
| 可选 | `radio_calisthenics` | 广播体操 | ❌ | ❌ | ❌ | ❌ | `otto_radio_calisthenics` |
| 可选 | `magic_circle` | 魔力转圈 | ❌ | ❌ | ❌ | ❌ | `otto_magic_circle` |
| 可选 | `showcase` | 表演 | ❌ | ❌ | ❌ | ❌ | `otto_showcase` |

#### 使用示例

**示例 1：前进 3 步（默认速度）**
```json
{
  "action": "walk",
  "steps": 3
}
```
场景：用户说"向前走三步"。speed 默认 1000（适中），direction 默认 1（前进），amount 默认 25（正常步幅）。

**示例 2：快速后退 5 步**
```json
{
  "action": "walk_backward",
  "steps": 5,
  "speed": 600
}
```
场景：用户说"快后退"。speed=600 表示较快，steps=5 后退 5 步。

**示例 3：左转 90 度**
```json
{
  "action": "turn",
  "steps": 1,
  "direction": 1
}
```
场景：用户说"向左转"。OttoClaw 中 turn 的 steps×90=90 度（按实现可调）。

**示例 4：慢速大步走 10 步**
```json
{
  "action": "walk",
  "steps": 10,
  "speed": 1500,
  "amount": 50
}
```
场景：用户说"大步慢走十步"。speed=1500 慢速，amount=50 大步幅。

**示例 5：跳 3 下**
```json
{
  "action": "jump",
  "steps": 3,
  "speed": 500
}
```
场景：用户说"跳三下"。speed=500 快速跳跃。

**示例 6：右手挥手**
```json
{
  "action": "hand_wave",
  "direction": -1
}
```
场景：用户说"挥挥手"。direction=-1 表示右手挥手。

**示例 7：双手举起**
```json
{
  "action": "hands_up",
  "direction": 0,
  "speed": 800
}
```
场景：用户说"举起手来"。direction=0 表示双手，speed=800 适中速度。

**示例 8：坐下**
```json
{
  "action": "sit"
}
```
场景：用户说"坐下"。无额外参数。

**示例 9：复位归位**
```json
{
  "action": "home"
}
```
场景：用户说"站好"或动作完成后自动归位。所有舵机回到中位。

**示例 10：急停**
```json
{
  "action": "stop"
}
```
场景：用户说"停下"或检测到障碍物需立即停止。立即停止所有动作。

**示例 11：害羞动作**
```json
{
  "action": "shy",
  "direction": 1,
  "steps": 2
}
```
场景：用户调侃后机器人表达害羞情绪。direction=1 左侧偏头，steps=2 重复 2 次。

**示例 12：表演节目**
```json
{
  "action": "showcase"
}
```
场景：用户说"表演个节目"。机器人执行预设的完整表演动作序列。

#### 下游执行要求
- 解析 `action` 字段，按 action 分支调用对应步态函数
- 各 action 的步态算法实现可参考 OttoClaw `main/otto/otto_movements.c`
- 非 `home`/`stop` 动作执行完后建议自动归位（参考 OttoClaw `should_auto_home_action`）
- 返回 JSON 结果，如 `{"success":true,"action":"walk","steps":3}`

**兼容性**：保留 `robot.move` / `robot.stop` 旧工具名，内部映射到 `action=walk` / `action=stop`，避免存量下游破坏。

---

### 4.3 P0 — 保留 `robot.move` / `robot.stop` / `robot.speak`（向后兼容）

**价值**：不破坏存量下游适配。

#### `robot.move` 参数详细介绍

| 参数名 | 类型 | 必填 | 取值范围 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `direction` | string | 是 | `forward` / `backward` / `left` / `right` | — | 移动方向：forward=前进，backward=后退，left=左转，right=右转 |
| `steps` | integer | 否 | 1-10 | 1 | 移动步数/次数 |

**使用示例**：

```json
{"direction":"forward","steps":2}
```
场景：用户说"向前走两步"。映射到 `robot.action`：`{"action":"walk","steps":2,"direction":1}`。

```json
{"direction":"left","steps":1}
```
场景：用户说"向左转"。映射到 `robot.action`：`{"action":"turn","steps":1,"direction":1}`。

```json
{"direction":"backward","steps":3}
```
场景：用户说"后退三步"。映射到 `robot.action`：`{"action":"walk_backward","steps":3,"direction":-1}`。

#### `robot.stop` 参数详细介绍

无参数。

**使用示例**：

```json
{}
```
场景：用户说"停下"。映射到 `robot.action`：`{"action":"stop"}`。

#### `robot.speak` 参数详细介绍

| 参数名 | 类型 | 必填 | 取值范围 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `text` | string | 是 | 任意文本（建议 <200 字） | — | 需要播报的文本内容。下游调 TTS 合成语音播放 |

**使用示例**：

```json
{"text":"你好，我是机器人小乐"}
```
场景：用户说"自我介绍"。机器人通过 TTS 播报文本。

```json
{"text":"移动完成，已到达目的地"}
```
场景：执行完 `robot.action` 后，机器人播报状态。

#### 旧工具到新工具的映射关系

| 旧工具 | 旧参数 | 新行为 | 映射后 |
|---|---|---|---|
| `robot.move` | `direction=forward` + `steps=N` | 转发 | `robot.action`：`action=walk, steps=N, direction=1` |
| `robot.move` | `direction=backward` + `steps=N` | 转发 | `robot.action`：`action=walk_backward, steps=N, direction=-1` |
| `robot.move` | `direction=left` + `steps=N` | 转发 | `robot.action`：`action=turn, steps=N, direction=1` |
| `robot.move` | `direction=right` + `steps=N` | 转发 | `robot.action`：`action=turn, steps=N, direction=-1` |
| `robot.stop` | 无 | 转发 | `robot.action`：`action=stop` |
| `robot.speak` | `text` | 保持不变 | 仍走 TTS 通道，不映射 |

---

### 4.4 P1 — `robot.play_sound`：播放预置音效（可选）

OttoClaw 没有，但对话机器人常见需求。与 `robot.speak`（TTS 合成）区别：播放固定音效文件（如鼓掌、欢呼、警报），非文字合成，响应更快、音质更好。

#### 参数详细介绍

| 参数名 | 类型 | 必填 | 取值范围 | 默认值 | 说明 |
|---|---|---|---|---|---|
| `sound` | string | 是 | enum（见下） | — | 音效名称，对应预置的音效文件 |
| `volume` | integer | 否 | 0-100 | 50 | 音量百分比。0=静音，100=最大音量 |

**sound enum 建议值**（下游按实际预置资源裁剪）：

| sound 值 | 说明 | 适用场景 |
|---|---|---|
| `applause` | 鼓掌 | 表演完成、用户表扬 |
| `cheer` | 欢呼 | 庆祝胜利 |
| `laugh` | 笑声 | 听到笑话 |
| `sad` | 悲伤 | 用户难过时共情 |
| `alert` | 警报 | 检测到异常 |
| `beep` | 蜂鸣 | 操作确认 |
| `startup` | 开机音 | 系统启动 |
| `shutdown` | 关机音 | 系统关机 |

#### 使用示例

**示例 1：播放鼓掌声**
```json
{"sound":"applause"}
```
场景：用户说"给我鼓个掌"。机器人播放鼓掌音效。

**示例 2：小音量播放笑声**
```json
{"sound":"laugh","volume":30}
```
场景：用户讲笑话后机器人轻笑。volume=30 较小音量。

**示例 3：表演后庆祝**
```json
{"sound":"cheer","volume":80}
```
场景：执行完 `robot.action` 表演动作后，播放欢呼音效庆祝。

#### 下游执行要求
- 预置音效文件存于本地存储（如 `/userdata/sounds/applause.mp3`）
- 按 `sound` 字段查找并播放对应文件
- 音量按 `volume` 字段调节（0-100 映射到硬件音量范围）
- 返回 `{"success":true,"sound":"applause","volume":50}`

---

## 5. 补齐优先级总表

| 优先级 | 工具名 | 价值 | 工作量 | 说明 |
|---|---|---|---|---|
| **P0** | `robot.pose` | 直接舵机控制，即兴姿态 | 小 | agent_core.c 加一个 CrabToolSpec + 下游解析 6 路角度 |
| **P0** | `robot.action` | 多种预定义动作，替代旧 `robot.move` | 中 | 需下游实现各动作步态函数 |
| **P0** | 保留 `robot.move`/`robot.stop`/`robot.speak` | 向后兼容 | 小 | 映射到 action / 保持 TTS |
| P1 | `robot.play_sound` | 音效播放 | 小 | 需预置音效资源 |

---

## 6. 相关代码位置参考

### 当前 agent（待改造）

| 文件 | 行 | 说明 |
|---|---|---|
| `agent_core.c` | 30-48 | 现有 3 个 robot 工具定义（需扩展） |
| `agent_core.c` | 88-93 | 工具注册（新工具在此注册） |
| `agent_core.c` | 23-27 | `stub_execute`（tool_call_cb 模式下不执行） |
| `src/kernel/kernel.c` | 624-637 | tool_call 回调下发逻辑（无需改） |
| `src/kernel/kernel.c` | 379-388 | tools JSON 渲染（无需改，自动包含新工具） |
| `include/litecrab/kernel.h` | 217-219 | 回调签名（无需改） |
| `机器人Tool协议接入指南.md` | — | 现有下游接入文档（需同步更新） |

### OttoClaw 参考实现

| 文件 | 说明 |
|---|---|
| `main/tools/tool_otto.c` | `self.otto.action` + `self.otto.pose` 完整实现（含 schema + execute） |
| `main/tools/tool_registry.c` | 工具注册表（`tool_otto_register` 调用位置） |
| `main/tools/tool_registry.h` | `ottoclaw_tool_t` 结构定义 |
| `main/otto/otto_movements.h` | 21 种动作函数声明 + 舵机索引常量（LEFT_LEG 等） |
| `main/otto/otto_movements.c` | 动作函数实现（步态算法，20KB） |
| `main/otto/oscillator.c` | 舵机振荡器底层驱动 |
| `main/otto/oscillator.h` | 振荡器结构定义 |

### xiaozhi 参考实现

| 文件 | 说明 |
|---|---|
| `core/providers/tools/device_iot/iot_executor.py` | 动态工具生成 + WebSocket 下发（`_send_iot_command`） |
| `core/providers/tools/device_iot/iot_descriptor.py` | IoT 描述符数据结构（properties + methods） |
