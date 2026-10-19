/* robot_tools.c — 机器人舵机工具注册表 (从 agent_core.c 迁出)
 *
 * 本文件集中定义所有"机器人/舵机类"工具的 CrabToolSpec:
 *   - robot.action     21 种 OttoClaw 预定义动作 + home + stop (共 23 种)
 *   - robot.pose       6 路舵机直接角度控制 (LLM 即兴姿态)
 *   - robot.play_sound 播放预置音效
 *   - robot.stop       急停 (独立入口, 等价 action=stop 但语义更明确)
 *
 * 已删除的重复工具:
 *   - robot.move  (被 robot.action 的 walk/walk_backward/turn 完全覆盖)
 *   - robot.speak (LLM 最终回复已走 on_tts 回调播报, 无需工具调用)
 *
 * 所有工具的 execute 都是 robot_stub_execute: 仅返回 "relayed to external executor"。
 * 真正执行通过 AgentToolCallCallback 回调下发到下游舵机模块 (见 kernel.c:624)。
 *
 * 参考文档: Agent_Tool补齐分析报告.md
 */
#include "litecrab/runtime.h"
#include "litecrab/kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 桩 handler — 工具调用通过 tool_call_cb 回调下发到下游舵机执行。
 * 当设置了 tool_call_cb 后, kernel.c 不调此 handler, 而是直接调回调。
 * 此 handler 仅作为 CrabToolSpec.execute 的占位 (registry 要求非空)。 */
static int robot_stub_execute(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void)r; (void)c;
    CrabRawResultSetStdout(raw, "tool relayed to external executor");
    return 0;
}

/* =========================================================================
 *  robot.action — 预定义动作 (21 种 OttoClaw 动作 + home + stop = 23 种)
 * =========================================================================
 *  参考报告 4.2 节。对应 OttoClaw `self.otto.action` 工具。
 *
 *  OttoClaw 21 种动作 (按报告 2.1 节分类):
 *    移动 4: walk / walk_backward / turn / moonwalk
 *    姿态 5: bend / shake_leg / sit / updown / swing
 *    跳跃 2: jump / takeoff
 *    手部 3: hands_up / hands_down / hand_wave
 *    花式 2: windmill / fitness
 *    情感 2: greeting / shy
 *    编舞 3: radio_calisthenics / magic_circle / showcase
 *  另加 home (复位归位) + stop (急停) 共 23 个 enum 值。
 */
static const char* const kActionEnum[] = {
    /* 移动类 */
    "walk", "walk_backward", "turn", "moonwalk",
    /* 姿态类 */
    "bend", "shake_leg", "sit", "updown", "swing",
    /* 跳跃类 */
    "jump", "takeoff",
    /* 手部类 */
    "hands_up", "hands_down", "hand_wave",
    /* 花式类 */
    "windmill", "fitness",
    /* 情感类 */
    "greeting", "shy",
    /* 编舞类 */
    "radio_calisthenics", "magic_circle", "showcase",
    /* 复位/急停 (非 OttoClaw 21 动作, 但作为 action 入口) */
    "home", "stop",
};
#define kActionEnumCount (sizeof(kActionEnum) / sizeof(kActionEnum[0]))

static const CrabToolParamSpec action_params[] = {
    {"action",
     "predefined robot action name. walk/walk_backward/turn/moonwalk(move), "
     "bend/shake_leg/sit/updown/swing(pose), jump/takeoff(jump), "
     "hands_up/hands_down/hand_wave(hand), windmill/fitness(fancy), "
     "greeting/shy(emotion), radio_calisthenics/magic_circle/showcase(choreo), "
     "home(reset)/stop(emergency)",
     CRAB_TOOL_VALUE_STRING, 1, 0, {0}, 0, 0,
     kActionEnum, kActionEnumCount},
    {"steps",
     "repeat count for the action (e.g. walk 3 steps, jump 3 times). "
     "ignored by home/stop. default 3, range 1-100",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 1, 100, NULL, 0},
    {"speed",
     "action cycle period in ms. smaller=faster. 100=very fast, 3000=very slow. "
     "default 1000, range 100-3000",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 100, 3000, NULL, 0},
    {"direction",
     "action direction. 1=forward/left, -1=backward/right, 0=both hands. "
     "meaning varies by action class. default 1, range -1..1",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, -1, 1, NULL, 0},
    {"amount",
     "action amplitude in degrees. affects swing range (step size, jump height, "
     "bend depth). default 25, range 0-170",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 170, NULL, 0},
};

/* =========================================================================
 *  robot.pose — 6 路舵机直接角度控制 (LLM 即兴姿态)
 * =========================================================================
 *  参考报告 4.1 节。对应 OttoClaw `self.otto.pose` 工具。
 *  6 路舵机: left_leg / right_leg / left_foot / right_foot / left_hand / right_hand
 *  加 time_ms 过渡时间。未传字段下游应保持当前角度 (不强制复位)。
 */
static const CrabToolParamSpec pose_params[] = {
    {"left_leg",
     "left leg servo target angle in degrees. 90=neutral(stand). <90=lean forward, "
     ">90=lean back. default 90, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"right_leg",
     "right leg servo target angle in degrees. 90=neutral. mirror of left_leg. "
     "default 90, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"left_foot",
     "left foot servo target angle in degrees. controls foot tilt, affects step "
     "length and balance. default 90, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"right_foot",
     "right foot servo target angle in degrees. mirror of left_foot. "
     "default 90, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"left_hand",
     "left hand servo target angle in degrees. 45=natural hang, 180=raised overhead "
     "(if hand servo exists). default 45, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"right_hand",
     "right hand servo target angle in degrees. 135=natural hang, 0=raised overhead. "
     "mirror of left_hand. default 135, range 0-180",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 180, NULL, 0},
    {"time_ms",
     "transition time from current pose to target pose in milliseconds. smaller=faster. "
     "recommend 500-1000 for smooth motion. default 700, range 100-3000",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 100, 3000, NULL, 0},
};

/* =========================================================================
 *  robot.stop — 急停 (等价 action=stop, 但作为独立入口语义更明确)
 * ========================================================================= */
/* 无参数工具: params=NULL, paramCount=0 */

/* =========================================================================
 *  robot.play_sound — 播放预置音效 (P1 可选)
 * =========================================================================
 *  参考报告 4.4 节。OttoClaw 没有, 但对话机器人常见需求。
 *  与 robot.speak 区别: 播放固定音效文件 (非 TTS 合成)。
 */
static const char* const kSoundEnum[] = {
    "applause", "cheer", "laugh", "sad",
    "alert", "beep", "startup", "shutdown",
};
static const CrabToolParamSpec play_sound_params[] = {
    {"sound",
     "preset sound effect name: applause/cheer/laugh/sad/alert/beep/startup/shutdown",
     CRAB_TOOL_VALUE_STRING, 1, 0, {0}, 0, 0,
     kSoundEnum, 8},
    {"volume",
     "volume percentage. 0=mute, 100=max. default 50, range 0-100",
     CRAB_TOOL_VALUE_INT, 0, 1, {0}, 0, 100, NULL, 0},
};

/* =========================================================================
 *  工具注册表
 * =========================================================================
 *  4 个工具: action / pose / play_sound / stop
 *  - action 覆盖所有移动/姿态/跳跃/手部/花式/情感/编舞/复位动作
 *  - pose 提供直接舵机角度控制 (action 无法表达的即兴姿态)
 *  - play_sound 播放固定音效 (与 TTS 区别: 非合成, 固定文件)
 *  - stop 急停独立入口 (语义比 action=stop 更明确)
 *  已删除: robot.move (被 action 覆盖) / robot.speak (LLM 回复走 on_tts)
 */
static const CrabToolSpec robot_tools[] = {
    {"robot.action",
     "Execute a predefined robot action. Supports 21 OttoClaw actions (walk, "
     "turn, jump, sit, hand_wave, bend, swing, moonwalk, greeting, shy, "
     "radio_calisthenics, showcase, etc) plus home (reset) and stop (emergency).",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, CRAB_TOOL_RISK_LOW,
     action_params, sizeof(action_params) / sizeof(action_params[0]),
     robot_stub_execute, CrabBasicFilter},
    {"robot.pose",
     "Directly set target angles for all 6 servos (left/right leg/foot/hand) with "
     "a transition time. Lets the LLM compose arbitrary poses (cheer, pray, hug, "
     "kick-stance, dance pose) by calling multiple times. Unset fields should be "
     "kept at current angle by the downstream servo module.",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, CRAB_TOOL_RISK_LOW,
     pose_params, sizeof(pose_params) / sizeof(pose_params[0]),
     robot_stub_execute, CrabBasicFilter},
    {"robot.play_sound",
     "Play a preset sound effect file (not TTS). Faster and higher quality than "
     "robot.speak for fixed sounds like applause/cheer/alert. Downstream must have "
     "preset sound files.",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, CRAB_TOOL_RISK_LOW,
     play_sound_params, sizeof(play_sound_params) / sizeof(play_sound_params[0]),
     robot_stub_execute, CrabBasicFilter},
    {"robot.stop",
     "Emergency stop. Immediately halts all current robot motions. Equivalent to "
     "robot.action with action=stop but provided as a dedicated entry point for "
     "clearer semantics.",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, CRAB_TOOL_RISK_LOW,
     NULL, 0,
     robot_stub_execute, CrabBasicFilter},
};

int CrabRuntimeRegisterRobotTools(CrabRuntime* r) {
    if (!r)
        return CRAB_ERROR_INVALID_ARG;
    for (size_t i = 0; i < sizeof(robot_tools) / sizeof(robot_tools[0]); i++) {
        int rc = CrabRegistryRegister(&r->registry, &robot_tools[i]);
        if (rc) {
            printf("[robot_tools] register '%s' failed: %d\n", robot_tools[i].name, rc);
            return rc;
        }
    }
    printf("[robot_tools] registered %zu robot tools (%zu actions in robot.action enum)\n",
           sizeof(robot_tools) / sizeof(robot_tools[0]), kActionEnumCount);
    return 0;
}

/* 返回 robot.action 工具支持的动作数量 (供测试/调试用) */
size_t CrabRobotActionCount(void) {
    return kActionEnumCount;
}

/* 返回已注册的 robot 工具数量 (供测试/调试用) */
size_t CrabRobotToolsCount(void) {
    return sizeof(robot_tools) / sizeof(robot_tools[0]);
}
