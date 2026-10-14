/*
 * voice_asr.c - 阿里云 ASR 应用层实现(流式: 麦→文本)
 *
 * 基于 voice_ws(实例化句柄 + 二进制帧)封装 DashScope inference ASR:
 *   run-task → 等 task-started → feed 二进制 PCM → recv result-generated
 *   → sentence_end=true 即完整句 → finish-task 收尾。
 *
 * JSON 解析: 无 cJSON 依赖, 手写 strstr 提取 event/text/sentence_end。
 *   - event 字段唯一("event":"xxx"), strstr 安全。
 *   - text 字段: ASR 转写为 UTF-8 中文, 一般不含 ", 简单提取到下一个 " 即可。
 *
 * 协议格式见 SPEC_VOICE.md §4.1 + Phase 1 板级验证(task-started 已跑通)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "voice_ws.h"
#include "voice_asr.h"

#define ASR_RECV_BUF    8192   /* 内部 recv 缓冲(转写长句 JSON 可达 ~6KB, 4096 会截断) */
#define ASR_TASK_ID_LEN 33     /* 32 hex + '\0' */
#define ASR_JSON_CAP    1024   /* run-task / finish-task JSON */

struct voice_asr_s {
    voice_ws_t *ws;
    char task_id[ASR_TASK_ID_LEN];
    char recv_buf[ASR_RECV_BUF];
};

/* ================= 内部: task_id 生成 ================= */

/* 生成 32 位十六进制 task_id(板上无 uuid; DashScope 仅要求唯一)。
 * 用 time(NULL)+counter, 避免 gettimeofday 的 syscall 256 警告(LiteOS 无害但烦)。 */
static void asr_gen_task_id(char *out, int cap)
{
    static unsigned long counter = 0;
    counter++;
    unsigned long t = (unsigned long)time(NULL);
    snprintf(out, cap, "%08lx%08lx%08lx%08lx",
             t, counter, t ^ counter, 0x766f6963UL /* "voic" */);
}

/* ================= 内部: JSON 提取(手写 strstr) ================= */

/* 判断 JSON 是否含指定 event 值: "event":"<name>" */
static int json_event_is(const char *json, const char *name)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"event\":\"%s\"", name);
    return strstr(json, pat) != NULL;
}

/* 从 JSON 提取 "text":"..." 的值到 out。返回文本长度, <0 未找到/为空。 */
static int json_extract_text(const char *json, char *out, int cap)
{
    const char *p = strstr(json, "\"text\"");
    if (p == NULL) return -1;
    p = strchr(p + 6, ':');
    if (p == NULL) return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    int i = 0;
    /* 拷到下一个未转义的 " 为止。ASR 转写一般不含 ", 简单处理即可。 */
    while (*p != '\0' && *p != '"' && i < cap - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i;
}

/* 判断 JSON 是否含 "sentence_end":true */
static int json_sentence_end_true(const char *json)
{
    const char *p = strstr(json, "\"sentence_end\"");
    if (p == NULL) return 0;
    p = strchr(p + 14, ':');
    if (p == NULL) return 0;
    p++;
    while (*p == ' ') p++;
    return (strncmp(p, "true", 4) == 0);
}

/* ================= 对外接口 ================= */

voice_asr_t *voice_asr_open(const char *api_key)
{
    voice_asr_t *asr = (voice_asr_t *)calloc(1, sizeof(*asr));
    if (asr == NULL) {
        printf("[asr] malloc ctx failed\n");
        return NULL;
    }

    printf("[asr] connecting ws...\n");
    asr->ws = voice_ws_connect(VOICE_WS_IP, VOICE_WS_HOST, VOICE_WS_PORT,
                               VOICE_WS_PATH, api_key);
    if (asr->ws == NULL) {
        printf("[asr] ws connect FAILED\n");
        free(asr);
        return NULL;
    }

    asr_gen_task_id(asr->task_id, sizeof(asr->task_id));

    /* 发 run-task(文本帧), 触发服务端回 task-started */
    char json[ASR_JSON_CAP];
    int jl = snprintf(json, sizeof(json),
        "{\"header\":{\"action\":\"run-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
        "\"payload\":{\"task_group\":\"audio\",\"task\":\"asr\",\"function\":\"recognition\","
        "\"model\":\"qwen-audio-3.1-asr-flash-streaming\","
        "\"parameters\":{\"sample_rate\":16000,\"format\":\"pcm\"},\"input\":{}}}",
        asr->task_id);
    if (voice_ws_send_text(asr->ws, json, jl) != 0) {
        printf("[asr] send run-task FAILED\n");
        voice_ws_close(asr->ws);
        free(asr);
        return NULL;
    }

    /* 等 task-started(最多 ~10s) */
    for (int i = 0; i < 20; i++) {
        int is_binary = 0;
        int n = voice_ws_recv(asr->ws, asr->recv_buf, ASR_RECV_BUF, &is_binary, 500);
        if (n < 0) {
            printf("[asr] recv task-started FAILED (conn closed)\n");
            voice_ws_close(asr->ws);
            free(asr);
            return NULL;
        }
        if (n == 0) continue;
        if (is_binary) continue;  /* 不该有二进制, 忽略 */
        if (json_event_is(asr->recv_buf, "task-started")) {
            printf("[asr] task-started (task_id=%s)\n", asr->task_id);
            return asr;
        }
        if (json_event_is(asr->recv_buf, "task-failed")) {
            printf("[asr] task-failed on run-task: %.300s\n", asr->recv_buf);
            voice_ws_close(asr->ws);
            free(asr);
            return NULL;
        }
        /* 其他事件(罕见), 继续 drain */
    }
    printf("[asr] task-started timeout\n");
    voice_ws_close(asr->ws);
    free(asr);
    return NULL;
}

int voice_asr_feed(voice_asr_t *asr, const short *pcm, int samples)
{
    if (asr == NULL || asr->ws == NULL || pcm == NULL || samples <= 0) return -1;
    /* 发二进制 PCM 帧(裸字节, samples*2 字节) */
    return voice_ws_send_binary(asr->ws, pcm, samples * (int)sizeof(short));
}

voice_asr_ev_t voice_asr_poll(voice_asr_t *asr, char *text_out, int cap, int timeout_ms)
{
    if (asr == NULL || asr->ws == NULL) return VOICE_ASR_CLOSED;
    if (text_out && cap > 0) text_out[0] = '\0';

    int is_binary = 0;
    int n = voice_ws_recv(asr->ws, asr->recv_buf, ASR_RECV_BUF, &is_binary, timeout_ms);
    if (n < 0) return VOICE_ASR_CLOSED;
    if (n == 0) return VOICE_ASR_NONE;
    if (is_binary) {
        /* ASR 不该回二进制帧, 收到忽略 */
        return VOICE_ASR_NONE;
    }

    /* TEXT 帧, 解析 JSON event */
    if (json_event_is(asr->recv_buf, "task-started")) {
        return VOICE_ASR_STARTED;
    }
    if (json_event_is(asr->recv_buf, "task-finished")) {
        return VOICE_ASR_FINISHED;
    }
    if (json_event_is(asr->recv_buf, "task-failed")) {
        printf("[asr] task-failed: %.300s\n", asr->recv_buf);
        return VOICE_ASR_FAILED;
    }
    if (json_event_is(asr->recv_buf, "result-generated")) {
        int end = json_sentence_end_true(asr->recv_buf);
        if (text_out && cap > 0) {
            (void)json_extract_text(asr->recv_buf, text_out, cap);
        }
        return end ? VOICE_ASR_FINAL : VOICE_ASR_PARTIAL;
    }
    /* 未知事件, 忽略 */
    return VOICE_ASR_NONE;
}

void voice_asr_close(voice_asr_t *asr)
{
    if (asr == NULL) return;
    if (asr->ws != NULL) {
        /* 发 finish-task 收尾(礼貌关闭; 失败忽略) */
        char json[ASR_JSON_CAP];
        int jl = snprintf(json, sizeof(json),
            "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
            "\"payload\":{\"input\":{}}}", asr->task_id);
        (void)voice_ws_send_text(asr->ws, json, jl);
        voice_ws_close(asr->ws);
    }
    free(asr);
    printf("[asr] closed\n");
}
