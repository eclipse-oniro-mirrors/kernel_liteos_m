/*
 * voice_asr.h - 阿里云 ASR 应用层(流式: 麦→文本)
 *
 * 基于 voice_ws(实例化句柄 + 二进制帧)封装 DashScope inference ASR:
 *   model: qwen-audio-3.1-asr-flash-streaming
 *   16kHz PCM 二进制帧上传 → 服务端 VAD 自动断句 → result-generated(sentence_end)
 *
 * 用法: open → 循环{feed 一帧 PCM; poll 事件} → close。
 * 长连接: 一次 open 持续 feed, 多个 FINAL(sentence_end=true)对应多轮(连续对话)。
 */
#ifndef VOICE_ASR_H
#define VOICE_ASR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct voice_asr_s voice_asr_t;

/* poll 返回的事件类型 */
typedef enum {
    VOICE_ASR_NONE,      /* 无事件(超时/二进制帧忽略) */
    VOICE_ASR_STARTED,   /* task-started: 可开始 feed 音频 */
    VOICE_ASR_PARTIAL,   /* result-generated, sentence_end=false: 中间结果 */
    VOICE_ASR_FINAL,     /* result-generated, sentence_end=true: 完整句, text_out 有文本 */
    VOICE_ASR_FINISHED,  /* task-finished */
    VOICE_ASR_FAILED,    /* task-failed */
    VOICE_ASR_CLOSED,    /* 连接关闭 */
} voice_asr_ev_t;

/*
 * 连 ASR WS + 发 run-task + 等 task-started。返回上下文, NULL 失败。
 * api_key: DashScope API key(Bearer 认证)。
 */
voice_asr_t *voice_asr_open(const char *api_key);

/*
 * 发一帧 16kHz PCM(samples 个 int16, 内部发二进制帧)。成功 0, 失败 -1。
 * 每次 feed 一帧 capture read 的量(480 样本/30ms); 服务端接受任意分块。
 */
int voice_asr_feed(voice_asr_t *asr, const short *pcm, int samples);

/*
 * poll 一个服务端事件。
 * PARTIAL/FINAL 时 text_out 填转写文本(UTF-8 中文; ASR 转写一般不含 ")。
 * timeout_ms=0 非阻塞(无事件返回 VOICE_ASR_NONE); >0 阻塞最多 timeout_ms。
 * 返回事件类型; VOICE_ASR_CLOSED/FAILED 表示连接异常, 应停止 feed 并 close。
 */
voice_asr_ev_t voice_asr_poll(voice_asr_t *asr, char *text_out, int cap, int timeout_ms);

/*
 * 发 finish-task + 关 WS + 释放上下文。asr 置 NULL。
 */
void voice_asr_close(voice_asr_t *asr);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_ASR_H */
