/*
 * voice_tts.h - 阿里云 TTS 应用层(流式: 文本→音频)
 *
 * 基于 voice_ws(实例化句柄 + 二进制帧)封装 DashScope inference TTS:
 *   model: qwen-audio-3.0-tts-flash
 *   continue-task 发文本 → 收二进制 24kHz PCM 帧 → 喇叭播放
 *
 * 用法: open → say(文本, on_audio 回调) → close。
 * 打断(Phase 7): say 期间另一线程调 cancel, say 循环检测 cancel 标志提前退出。
 */
#ifndef VOICE_TTS_H
#define VOICE_TTS_H

#include <stddef.h>
#include <time.h>   /* struct timespec(voice_tts_get_continue_sent 计时诊断用) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct voice_tts_s voice_tts_t;

/*
 * 连 TTS WS(仅 TCP+TLS+WS 升级, **不发 run-task**)。返回上下文, NULL 失败。
 * run-task 在 voice_tts_say 内每任务发(长连接复用: 一个 WS 跨多轮 say, 省每轮 TLS 握手)。
 * WS 被服务端关(60s idle / task-failed)时 say 内自动重连。
 * api_key: DashScope API key(Bearer 认证), 内部 strdup 供重连用。
 */
voice_tts_t *voice_tts_open(const char *api_key);

/*
 * run-task(新 task_id) + 等 task-started + continue-task 发文本 + recv 二进制音频循环。
 * 每收到一段 24kHz PCM 调 on_audio(pcm, len, ud)(pcm 指向内部 recv 缓冲, 回调内需立即消费或拷走)。
 * 循环到 task-finished(正常结束)或 t->cancel 被置 1(打断)。
 * WS 复用: task-finished 后 WS 保持开, 下轮 say 发新 run-task(文档连接复用)。
 * 返回 0 正常结束, <0 错误/连接关闭(下轮 say 自动重连)。
 */
int voice_tts_say(voice_tts_t *t, const char *text,
                  void (*on_audio)(const void *pcm, int len, void *ud), void *ud);

/*
 * 打断取消(Phase 7 barge-in): 发 finish-task directive=cancel。
 * 置 t->cancel=1 让正在跑的 say() 循环退出。成功 0, 失败 -1。
 */
int voice_tts_cancel(voice_tts_t *t);

/* 发 finish-task(正常收尾) + 关 WS + 释放上下文。t 置 NULL。 */
void voice_tts_close(voice_tts_t *t);

/* 取 continue-task 发出时刻(voice_tts_say 内部记), 供应用层算 T4/T5 计时分段。
 * out 非 NULL 时写入(t 为 NULL 或 say 未发到 continue-task 时写零)。 */
void voice_tts_get_continue_sent(const voice_tts_t *t, struct timespec *out);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_TTS_H */
