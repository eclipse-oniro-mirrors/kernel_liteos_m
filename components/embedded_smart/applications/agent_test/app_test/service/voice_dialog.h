#ifndef VOICE_DIALOG_H
#define VOICE_DIALOG_H
/*
 * voice_dialog.h - 语音唤醒 + 三段式对话(ASR → agent → TTS)(voice_service 执行体)
 *
 * 喊 VOICE_CFG_WAKE_WORD("Luna") 唤醒后进入对话。参照
 * device/soc/hisilicon/common/platform/mytest/voice 的分离式实现, 链路为:
 *   1. ASR  段: 16kHz PCM → 阿里云 DashScope inference WS 流式识别(voice_asr)
 *   2. agent 段: 转写文本 → AgentSvcChatVoice(能力表 + deepseek);
 *               原始回复整段串口打印(未解析, 供人工检查),
 *               [SAY]/[EMOJI]/[ACT] 解析分发与串口 chat 同一规则
 *   3. TTS  段: [SAY] 文本 → 阿里云 inference TTS(voice_tts) → 24kHz PCM → 喇叭
 *
 * 回声抑制(drain): 单线程顺序 —— agent/TTS 期间不喂 ASR; 每轮 LISTEN 前
 * 丢弃 40 帧(1.2s)清 VB 回声, AO 尾部下轮自然排空。
 * 唤醒监听/启停控制/播放音量接口与旧实现(阿里云 Qwen 实时对话)一致。
 *
 * 前提: VoiceDialogLoop 调用前 采集(WakeCaptureInit)/播放(WakePlaybackInit)/
 * 唤醒词(wake_word_init) 已由 voice_service 初始化好(循环退出不反初始化,
 * 复用设备, 重启无需重初始化)。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * (重新)进入 唤醒↔对话 循环前调用: 复位循环标志与唤醒词状态。
 * 每次 VoiceDialogLoop 前必须调用一次。
 */
void VoiceDialogArm(void);

/*
 * 唤醒→对话主循环(阻塞): 监听唤醒词 → 对话会话(空闲超时回监听) → 循环,
 * 直到 VoiceDialogRequestStop 被调用。返回 0 正常退出。
 * gate_dbfs 参数保留(三段式用 drain 回声抑制, 当前未用)。
 */
int VoiceDialogLoop(float gate_dbfs, unsigned int mic_vol);

/* 线程安全: 请求退出循环 —— 监听中立即返回待命, 对话中收尾 ASR/TTS 连接。 */
void VoiceDialogRequestStop(void);

/*
 * 播放音量(软件数字缩放): 0=静音 100=原始电平(默认), 越界自动 clamp,
 * 返回实际值。运行中立即生效(下一写入块起); 串口 vol 命令走这里。
 */
int VoiceDialogSetVol(int vol);
int VoiceDialogGetVol(void);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_DIALOG_H */
