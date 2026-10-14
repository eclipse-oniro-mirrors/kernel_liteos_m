#ifndef VOICE_SERVICE_H
#define VOICE_SERVICE_H
/*
 * voice_service.h - 语音唤醒 + 三段式对话(ASR → agent → TTS)服务
 * 依赖: wake_sdk 的 wake_audio_core(采集/播放/唤醒词, 经 BUILD.gn dep)
 *       + voice_asr/voice_tts(阿里云 inference WS, 本目录源码)
 *       + wifi_service(在线检查) + agent_service(中段智能体 + 指令分发)
 *
 * 功能: 后台线程跑 唤醒"Luna" <-> 三段式对话 循环(开机自启, voice stop 可停):
 *   1. ASR:   16kHz PCM → 阿里云 DashScope inference 流式识别(voice_asr)
 *   2. agent: 转写文本 → AgentSvcChatVoice(能力表+deepseek);
 *             原始回复整段串口打印(未解析), [EMOJI]/[ACT] 经能力表分发 ——
 *             与串口 chat 同一套校验、表情文件通道、动作队列
 *   3. TTS:   [SAY] 文本 → 阿里云 inference TTS(voice_tts) → 喇叭
 *
 * 注意: 独占麦克风/喇叭 —— 与 my_wake 程序二选一运行; 首次启动初始化约数秒。
 * 后续接入方法: include 本头文件, 调 VoiceSvcStart/Stop 即可;
 * shell 新指令在 voice_service.c 加 VoiceCmdXxx, 声明放这里, 主程序命令表绑定。
 */

/* ============【语音配置区】============ */
#define VOICE_CFG_WAKE_WORD  "Luna"          /* 唤醒词(模型内置, 仅展示用) */

/* 阿里云 DashScope API key(ASR/TTS inference WS 共用, Bearer 认证;
 * 端点见 voice_ws.h 的 VOICE_WS_* 常量) */
#define VOICE_CFG_QWEN_KEY   "sk-ws-H.PIPHLHL.jG0u.MEUCIQDHrsS_H86Sa7dOmMlBWpR1gcd3baedf7WO6wZcwI5r0QIgI9XxbFBm9ki7f3g7cv2Kx4FuFqWFshwbc2uf3fj3vxk"

/* 唤醒后设备主动问候语(TTS 直播, 不走 agent) */
#define VOICE_CFG_GREETING   "你好"

/* 回声能量门(dBFS): 三段式已改用 drain 回声抑制(每轮 LISTEN 前丢 1.2s 帧),
 * 此门控当前未用, 参数保留兼容 VoiceDialogLoop 签名 */
#define VOICE_CFG_GATE_DBFS  (-18.0f)

/* 对话期麦克风增益 0~0x7e; 0=用唤醒校准值(WakeCaptureDefaultMicVol) */
#define VOICE_CFG_MIC_VOL    0

/* 播放音量 0~100(软件数字缩放, 100=原始电平): 云端"声音大点"FC / 串口 vol 运行时改 */
#define VOICE_CFG_PLAY_VOL   10

/* 待机收尾提示音(24kHz/16bit/mono 裸 PCM, userfs data/voice/bye.pcm):
 * 对话空闲超时回唤醒监听前本地播放"下次再聊"。
 * 文件缺失/读失败则跳过提示, 直接回待机 */
#define VOICE_CFG_BYE_PCM    "/storage/data/voice/bye.pcm"

/* 云端助手人设/守门规则(session.update 用; 禁含双引号, 拼进 JSON 字符串):
 * 动作/表情/音量意图才调 control_robot(枚举来自 agent_service 能力表), 闲聊直接语音答;
 * 用户说休息/待机/拜拜等要结束对话时调 go_standby(模型道别后本端立即回待机) */
#define VOICE_CFG_INSTRUCTIONS \
    "你是装在六足机器人上的语音助手Luna。用户说话时先判断意图:若用户明确要求做肢体动作(如走/转身/挥手/跳舞/坐下等)或切换表情(如开心点/难过/惊讶等),调用control_robot工具,action和expression从枚举里选最贴合的,turn和walk可以用dir表示方向;用户要求调大/调小/设置音量(如'声音大点''小声点''音量50')时,调用control_robot并填volume(0~100整数,调大在当前基础上+30,调小-30,用户说了具体数字就填那个数),不要只口头答应;没要求的字段不填,整体没要求就不要调用工具,禁止自作主张补操作。若用户表达想结束对话或让你休息(如说休息/待机/睡吧/先这样吧/拜拜等),必须调用go_standby工具,然后用一句话简短自然地道别即可,不要再提问也不要延续任何话题。调用工具后等结果,用一句话自然地确认你做了什么,不要念英文名,不要提工具。若是问答/闲聊/知识/天气/时间/讲故事等,不要调用工具,直接自己语音回答。语气口语化,不输出emoji和Markdown。"

/* 对话空闲超时(秒): 无任何云端事件就播告别语断开回唤醒监听 */
#define VOICE_CFG_IDLE_SEC   180

/* 启动(幂等): 起后台线程跑 唤醒<->对话 循环; 首次做采集/播放/唤醒词初始化,
 * 已初始化则复用。返回 0=已启动/在跑, -1=线程创建失败(详见日志) */
int VoiceSvcStart(void);

/* 停止: 请求退出当前对话/监听(线程保留待命, 设备不反初始化, 再 start 复用)。
 * 返回 0=已请求, -1=本就没在跑 */
int VoiceSvcStop(void);

/* 循环是否运行中(1=唤醒监听或对话中, 0=待命/未启动) */
int VoiceSvcIsRunning(void);

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int VoiceCmdVoice(int argc, char **argv);   /* voice start|stop|status */
int VoiceCmdVol(int argc, char **argv);     /* vol [0~100] 播放音量 */

#endif /* VOICE_SERVICE_H */
