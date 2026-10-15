/*
 * voice_tts.c - 阿里云 TTS 应用层实现(流式: 文本→音频)
 *
 * 基于 voice_ws(实例化句柄 + 二进制帧)封装 DashScope inference TTS:
 *   run-task → 等 task-started → continue-task 发文本 → recv 二进制 24kHz PCM
 *   → task-finished 收尾。
 *
 * 协议格式见 SPEC_VOICE.md §4.2。run-task JSON 已对照 语音合成.md 原始 WS 示例核对。
 *
 * 打断(Phase 7): say() 循环每轮检查 t->cancel; cancel() 置标志 + 发 finish-task
 * directive=cancel。Phase 3 的 tts 子命令不触发 cancel, 但 cancel 函数已备好。
 *
 * JSON 解析: 无 cJSON, 手写 strstr 提取 event(字段唯一, 安全)。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "voice_ws.h"
#include "voice_tts.h"

/* 二进制 PCM 帧缓冲: TTS 服务端发大帧(实测 19200~38400B = 0.4~0.8s@24kHz)。
 * 必须 >= 最大帧, 否则 ws_recv_frame 截断丢数据(消费整帧只返回前面能装的 → 音频碎裂)。
 * 65536 可装 ~1.36s 帧, 与 voice_ws WS_RX_BUF_SIZE 对齐。 */
#define TTS_RECV_BUF     65536
#define TTS_TASK_ID_LEN  33     /* 32 hex + '\0' */
#define TTS_JSON_CAP     2048   /* run-task/continue-task/finish-task JSON(含文本) */

struct voice_tts_s {
    voice_ws_t *ws;
    char *api_key;              /* strdup'd, 重连用(open 时存, close 时 free) */
    char task_id[TTS_TASK_ID_LEN];
    volatile int cancel;        /* Phase 7: barge-in 置 1 让 say() 退出 */
    unsigned char recv_buf[TTS_RECV_BUF];
    struct timespec continue_sent;  /* continue-task 发出时刻(计时诊断 T4/T5 切分点) */
    int ws_alive;               /* 1=WS 已连可发 run-task; 0=需重连(60s idle/task-failed 后) */
};

/* ================= 内部: task_id 生成 ================= */

static void tts_gen_task_id(char *out, int cap)
{
    static unsigned long counter = 0;
    counter++;
    unsigned long t = (unsigned long)time(NULL);
    snprintf(out, cap, "%08lx%08lx%08lx%08lx",
             t, counter, t ^ counter, 0x74747375UL /* "ttus" */);
}

/* ================= 内部: JSON 提取 ================= */

static int json_event_is(const char *json, const char *name)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"event\":\"%s\"", name);
    return strstr(json, pat) != NULL;
}

/* ================= 对外接口 ================= */

/* 内部: (重)连 WS(TCP+TLS+升级)。t->ws_alive 置 1 成功/0 失败。
 * 长连接复用核心: DashScope 文档(语音合成.md "连接复用")允许一个 WS 跨多个
 * run-task/finish-task 周期, 仅 task-failed 或 60s 空闲才关。say 内按需调此重连。 */
static int tts_connect_ws(voice_tts_t *t)
{
    if (t->ws != NULL) {
        voice_ws_close(t->ws);
        t->ws = NULL;
    }
    printf("[tts] connecting ws...\n");
    t->ws = voice_ws_connect(VOICE_WS_IP, VOICE_WS_HOST, VOICE_WS_PORT,
                             VOICE_WS_PATH, t->api_key);
    if (t->ws == NULL) {
        printf("[tts] ws connect FAILED\n");
        t->ws_alive = 0;
        return -1;
    }
    t->ws_alive = 1;
    return 0;
}

/* 内部: 若 WS 不在则(重)连。say 开头调, 保证发 run-task 前连接就绪。 */
static void tts_ensure_connected(voice_tts_t *t)
{
    if (!t->ws_alive) {
        printf("[tts] ws not alive, reconnecting...\n");
        (void)tts_connect_ws(t);
    }
}

/* 内部: 发 run-task(新 task_id, 文档要求复用连接每任务 task_id 不同)+ 等 task-started。
 * 成功 0, 失败 -1(并置 ws_alive=0 让下轮重连)。从原 voice_tts_open 抽出。 */
static int tts_start_task(voice_tts_t *t)
{
    tts_gen_task_id(t->task_id, sizeof(t->task_id));

    char json[TTS_JSON_CAP];
    int jl = snprintf(json, sizeof(json),
        "{\"header\":{\"action\":\"run-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
        "\"payload\":{\"task_group\":\"audio\",\"task\":\"tts\",\"function\":\"SpeechSynthesizer\","
        "\"model\":\"qwen-audio-3.0-tts-flash\","
        "\"parameters\":{\"text_type\":\"PlainText\",\"voice\":\"longanhuan_v3.6\","
        "\"format\":\"pcm\",\"sample_rate\":24000,\"volume\":50,"
        "\"rate\":1,\"pitch\":1,\"enable_ssml\":false},\"input\":{}}}",
        t->task_id);
    if (voice_ws_send_text(t->ws, json, jl) != 0) {
        printf("[tts] send run-task FAILED\n");
        t->ws_alive = 0;
        return -1;
    }

    /* 等 task-started(最多 ~10s) */
    for (int i = 0; i < 20; i++) {
        int is_binary = 0;
        int n = voice_ws_recv(t->ws, t->recv_buf, TTS_RECV_BUF, &is_binary, 500);
        if (n < 0) {
            printf("[tts] recv task-started FAILED (conn closed)\n");
            t->ws_alive = 0;
            return -1;
        }
        if (n == 0) continue;
        if (is_binary) continue;
        if (json_event_is((const char *)t->recv_buf, "task-started")) {
            printf("[tts] task-started (task_id=%s)\n", t->task_id);
            return 0;
        }
        if (json_event_is((const char *)t->recv_buf, "task-failed")) {
            printf("[tts] task-failed on run-task: %.300s\n", t->recv_buf);
            t->ws_alive = 0;   /* 文档: task-failed 后连接不可复用 */
            return -1;
        }
    }
    printf("[tts] task-started timeout\n");
    t->ws_alive = 0;
    return -1;
}

/*
 * 连 TTS WS(仅 TCP+TLS+WS 升级, **不发 run-task**)。返回上下文, NULL 失败。
 * run-task 在 voice_tts_say 内每任务发(长连接复用: 一个 WS 跨多轮 say)。
 * api_key: DashScope API key(Bearer 认证), 内部 strdup 供重连用。
 */
voice_tts_t *voice_tts_open(const char *api_key)
{
    voice_tts_t *t = (voice_tts_t *)calloc(1, sizeof(*t));
    if (t == NULL) {
        printf("[tts] malloc ctx failed\n");
        return NULL;
    }
    t->api_key = strdup(api_key ? api_key : "");
    if (t->api_key == NULL) {
        printf("[tts] strdup api_key failed\n");
        free(t);
        return NULL;
    }
    if (tts_connect_ws(t) != 0) {
        free(t->api_key);
        free(t);
        return NULL;
    }
    return t;
}

/* JSON 字符串转义(合成文本内嵌进 continue-task JSON 用)。
 * deepseek 回复含真实换行(llm_client extract_content 把 JSON \\n 转成字面 \n),
 * raw 塞 JSON 字符串会致 JSON 非法(字面控制符在 JSON string 里不合法)→
 * 服务端关连接(实测 close code=1009)。转义 " \\ \n \r \t; 其他 <0x20 控制符丢弃;
 * UTF-8 多字节(汉字/emoji, 均 >=0x80)原样保留, JSON 合法。 */
static int tts_json_escape(const char *src, char *dst, int cap)
{
    int o = 0;
    for (; *src != '\0' && o < cap - 1; src++) {
        switch (*src) {
        case '"':  if (o + 2 >= cap) break; dst[o++] = '\\'; dst[o++] = '"';  break;
        case '\\': if (o + 2 >= cap) break; dst[o++] = '\\'; dst[o++] = '\\'; break;
        case '\n': if (o + 2 >= cap) break; dst[o++] = '\\'; dst[o++] = 'n';  break;
        case '\r': if (o + 2 >= cap) break; dst[o++] = '\\'; dst[o++] = 'r';  break;
        case '\t': if (o + 2 >= cap) break; dst[o++] = '\\'; dst[o++] = 't';  break;
        default:
            if ((unsigned char)*src >= 0x20) {
                dst[o++] = *src;
            }
            break;
        }
    }
    dst[o] = '\0';
    return o;
}

int voice_tts_say(voice_tts_t *t, const char *text,
                  void (*on_audio)(const void *pcm, int len, void *ud), void *ud)
{
    if (t == NULL || text == NULL) return -1;

    /* 长连接复用: 每轮 say 发新 run-task(新 task_id)开新任务(文档允许同一 WS 跨多任务)。
     * WS 不在(60s idle / task-failed 后被服务端关)则 tts_ensure_connected 重连。
     * run-task 失败重连一次再试(镜像 ASR 的重连兜底)。 */
    tts_ensure_connected(t);
    if (tts_start_task(t) != 0) {
        tts_ensure_connected(t);
        if (tts_start_task(t) != 0) {
            printf("[tts] start_task FAILED after reconnect, give up\n");
            return -1;
        }
    }

    /* JSON 转义合成文本: esc worst case = 2*len+1(每字符最多转义成 2 字节)。
     * json 缓冲按 esc 大小 + 包头余量 malloc, 装得下转义后文本(原栈 TTS_JSON_CAP=2048
     * 装不下 4KB 回复转义后 ~8KB)。 */
    int text_len = (int)strlen(text);
    int esc_cap = text_len * 2 + 1;
    int json_cap = esc_cap + 256;
    char *esc = (char *)malloc(esc_cap);
    char *json = (char *)malloc(json_cap);
    if (esc == NULL || json == NULL) {
        printf("[tts] malloc failed for continue-task\n");
        free(esc); free(json);
        return -1;
    }
    tts_json_escape(text, esc, esc_cap);

    /* 发 continue-task(文本帧, 带合成文本) */
    int jl = snprintf(json, json_cap,
        "{\"header\":{\"action\":\"continue-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
        "\"payload\":{\"input\":{\"text\":\"%s\"}}}",
        t->task_id, esc);
    if (jl <= 0 || jl >= json_cap) {
        printf("[tts] text too long for json buffer\n");
        free(esc); free(json);
        return -1;
    }
    if (voice_ws_send_text(t->ws, json, jl) != 0) {
        printf("[tts] send continue-task FAILED\n");
        t->ws_alive = 0;
        free(esc); free(json);
        return -1;
    }
    /* 记 continue-task 发出时刻, 供应用层算 T4(llm返回→发文本)/T5(发文本→首帧) */
    clock_gettime(CLOCK_MONOTONIC, &t->continue_sent);
    printf("[tts] continue-task sent (%d bytes json, %d text): %s\n", jl, text_len, json);
    free(esc);

    /* 收尾标记: 发 finish-task 让服务端知道文本发完, 开始合成并回 task-finished */
    int fl = snprintf(json, json_cap,
        "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
        "\"payload\":{\"input\":{}}}", t->task_id);
    if (voice_ws_send_text(t->ws, json, fl) != 0) {
        printf("[tts] send finish-task FAILED\n");
        t->ws_alive = 0;
        free(json);
        return -1;
    }
    free(json);

    /* recv 循环: 二进制帧→on_audio 回调, 文本帧→解 event */
    int total_audio = 0;
    for (;;) {
        if (t->cancel) {
            printf("[tts] cancelled by barge-in\n");
            /* cancel() 已发 cancel finish-task, 此处直接退出 */
            return 0;
        }
        int is_binary = 0;
        int n = voice_ws_recv(t->ws, t->recv_buf, TTS_RECV_BUF, &is_binary, 30000);
        if (n < 0) {
            printf("[tts] recv FAILED (conn closed), got %d bytes audio\n", total_audio);
            t->ws_alive = 0;
            return -1;
        }
        if (n == 0) {
            /* 超时无帧: 正常合成中或已结束, 继续等 */
            continue;
        }
        if (is_binary) {
            total_audio += n;
            if (on_audio) {
                on_audio(t->recv_buf, n, ud);
            }
            continue;
        }
        /* TEXT 帧, 解析 event */
        if (json_event_is((const char *)t->recv_buf, "task-finished")) {
            printf("[tts] task-finished, total audio %d bytes\n", total_audio);
            return 0;
        }
        if (json_event_is((const char *)t->recv_buf, "task-failed")) {
            printf("[tts] task-failed: %.300s\n", t->recv_buf);
            t->ws_alive = 0;   /* 文档: task-failed 后连接不可复用 */
            return -1;
        }
        /* 其他事件忽略 */
    }
}

/* 取 continue-task 发出时刻(voice_tts_say 内部记)。out 非 NULL 时写入。
 * 供应用层算 T4/T5 计时分段。t 为 NULL 或 say 未发到 continue-task 时写零。 */
void voice_tts_get_continue_sent(const voice_tts_t *t, struct timespec *out)
{
    if (t != NULL && out != NULL) {
        *out = t->continue_sent;
    } else if (out != NULL) {
        out->tv_sec = 0;
        out->tv_nsec = 0;
    }
}

int voice_tts_cancel(voice_tts_t *t)
{
    if (t == NULL || t->ws == NULL) return -1;
    t->cancel = 1;
    /* 发 finish-task directive=cancel 让服务端停合成 */
    char json[TTS_JSON_CAP];
    int jl = snprintf(json, sizeof(json),
        "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},"
        "\"payload\":{\"input\":{\"directive\":\"cancel\"}}}", t->task_id);
    if (voice_ws_send_text(t->ws, json, jl) != 0) {
        printf("[tts] send cancel FAILED\n");
        return -1;
    }
    printf("[tts] cancel sent (directive=cancel)\n");
    return 0;
}

void voice_tts_close(voice_tts_t *t)
{
    if (t == NULL) return;
    if (t->ws != NULL) {
        voice_ws_close(t->ws);
    }
    free(t->api_key);
    free(t);
    printf("[tts] closed\n");
}
