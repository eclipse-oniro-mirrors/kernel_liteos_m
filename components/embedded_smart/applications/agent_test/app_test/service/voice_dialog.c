/*
 * voice_dialog.c - 语音唤醒 + 三段式对话(ASR → agent → TTS)(app_test 集成版)
 *
 * 参照 device/soc/hisilicon/common/platform/mytest/voice 的分离式实现,
 * 替换原"阿里云 Qwen-Audio-Realtime 一体化"链路(Qwen 语音+理解+合成一条 WS):
 *
 *   1. ASR  段: 本地 16kHz 采集 → 阿里云 DashScope inference WS 流式识别
 *               (voice_asr: run-task → 二进制 PCM 帧 → result-generated,
 *                sentence_end=true 即一句完整转写)
 *   2. agent 段: ASR 文本 → agent_input(同事 agent 组件, LLM tool 循环)
 *               → 【最终回复整段串口打印(人工检查用)】
 *               → 动作经 tool_call 桥映射本包能力表分发; 回复整段送 TTS
 *   3. TTS  段: 回复文本 → 阿里云 inference TTS(voice_tts)
 *               → 24kHz PCM 流式回调 → 喇叭(24k→16k 重采样)
 *
 * 回声抑制(drain, 同 mytest/voice): 单线程顺序 —— agent/TTS 期间不喂 ASR
 *   (ASR 不转录回声); 每轮 LISTEN 前丢弃 40 帧(1.2s)清 VB 堆积回声,
 *   AO 尾部在下轮 drain 期间自然排空。
 *
 * 与旧实现的对应关系: 唤醒监听(wake_listen_loop)/启停控制/播放音量/字幕通道
 * 保持不变; 三线程实时对话/云端 FC/go_standby/在线告别语 随一体化链路移除,
 * 空闲超时改播本地 bye.pcm 后回唤醒监听。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "voice_dialog.h"
#include "voice_service.h"
#include "wake_capture.h"
#include "wake_playback.h"
#include "wake_word.h"
#include "voice_ws.h"
#include "voice_asr.h"
#include "voice_tts.h"
#include "agent/agent.h"
#include "agent_service.h"
#include "llm_service.h"
#include "servo_service.h"
#include "wifi_service.h"

/* ---- 三段式缓冲/参数 ---- */
#define DIALOG_ASR_TEXT_CAP   512    /* ASR 转写文本缓冲 */
#define DIALOG_LLM_RAW_CAP    4096   /* agent 原始回复缓冲(与 LLM 回复上限一致) */
#define DIALOG_SAY_CAP        512    /* [SAY] 文本缓冲(供 TTS) */
#define DIALOG_ECHO_DRAIN_FRAMES 40  /* 每轮 LISTEN 前丢弃帧数: 40*30ms = 1.2s */
#define DIALOG_ECHO_REPLY_CAP 512    /* 回声比对用的"机器人刚说过的话"缓存 */
#define DIALOG_FRAMES_PER_SEC 33     /* 30ms/帧, 每秒一次电平日志 */
#define DIALOG_TAIL_DRAIN_MS  1200   /* 问候语播完后等 AO 尾部排空(固定兜底) */

/* 唤醒监听诊断参数(同旧实现) */
#define WAKE_LISTEN_LOG_FRAMES  33  /* 33*30ms ≈ 1s */
#define WAKE_STEPS_PER_READ     3   /* 每帧 480 点喂 3 次(每次 160 点@10ms) */

/* ASR 长连接断线重连兜底(同 mytest/voice VoiceRunDialog) */
#define DIALOG_ASR_OPEN_TRIES 3

/* ---- 全局状态 ---- */
static volatile int g_flow_running = 1;  /* 0 = VoiceDialogLoop 主循环退出 */

/* 播放音量(0~100, 软件数字缩放): 串口 vol 命令运行时调整, 见 VoiceDialogSetVol */
static volatile int g_play_vol = VOICE_CFG_PLAY_VOL;

/* 待机请求(robot.standby 工具置位): 本轮告别语播完后退出对话循环回唤醒监听,
 * 与空闲超时同一退出路径; VoiceDialogArm 复位 */
static volatile int g_standby_requested = 0;

/* 唤醒→首轮播放时延打点(wake 检测时刻, 首播后清零) */
static long g_t_wake_ms = 0;

static long now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)tv.tv_usec / 1000L;
}

/* ================= 基础工具 ================= */

/* 块 RMS 电平(dBFS): int16/32768 归一 → RMS → 20*log10, 底 -120dBFS */
static float rms_dbfs(const short *s, int n)
{
    double sum = 0.0;
    int i;

    for (i = 0; i < n; i++) {
        double v = (double)s[i] / 32768.0;

        sum += v * v;
    }
    {
        double rms = sqrt(sum / n);

        return rms > 1e-9 ? (float)(20.0 * log10(rms)) : -120.0f;
    }
}

/* 回声防护: ASR 终稿是否是"机器人自己刚说过的话"(喇叭回声被麦克风拾到)。
 * 2026-10-21 板测: "休息吧"后模型只口头答应不调 robot.standby, 会话不退出,
 * 1.2s 固定回声清理不够, ASR 把 TTS 尾音转写成用户输入 → 模型再答一遍 →
 * 自问自答死循环(每次都拿到 FINAL, 空闲超时永不触发)。
 * 判据: 完全一致, 或互相包含且双方 >= 3 个汉字(9 字节, 过滤短碰撞)。 */
static int is_echo_of_reply(const char *text, const char *last_reply)
{
    size_t tn = strlen(text);
    size_t rn = strlen(last_reply);

    if (tn < 9 || rn < 9) {
        return 0;
    }
    if (strcmp(text, last_reply) == 0) {
        return 1;
    }
    return strstr(last_reply, text) != NULL || strstr(text, last_reply) != NULL;
}

/* 播放音量软缩放(Q8 定点乘系数, 100 跳过乘法保真) */
static void apply_play_vol(short *s, int len)
{
    int v = g_play_vol;

    if (v >= 100 || len <= 0) {
        return;
    }
    if (v < 0) {
        v = 0;
    }
    {
        int k = (v * 256) / 100;   /* Q8 系数 */
        int ns = len / 2;
        int i;

        for (i = 0; i < ns; i++) {
            s[i] = (short)(((int)s[i] * k) >> 8);
        }
    }
}

/* ================= 字幕通道(ui_display 屏幕显示回复文本) =================
 * 沿用原单文件追加式协议(/storage/data/cap.log):
 *   "C\n" 新回合清屏 / "A<文本>\n" 追加回复 / "D\n" 回合结束
 * 三段式简化: 每轮在 TTS 播报开始时 C+A、say 返回后 D
 * (不再有流式 delta 与 P 进度行, ui_display 收到 D 即补完显示)。
 * 所有文件操作持 g_cap_mtx 串行(fat 并发"开-写-关"会互相覆盖, 实测踩坑)。 */
#define CAPTION_LOG  "/storage/data/cap.log"

static pthread_mutex_t g_cap_mtx = PTHREAD_MUTEX_INITIALIZER;

static int CapTryLock(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        (void)pthread_mutex_lock(&g_cap_mtx);
        return 0;
    }
    ts.tv_sec += 2;
    if (pthread_mutex_timedlock(&g_cap_mtx, &ts) != 0) {
        static long lastLog = 0;
        long now = now_ms();

        if (now - lastLog > 5000) {
            lastLog = now;
            printf("[cap-w] !! lock timeout 2s, skip caption write\n");
        }
        return -1;
    }
    return 0;
}

/* 回合开始: 重建通道文件并写清屏标记 */
static void caption_reset(void)
{
    FILE *f;

    if (CapTryLock() != 0) {
        return;
    }
    f = fopen(CAPTION_LOG, "w");
    if (f == NULL) {
        printf("[cap-w] reset open FAIL\n");
        (void)pthread_mutex_unlock(&g_cap_mtx);
        return;
    }
    (void)fprintf(f, "C\n");
    (void)fclose(f);
    (void)pthread_mutex_unlock(&g_cap_mtx);
}

/* 追加回复文本(净化换行/制表符, 防单行字幕协议被破坏) */
static void caption_append(const char *text)
{
    char clean[520];
    int i, j = 0, tl = (text != NULL) ? (int)strlen(text) : 0;
    FILE *f;

    if (tl <= 0) {
        return;
    }
    for (i = 0; i < tl && j < (int)sizeof(clean) - 2; i++) {
        char c = text[i];

        if (c == '\n' || c == '\r' || c == '\t') {
            clean[j++] = ' ';
        } else {
            clean[j++] = c;
        }
    }
    clean[j] = '\0';
    if (CapTryLock() != 0) {
        return;
    }
    f = fopen(CAPTION_LOG, "a");
    if (f == NULL) {
        printf("[cap-w] append open FAIL\n");
        (void)pthread_mutex_unlock(&g_cap_mtx);
        return;
    }
    (void)fprintf(f, "A%s\n", clean);
    (void)fclose(f);
    (void)pthread_mutex_unlock(&g_cap_mtx);
}

/* 回合结束标记 */
static void caption_done(void)
{
    FILE *f;

    if (CapTryLock() != 0) {
        return;
    }
    f = fopen(CAPTION_LOG, "a");
    if (f == NULL) {
        (void)pthread_mutex_unlock(&g_cap_mtx);
        return;
    }
    (void)fprintf(f, "D\n");
    (void)fclose(f);
    (void)pthread_mutex_unlock(&g_cap_mtx);
}

/* ================= TTS 播报(TTS 段) ================= */

/* voice_tts_say 的 on_audio 回调: 首帧记时间戳 + 音量缩放 → 喇叭 */
struct say_timing {
    int got_first;
    struct timespec t_first_audio;
};

static void DialogTtsOnAudio(const void *pcm, int len, void *ud)
{
    struct say_timing *st = (struct say_timing *)ud;

    if (st != NULL && !st->got_first) {
        st->got_first = 1;
        (void)clock_gettime(CLOCK_MONOTONIC, &st->t_first_audio);
    }
    apply_play_vol((short *)(uintptr_t)pcm, len);
    (void)WakePlaybackWrite(pcm, len);
}

/*
 * 播一段文本(TTS 长连接复用, say 内按需重连), 含字幕通道写入。
 * 返回 0 正常, <0 失败。
 */
static int tts_say_caption(voice_tts_t *tts, const char *text, struct say_timing *st)
{
    int rc;

    (void)memset(st, 0, sizeof(*st));
    caption_reset();
    caption_append(text);
    rc = voice_tts_say(tts, text, DialogTtsOnAudio, st);
    caption_done();
    return rc;
}

/* 等 AO 尾部排空(固定兜底; 每 30ms 检查退出请求) */
static void wait_tail_drain(int cap_ms)
{
    int waited = 0;

    while (g_flow_running && waited < cap_ms) {
        usleep(30 * 1000);
        waited += 30;
    }
}

/* ================= agent 段(同事 agent 组件, LiteCrab 内核) =================
 * 链路: ASR 文本 → agent_input(内部 LLM tool 循环, 系统提示词在
 * agent/src/kernel/kernel.c ContextBuildSystemPromptEx, 英文机器人助手,
 * "Respond concisely in the user's language") → 最终回复经 on_tts 桥
 * 同步送回 → 串口横幅打印(人工检查) → 整段送 TTS;
 * 动作经 tool_call 桥回调 → robot.action 枚举映射本包能力表后分发。
 * 与旧本地链路(deepseek+四行模板)的差异: LLM 侧上下文/工具决策都在 agent
 * 组件内; 表情(robot.expression)/音量(robot.set_volume)/动作编排(robot.actions)
 * 均经 tool_call 桥分发到本包现成接口。
 */

#define AGENT_REPLY_CAP 4096
static char g_agent_reply[AGENT_REPLY_CAP];  /* agent 回复累积(流式 delta 逐段拼接) */

/* 懒初始化状态(agent_init 一次, 进程内复用) */
static int g_agent_inited;

/* ================= 流式接线(SPEC_AGENT_ADAPTATION 修改点①) =================
 * agent_set_stream_callbacks 注册后, agent 的 LLM 调用改走 SSE 流式, 正文增量
 * 在 agent_input 线程同步回调到这里: 累积全文(横幅/兜底播报用) + 按句冲刷到本
 * 会话 TTS(voice_tts_say 阻塞播完, 天然背压 → 边生成边播)。每轮流正文结束
 * agent 补调一次 (NULL,0) → 冲尾句。返回 0 继续(播放期打断后续按需加)。 */
static voice_tts_t *g_stream_tts;   /* 本会话 TTS 句柄(run_dialog_session 维护) */
static char g_stream_pending[512];  /* 未成句增量缓存 */
static int g_stream_spoken_len;     /* 本轮已实时播报的字节数(agent_turn 只补播尾段,
                                     * 待机轮据此决定是否叠加本地 bye.pcm) */

/* 成句边界: ASCII .!?;\n 或中文句读(。！？；、…)尾字节, 且下一字节非 UTF-8 续字节 */
static int DialogStreamIsBoundary(const char *s, int i, int n)
{
    static const unsigned char cn_tail[] = {0x81, 0x82, 0x9B, 0x9F, 0xA6};
    unsigned char c = (unsigned char)s[i];
    size_t k;

    if (c == '.' || c == '!' || c == '?' || c == ';' || c == '\n') {
        return 1;
    }
    for (k = 0; k < sizeof(cn_tail); k++) {
        if (c == cn_tail[k]) {
            return (i + 1 >= n) || (((unsigned char)s[i + 1] & 0xC0) != 0x80);
        }
    }
    return 0;
}

/* 段内是否有可发音内容。
 * 纯标点/空白段(如 "！\n\n")送 TTS 会 task-failed InvalidParameter 且杀掉
 * ws 连接, 必须跳过。注意全角标点(！，。？ U+FF00/U+3000 区, UTF-8 首字节
 * 0xE3/0xEF)也是多字节, 不能简单按 >=0x80 当"中文"——2026-10-21 板测教训。
 * 判定: ASCII 字母数字, 或 CJK 统一表意区(首字节 0xE4-0xE9), 或 4 字节区
 * (0xF0+, emoji/扩展汉字, 宽松放行); 其余(全角/CJK 标点、拉丁扩展)不算。 */
static int has_speakable(const char *s, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c < 0x80) {
            if (isalnum(c)) {
                return 1;
            }
            continue;
        }
        if (c >= 0xF0) {
            return 1;
        }
        if (c >= 0xE4 && c <= 0xE9) {
            return 1;
        }
        /* 3 字节(E0-EF): 跳过整字(标点区); 2 字节(C0-DF): 跳过 */
        if (c >= 0xE0) {
            i += 2;
        } else if (c >= 0xC0) {
            i += 1;
        }
    }
    return 0;
}

/* 播一句(阻塞至该句播完); 失败仅丢该句 */
static void DialogStreamFlush(const char *text, int len, voice_tts_t *tts)
{
    struct say_timing st;
    char sent[512];

    if (tts == NULL || text == NULL || len <= 0) {
        return;
    }
    if (len >= (int)sizeof(sent)) {
        len = (int)sizeof(sent) - 1;
    }
    (void)memcpy(sent, text, (size_t)len);
    sent[len] = '\0';
    if (!has_speakable(sent, len)) {
        /* 纯标点/空白: 无可发音内容, 视作已交付(计入已播字节, 尾段不重播) */
        g_stream_spoken_len += len;
        return;
    }
    if (tts_say_caption(tts, sent, &st) != 0) {
        printf("[agent] stream say FAILED(句丢弃)\n");
    } else {
        g_stream_spoken_len += len;   /* 已实时播报字节数, 尾段免重播 */
    }
}

static int DialogAgentOnDelta(const char *delta, int len, void *ud)
{
    voice_tts_t *tts = (g_stream_tts != NULL && !g_standby_requested) ? g_stream_tts
                                                                      : NULL;
    /* ud 忽略: 句柄由会话层维护。待机请求(robot.standby)后停止实时冲刷 —— 告别语
     * 整段留给 agent_turn 后一次播完, 避免与本地 bye.pcm 叠成"多段不同音色" */
    int plen;
    int i;

    if (delta != NULL && len > 0) {
        /* 1. 累积全文(agent_turn 横幅/兜底播报用) */
        size_t cur = strlen(g_agent_reply);
        if (cur + (size_t)len < sizeof(g_agent_reply)) {
            (void)memcpy(g_agent_reply + cur, delta, (size_t)len);
            g_agent_reply[cur + (size_t)len] = '\0';
        }
        /* 2. 入 pending, 冲刷全部完整句(每次取最靠后边界, 循环至无) */
        plen = (int)strlen(g_stream_pending);
        for (i = 0; i < len && plen < (int)sizeof(g_stream_pending) - 1; i++) {
            g_stream_pending[plen++] = delta[i];
        }
        g_stream_pending[plen] = '\0';
        for (;;) {
            int cut = -1;

            for (i = 0; i < plen; i++) {
                if (DialogStreamIsBoundary(g_stream_pending, i, plen)) {
                    cut = i;
                }
            }
            if (cut < 0 && plen >= (int)sizeof(g_stream_pending) - 1) {
                cut = plen - 1;   /* 缓存将满强制成句, 防溢出丢字 */
            }
            if (cut < 0) {
                break;
            }
            DialogStreamFlush(g_stream_pending, cut + 1, tts);
            (void)memmove(g_stream_pending, g_stream_pending + cut + 1,
                          (size_t)(plen - cut - 1));
            plen -= cut + 1;
            g_stream_pending[plen] = '\0';
        }
    } else {
        /* 收尾(NULL,0): 冲尾句(纯空白跳过) */
        plen = (int)strlen(g_stream_pending);
        for (i = 0; i < plen; i++) {
            if ((unsigned char)g_stream_pending[i] > ' ') {
                DialogStreamFlush(g_stream_pending, plen, tts);
                break;
            }
        }
        g_stream_pending[0] = '\0';
    }
    return 0;
}

/*
 * robot.action(Agent_Tool补齐分析报告.md 枚举) -> 本包 otto_pca 底层动作名。
 * kind 决定 par[] 如何按 otto_pca.h 各 Ex 形参顺序构造(OttoPcaRun 缺省从左补默认):
 *   walk/turn Ex(steps,periodMs,dir,arm)  dir: 1=前/左 -1=后/右
 *   updown/swing/tiptoe/jitter/ascend/whirlwind/windmill/takeoff/fitness
 *     Ex(steps,periodMs,amp)              amp: 摆动幅度(度)
 *   moonwalk/crusaito/flapping Ex(steps,periodMs,amp,dir)
 *   handsup/handsdown Ex(periodMs,dir)    dir: 0=双手 1=左 -1=右
 *   handwave Ex(dir)  greeting/shy Ex(dir,steps)
 * def_amp 为本固件该动作默认幅度, LLM 未传 amount 时用它(与固件默认一致)。
 */
typedef enum {
    PAR_NONE = 0,            /* 无参: sit/home/calisthenics/showcase/magic */
    PAR_STEPS_SPEED,         /* [steps, periodMs]: jump */
    PAR_STEPS_SPEED_DIR,     /* [steps, periodMs, dir]: walk/turn/bend/shakeleg */
    PAR_STEPS_SPEED_AMP,     /* [steps, periodMs, amp] */
    PAR_STEPS_SPEED_AMP_DIR, /* [steps, periodMs, amp, dir] */
    PAR_SPEED_DIR,           /* [periodMs, dir]: handsup/handsdown */
    PAR_DIR,                 /* [dir]: handwave */
    PAR_DIR_STEPS,           /* [dir, steps]: greeting/shy */
} par_kind_t;
typedef struct {
    const char *tool_name;   /* 报告 robot.action 枚举名 */
    const char *run_name;    /* otto_pca 底层动作名(OttoPcaRun) */
    par_kind_t kind;
    int def_amp;
    int allow_dir0;          /* handsup/handsdown 支持 0=双手, 其余 0 归 1 */
} act_map_t;
static const act_map_t g_actMap[] = {
    {"walk", "walk", PAR_STEPS_SPEED_DIR, 0, 0},
    {"walk_backward", "walk", PAR_STEPS_SPEED_DIR, 0, 0},   /* dir 强制 -1 */
    {"turn", "turn", PAR_STEPS_SPEED_DIR, 0, 0},
    {"moonwalk", "moonwalk", PAR_STEPS_SPEED_AMP_DIR, 25, 0},
    {"jump", "jump", PAR_STEPS_SPEED, 0, 0},
    {"sit", "sit", PAR_NONE, 0, 0},
    {"bend", "bend", PAR_STEPS_SPEED_DIR, 0, 0},
    {"shake_leg", "shakeleg", PAR_STEPS_SPEED_DIR, 0, 0},
    {"updown", "updown", PAR_STEPS_SPEED_AMP, 30, 0},
    {"swing", "swing", PAR_STEPS_SPEED_AMP, 30, 0},
    {"tiptoe", "tiptoe", PAR_STEPS_SPEED_AMP, 30, 0},
    {"jitter", "jitter", PAR_STEPS_SPEED_AMP, 20, 0},
    {"ascend", "ascend", PAR_STEPS_SPEED_AMP, 13, 0},
    {"crusaito", "crusaito", PAR_STEPS_SPEED_AMP_DIR, 25, 0},
    {"flapping", "flapping", PAR_STEPS_SPEED_AMP_DIR, 25, 0},
    {"whirlwind", "whirlwind", PAR_STEPS_SPEED_AMP, 30, 0},
    {"windmill", "windmill", PAR_STEPS_SPEED_AMP, 40, 0},
    {"takeoff", "takeoff", PAR_STEPS_SPEED_AMP, 40, 0},
    {"fitness", "fitness", PAR_STEPS_SPEED_AMP, 25, 0},
    {"hand_wave", "handwave", PAR_DIR, 0, 0},
    {"hands_up", "handsup", PAR_SPEED_DIR, 0, 1},
    {"hands_down", "handsdown", PAR_SPEED_DIR, 0, 1},
    {"greeting", "greeting", PAR_DIR_STEPS, 0, 0},
    {"shy", "shy", PAR_DIR_STEPS, 0, 0},
    {"radio_calisthenics", "calisthenics", PAR_NONE, 0, 0},
    {"magic_circle", "magic", PAR_NONE, 0, 0},
    {"showcase", "showcase", PAR_NONE, 0, 0},
    {"home", "home", PAR_NONE, 0, 0},
};

/* 简易 JSON 字段提取(同 agent 的 机器人Tool协议接入指南.md 示例);
 * 兼容 LLM 常见的 ": " 冒号带空格排版({"action": "hand_wave"}) */
static int tool_extract_str(const char *json, const char *key,
                            char *out, int out_size)
{
    char pat[48];

    (void)snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = (json != NULL) ? strstr(json, pat) : NULL;
    int o = 0;

    if (p == NULL) {
        return -1;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    while (*p != '\0' && *p != '"' && o < out_size - 1) {
        out[o++] = *p++;
    }
    out[o] = '\0';
    return 0;
}

static int tool_extract_int(const char *json, const char *key, int *out)
{
    char pat[48];

    (void)snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = (json != NULL) ? strstr(json, pat) : NULL;

    if (p == NULL) {
        return -1;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    *out = atoi(p);
    return 0;
}

/*
 * 按 robot.action 枚举名查 g_actMap 并构造参数入队(与 robot.action 分支
 * 同一套 kind 映射; 供组合编排 robot.actions 逐个调用)。
 * 返回 0=已入队, -1=枚举名未识别。
 */
static int ActEnqueueByToolName(const char *act, int dir, int steps,
                                int speed, int have_amount, int amount)
{
    size_t i;
    int par[AGENT_CFG_ACT_ARG_MAX];
    int np = 0;

    if (act == NULL || act[0] == '\0') {
        return -1;
    }
    for (i = 0; i < sizeof(g_actMap) / sizeof(g_actMap[0]); i++) {
        const act_map_t *m = &g_actMap[i];

        if (strcmp(m->tool_name, act) != 0) {
            continue;
        }
        if (strcmp(m->tool_name, "walk_backward") == 0) {
            dir = -1;   /* 后退不看 direction */
        }
        if (dir != 0 || m->allow_dir0 == 0) {
            if (dir != 1 && dir != -1) {
                dir = 1;
            }
        }
        switch (m->kind) {
            case PAR_STEPS_SPEED_DIR:
                par[np++] = steps;
                par[np++] = speed;
                par[np++] = dir;
                break;
            case PAR_STEPS_SPEED:
                par[np++] = steps;
                par[np++] = speed;
                break;
            case PAR_STEPS_SPEED_AMP:
                par[np++] = steps;
                par[np++] = speed;
                par[np++] = have_amount ? amount : m->def_amp;
                break;
            case PAR_STEPS_SPEED_AMP_DIR:
                par[np++] = steps;
                par[np++] = speed;
                par[np++] = have_amount ? amount : m->def_amp;
                par[np++] = dir;
                break;
            case PAR_SPEED_DIR:
                par[np++] = speed;
                par[np++] = dir;
                break;
            case PAR_DIR:
                par[np++] = dir;
                break;
            case PAR_DIR_STEPS:
                par[np++] = dir;
                par[np++] = steps;
                break;
            case PAR_NONE:
            default:
                break;
        }
        (void)AgentSvcEnqueueRaw(m->run_name, par, np);
        return 0;
    }
    return -1;
}

/* tool_call 桥: agent LLM 循环下发的 robot.* 工具 → 本包舵机/急停
 * (参数语义按 Agent_Tool补齐分析报告.md; 执行经 AgentSvcEnqueueRaw 入队) */
static int DialogAgentOnTool(const char *name, const char *args_json,
                             char *result, size_t result_max)
{
    printf("[agent] tool: %s %s\r\n", name, (args_json != NULL) ? args_json : "{}");

    if (name == NULL) {
        (void)snprintf(result, result_max, "{\"success\":false}");
        return -1;
    }
    if (strcmp(name, "robot.action") == 0) {
        char act[32];
        int dir = 1;
        int steps = 3;
        int speed = 1000;
        int amount = 0;
        int have_amount;
        int par[AGENT_CFG_ACT_ARG_MAX];
        int np = 0;
        size_t i;

        if (tool_extract_str(args_json, "action", act, sizeof(act)) < 0) {
            act[0] = '\0';
        }
        (void)tool_extract_int(args_json, "direction", &dir);
        (void)tool_extract_int(args_json, "steps", &steps);
        (void)tool_extract_int(args_json, "speed", &speed);
        have_amount = (tool_extract_int(args_json, "amount", &amount) == 0);
        if (strcmp(act, "stop") == 0) {
            (void)AgentSvcStopAll();
            (void)snprintf(result, result_max,
                           "{\"success\":true,\"action\":\"stop\"}");
            return 0;
        }
        if (steps < 1) {
            steps = 3;
        } else if (steps > 100) {
            steps = 100;
        }
        if (speed < 100) {
            speed = 100;
        } else if (speed > 3000) {
            speed = 3000;
        }
        for (i = 0; i < sizeof(g_actMap) / sizeof(g_actMap[0]); i++) {
            const act_map_t *m = &g_actMap[i];

            if (strcmp(m->tool_name, act) != 0) {
                continue;
            }
            if (strcmp(m->tool_name, "walk_backward") == 0) {
                dir = -1;   /* 报告: walk_backward 即后退, 不看 direction */
            }
            if (dir != 0 || m->allow_dir0 == 0) {
                if (dir != 1 && dir != -1) {
                    dir = 1;
                }
            }
            switch (m->kind) {
                case PAR_STEPS_SPEED_DIR:
                    par[np++] = steps;
                    par[np++] = speed;
                    par[np++] = dir;
                    break;
                case PAR_STEPS_SPEED:
                    par[np++] = steps;
                    par[np++] = speed;
                    break;
                case PAR_STEPS_SPEED_AMP:
                    par[np++] = steps;
                    par[np++] = speed;
                    par[np++] = have_amount ? amount : m->def_amp;
                    break;
                case PAR_STEPS_SPEED_AMP_DIR:
                    par[np++] = steps;
                    par[np++] = speed;
                    par[np++] = have_amount ? amount : m->def_amp;
                    par[np++] = dir;
                    break;
                case PAR_SPEED_DIR:
                    par[np++] = speed;
                    par[np++] = dir;
                    break;
                case PAR_DIR:
                    par[np++] = dir;
                    break;
                case PAR_DIR_STEPS:
                    par[np++] = dir;
                    par[np++] = steps;
                    break;
                case PAR_NONE:
                default:
                    break;
            }
            (void)AgentSvcEnqueueRaw(m->run_name, par, np);
            (void)snprintf(result, result_max,
                           "{\"success\":true,\"action\":\"%s\",\"steps\":%d,"
                           "\"speed\":%d,\"direction\":%d}",
                           m->tool_name, steps, speed, dir);
            return 0;
        }
        (void)snprintf(result, result_max,
                       "{\"success\":false,\"error\":\"unknown action: %s\"}", act);
        return -1;
    }
    if (strcmp(name, "robot.pose") == 0) {
        /* 报告 §4.1: 6 路舵机角度直控; 通道=OTTO_LIMB_CH_TAB(ll rl lf rf lh rh) */
        static const struct {
            const char *key;
            int ch;
        } poseCh[] = {
            {"left_leg", 0},   {"right_leg", 1},  {"left_foot", 2},
            {"right_foot", 3}, {"left_hand", 4},  {"right_hand", 5},
        };
        int v, setn = 0, k;

        if (ServoEnsureInit() != 0) {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"servo not ready\"}");
            return -1;
        }
        for (k = 0; k < (int)(sizeof(poseCh) / sizeof(poseCh[0])); k++) {
            if (tool_extract_int(args_json, poseCh[k].key, &v) == 0) {
                if (v < 0) {
                    v = 0;
                } else if (v > 180) {
                    v = 180;
                }
                (void)ServoSetAngle(poseCh[k].ch, v);
                setn++;
            }
        }
        if (setn == 0) {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"no servo field in args\"}");
            return -1;
        }
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"action\":\"pose\",\"servos_set\":%d}",
                       setn);
        return 0;
    }
    if (strcmp(name, "robot.play_sound") == 0) {
        /* 报告 §4.4: 需预置音效资源, 本包暂无 -> 如实返回失败让 LLM 告知用户 */
        (void)snprintf(result, result_max,
                       "{\"success\":false,\"error\":\"no sound resources on device\"}");
        return -1;
    }
    if (strcmp(name, "robot.set_volume") == 0) {
        /* LiteCrab robot.set_volume: volume 0~100(绝对值, 必填)。
         * 播放音量为软件数字缩放, VoiceDialogSetVol 内部 clamp 0~100,
         * 下一段 TTS 播放立即生效; 实际生效值回传 LLM 保持多轮一致 */
        int v = 0;
        int prev = VoiceDialogGetVol();
        if (tool_extract_int(args_json, "volume", &v) < 0) {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"missing volume(0-100)\"}");
            return -1;
        }
        v = VoiceDialogSetVol(v);
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"previous_volume\":%d,\"volume\":%d}",
                       prev, v);
        return 0;
    }
    if (strcmp(name, "robot.expression") == 0) {
        /* robot.expression: expression 表情名(必填, 枚举见 robot_tools.c)。
         * 经 AgentSvcDispatchEmoji 走能力表校验 + 表情文件通道
         * (/storage/data/emoji_cmd, ui_display 轮询消费切 GIF) */
        char emo[32];
        if (tool_extract_str(args_json, "expression", emo, (int)sizeof(emo)) < 0 ||
            emo[0] == '\0') {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"missing expression\"}");
            return -1;
        }
        if (AgentSvcDispatchEmoji(emo) != 0) {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"unknown expression: %s\"}",
                           emo);
            return -1;
        }
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"expression\":\"%s\"}", emo);
        return 0;
    }
    if (strcmp(name, "robot.actions") == 0) {
        /* robot.actions: 组合动作编排 — actions 为逗号/空格分隔的 2~4 个
         * robot.action 枚举名, 逐个查映射表顺序入队(动作线程按序连跳);
         * steps/speed/direction/amount 可选, 对整段统一生效。 */
        char seq[160];
        char tok[24];
        char executed[96];
        int steps = 2;
        int speed = 1000;
        int dir = 1;
        int amount = 0;
        int have_amount;
        int okn = 0;
        int ntok = 0;
        size_t i = 0;
        size_t ti = 0;

        if (tool_extract_str(args_json, "actions", seq, (int)sizeof(seq)) < 0 ||
            seq[0] == '\0') {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"missing actions\"}");
            return -1;
        }
        (void)tool_extract_int(args_json, "direction", &dir);
        (void)tool_extract_int(args_json, "steps", &steps);
        (void)tool_extract_int(args_json, "speed", &speed);
        have_amount = (tool_extract_int(args_json, "amount", &amount) == 0);
        if (steps < 1) {
            steps = 1;
        } else if (steps > 4) {
            steps = 4;
        }
        if (speed < 100) {
            speed = 100;
        } else if (speed > 3000) {
            speed = 3000;
        }
        executed[0] = '\0';
        /* 逗号/空格分词(动作名含下划线, 不作分隔符), 最多取 4 段 */
        for (;;) {
            char c = seq[i];

            if (c == ',' || c == ' ' || c == '\t' || c == '\0') {
                if (ti > 0) {
                    tok[ti] = '\0';
                    ti = 0;
                    ntok++;
                    if (ntok <= 4 &&
                        ActEnqueueByToolName(tok, dir, steps, speed,
                                             have_amount, amount) == 0) {
                        okn++;
                        if (executed[0] != '\0' &&
                            strlen(executed) < sizeof(executed) - 1) {
                            (void)strncat(executed, ",",
                                          sizeof(executed) - strlen(executed) - 1);
                        }
                        (void)strncat(executed, tok,
                                      sizeof(executed) - strlen(executed) - 1);
                    }
                }
                if (c == '\0' || ntok >= 4) {
                    break;
                }
            } else if (ti < sizeof(tok) - 1) {
                tok[ti++] = c;
            }
            i++;
        }
        if (okn == 0) {
            (void)snprintf(result, result_max,
                           "{\"success\":false,\"error\":\"no valid action in sequence\"}");
            return -1;
        }
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"queued\":%d,\"actions\":\"%s\"}",
                       okn, executed);
        return 0;
    }
    if (strcmp(name, "robot.standby") == 0) {
        /* robot.standby: 用户要求休息/待机/拜拜 —— 置待机标志, 本轮告别语
         * TTS 播完后退出对话循环回唤醒监听(与空闲超时同路径) */
        g_standby_requested = 1;
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"standby\":true}");
        return 0;
    }
    if (strcmp(name, "robot.speak") == 0) {
        /* 兼容旧工具: 无独立播报通道, 文本最终会随 agent 回复一起 TTS */
        (void)snprintf(result, result_max,
                       "{\"success\":true,\"note\":\"text spoken in final reply\"}");
        return 0;
    }
    if (strcmp(name, "robot.stop") == 0) {
        (void)AgentSvcStopAll();
        (void)snprintf(result, result_max, "{\"success\":true,\"action\":\"stop\"}");
        return 0;
    }
    (void)snprintf(result, result_max,
                   "{\"success\":false,\"error\":\"tool not supported: %s\"}", name);
    return 0;
}

/* 懒初始化(首次对话前调用): 配置与 llm_service 同一网关/key/模型 */
static int DialogAgentEnsureInit(void)
{
    AgentConfig cfg;

    if (g_agent_inited) {
        return 0;
    }
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.llm_base_url = "https://" LLM_CFG_IP "/v1/chat/completions";
    cfg.llm_api_key = LLM_CFG_KEY;
    cfg.llm_model = LLM_CFG_MODEL;
    cfg.llm_timeout_ms = 60000;
    cfg.max_tool_iterations = 5;
    cfg.conversation_history_limit = 10;
    cfg.workspace_root = "/userdata/agent";
    if (agent_init(&cfg) != 0) {
        printf("[agent] agent_init 失败(agent 组件不可用)\r\n");
        return -1;
    }
    /* 流式接线(修改点①): 正文增量经 DialogAgentOnDelta 实时播报, tool_call 桥
     * 不变; 本会话 TTS 句柄由 run_dialog_session 维护(g_stream_tts), 无会话时
     * delta 只累积不播, agent_turn 落回整段播报路径。 */
    agent_set_stream_callbacks(DialogAgentOnDelta, NULL, DialogAgentOnTool);
    g_agent_inited = 1;
    printf("[agent] agent 组件就绪(backend=LiteCrab agent, model=%s)\r\n",
           LLM_CFG_MODEL);
    return 0;
}

/*
 * 单轮 agent 调用: ASR 文本 → agent_input(阻塞, LLM tool 循环)。
 * - raw_out: agent 最终回复全文 —— 调用方串口横幅打印, 人工检查用
 * - say_out: 同 raw(整段送 TTS 播报; agent 无 [SAY] 概念)
 * 动作已在 agent_input 内部经 DialogAgentOnTool 分发入队。
 * 返回 >=0 回复长度, <0 失败。
 */
static int agent_turn(const char *user_text,
                      char *raw_out, int raw_cap, char *say_out, int say_cap)
{
    int n;

    if (raw_out != NULL && raw_cap > 0) {
        raw_out[0] = '\0';
    }
    if (say_out != NULL && say_cap > 0) {
        say_out[0] = '\0';
    }
    if (DialogAgentEnsureInit() < 0) {
        return -1;
    }
    g_agent_reply[0] = '\0';
    g_stream_pending[0] = '\0';
    g_stream_spoken_len = 0;
    printf("[agent] agent_input >> %s\r\n", user_text);
    if (agent_input(user_text) < 0) {
        printf("[agent] agent_input 失败, 详见 [agent]/[llm] 日志\r\n");
        return -1;
    }
    n = (int)strlen(g_agent_reply);
    if (n == 0) {
        printf("[agent] agent 无回复文本\r\n");
        return -1;
    }
    if (raw_out != NULL && raw_cap > 0) {
        (void)snprintf(raw_out, (size_t)raw_cap, "%s", g_agent_reply);
    }
    if (say_out != NULL && say_cap > 0) {
        /* 只补播未实时播报的尾段: 全部已播 → 空(防重播); 待机轮 → 告别语整段;
         * 流式全失败/无会话 → 全文(等价旧非流式行为)。
         * 2026-10-21: 先跳过开头纯标点/空白(流式失败段 "！\n\n" 常残留在
         * 已播边界之后, 直接补播会再次 task-failed 或念出怪顿号) */
        const char *tail = g_agent_reply + g_stream_spoken_len;

        while (*tail != '\0') {
            unsigned char c = (unsigned char)*tail;

            if (c >= 0x80 || isalnum(c)) {
                break;
            }
            tail++;
        }
        if (tail[0] != '\0') {
            (void)snprintf(say_out, (size_t)say_cap, "%s", tail);
        } else {
            say_out[0] = '\0';
        }
    }
    return n;
}

/* ================= 空闲超时收尾: 本地 bye.pcm ================= */

/* 播放本地待机提示音(24kHz/16bit/mono 裸 PCM)。文件缺失则直接跳过。 */
static void play_bye_local(void)
{
    FILE *fp;
    unsigned char blk[4800];   /* 100ms @24kHz/16bit */
    size_t n;

    /* 等上一轮回复的 AO 尾部排空, 避免告别音与尾音重叠 */
    wait_tail_drain(DIALOG_TAIL_DRAIN_MS);
    fp = fopen(VOICE_CFG_BYE_PCM, "rb");
    if (fp == NULL) {
        printf("[dialog] bye fallback missing: %s (直接回待机)\n",
               VOICE_CFG_BYE_PCM);
        return;
    }
    while ((n = fread(blk, 1, sizeof(blk), fp)) > 0) {
        apply_play_vol((short *)(void *)blk, (int)n);
        if (WakePlaybackWrite(blk, (int)n) != 0) {
            break;
        }
    }
    (void)fclose(fp);
    printf("[dialog] bye played, back to wake listen after...\n");
    /* AO 尾部排空(告别音较短, 固定等待兜底) */
    wait_tail_drain(800);
}

/* ================= 对话会话(三段式, 单线程顺序) ================= */

/*
 * 单次对话会话: ASR/TTS 长连接 + 问候 + 多轮(听→agent→说) + 空闲超时退出。
 * 前提: 采集/播放/唤醒词已由 voice_service 初始化, WiFi 已在线(调用方已查)。
 * 返回 0 = 正常(空闲超时/stop), 非 0 = 建连失败。
 * 退出后关 ASR/TTS 连接; 采集/播放复用不反初始化(支持回唤醒监听)。
 */
static int run_dialog_session(unsigned int mic_vol)
{
    voice_asr_t *asr = NULL;
    voice_tts_t *tts = NULL;
    char *user_text = NULL;
    char *raw_reply = NULL;
    char *say_text = NULL;
    int rc = 1;
    int try_i;

    user_text = (char *)malloc(DIALOG_ASR_TEXT_CAP);
    raw_reply = (char *)malloc(DIALOG_LLM_RAW_CAP);
    say_text = (char *)malloc(DIALOG_SAY_CAP);
    if (user_text == NULL || raw_reply == NULL || say_text == NULL) {
        printf("[dialog] malloc FAILED\n");
        goto CLEANUP;
    }
    char last_reply[DIALOG_ECHO_REPLY_CAP] = "";

    /* 对话麦克风增益(可调, 同旧实现): 对话结束由 VoiceDialogLoop 恢复唤醒校准增益 */
    if (mic_vol == 0 || mic_vol > 0x7e) {
        mic_vol = WakeCaptureDefaultMicVol();
    }
    if (WakeCaptureSetMicVol(mic_vol) == 0) {
        printf("[dialog] mic vol=%u (wake default=%u)\n",
               mic_vol, WakeCaptureDefaultMicVol());
    }

    /* 1. ASR 长连接(open 一次循环复用; 断线自动重连兜底) */
    printf("[dialog] asr open (long-lived)...\n");
    for (try_i = 0; try_i < DIALOG_ASR_OPEN_TRIES; try_i++) {
        asr = voice_asr_open(VOICE_CFG_QWEN_KEY);
        if (asr != NULL) {
            break;
        }
        printf("[dialog] asr open FAILED (attempt %d/%d), %s\n",
               try_i + 1, DIALOG_ASR_OPEN_TRIES,
               (try_i + 1 < DIALOG_ASR_OPEN_TRIES) ? "retry in 2s" : "give up");
        if (try_i + 1 < DIALOG_ASR_OPEN_TRIES) {
            sleep(2);
        }
    }
    if (asr == NULL) {
        printf("[dialog] asr open FAILED after %d tries\n", DIALOG_ASR_OPEN_TRIES);
        goto CLEANUP;
    }

    /* 2. TTS 长连接(open 一次循环复用, say 内按需重连) */
    printf("[dialog] tts open (long-lived)...\n");
    tts = voice_tts_open(VOICE_CFG_QWEN_KEY);
    if (tts == NULL) {
        printf("[dialog] tts open FAILED\n");
        goto CLEANUP;
    }
    /* 流式播报句柄(修改点①): delta 回调按句冲刷到这里; 会话结束置空防悬垂 */
    g_stream_tts = tts;

    /* 3. 唤醒后主动问候(硬编码, 不走 agent, 快; 同 mytest/voice VoiceGreet) */
    {
        struct say_timing st;

        printf("[dialog] greet: speaking \"%s\"...\n", VOICE_CFG_GREETING);
        (void)tts_say_caption(tts, VOICE_CFG_GREETING, &st);
        if (st.got_first && g_t_wake_ms != 0) {
            long t1 = (long)(st.t_first_audio.tv_sec * 1000L +
                             st.t_first_audio.tv_nsec / 1000000L);

            printf("[timing] T1 wake->greet first audio: %ld ms\n",
                   t1 - g_t_wake_ms);
            g_t_wake_ms = 0;
        }
        wait_tail_drain(DIALOG_TAIL_DRAIN_MS);
        /* 问候语也是喇叭出声, 记入回声比对基准 */
        (void)snprintf(last_reply, sizeof(last_reply), "%s", VOICE_CFG_GREETING);
    }
    printf("[dialog] ready! talk to me (idle %ds -> wake)\n",
           VOICE_CFG_IDLE_SEC);

    /* 4. 多轮循环: LISTEN → PROCESSING(agent) → SPEAKING(TTS) */
    while (g_flow_running) {
        int got_final = 0;
        int asr_alive = 1;
        int frameCount = 0;
        const int idleFramesMax = VOICE_CFG_IDLE_SEC * DIALOG_FRAMES_PER_SEC;
        long t_final_ms = 0;
        long t_llm_start_ms;
        long t_llm_done_ms;
        int reply_len;
        struct say_timing st;
        int said_by_tts = 0;   /* 本轮事后整段播报成功过(含流式兜底尾段) */

        /* ---- LISTEN 前回声清理: 丢弃 1.2s 帧(agent/TTS 期间采集到喇叭回声
         * 堆积在 VB, 满后丢新帧; drain 期间不喂 ASR) ---- */
        printf("[dialog] draining echo (~1.2s)...\n");
        {
            short capBuf[WAKE_PT_PER_FRM];
            int i;

            for (i = 0; i < DIALOG_ECHO_DRAIN_FRAMES && g_flow_running; i++) {
                if (WakeCaptureRead(capBuf, WAKE_PT_PER_FRM) != 0) {
                    break;
                }
            }
        }
        if (!g_flow_running) {
            break;
        }
        printf("[dialog] listening (speak now)...\n");

        /* ---- LISTEN: 喂 ASR + 非阻塞 poll 事件, 等 sentence_end=true ---- */
        while (!got_final && asr_alive && g_flow_running) {
            short capBuf[WAKE_PT_PER_FRM];

            if (WakeCaptureRead(capBuf, WAKE_PT_PER_FRM) != 0) {
                printf("[dialog] capture read fail\n");
                continue;
            }
            if (voice_asr_feed(asr, capBuf, WAKE_PT_PER_FRM) != 0) {
                printf("[dialog] asr feed FAILED (conn closed?), will reconnect\n");
                asr_alive = 0;
                break;
            }
            /* 非阻塞 drain 服务端事件(可能多个) */
            for (;;) {
                voice_asr_ev_t ev = voice_asr_poll(asr, user_text,
                                                   DIALOG_ASR_TEXT_CAP, 0);

                if (ev == VOICE_ASR_NONE) {
                    break;
                }
                if (ev == VOICE_ASR_CLOSED || ev == VOICE_ASR_FAILED) {
                    printf("[dialog] asr closed/failed, will reconnect\n");
                    asr_alive = 0;
                    break;
                }
                if (ev == VOICE_ASR_PARTIAL) {
                    if (user_text[0] != '\0') {
                        printf("[asr] %s\n", user_text);
                    }
                } else if (ev == VOICE_ASR_FINAL) {
                    if (user_text[0] == '\0') {
                        /* 空转写: 底噪/上电瞬态被服务端 VAD 误判为一句, 忽略 */
                        printf("[dialog] empty transcription (noise?), keep listening\n");
                    } else if (last_reply[0] != '\0' &&
                               is_echo_of_reply(user_text, last_reply)) {
                        /* 回声防护: 机器人自己的话被拾音转写, 丢弃, 继续听 */
                        printf("[dialog] echo guard: drop \"%s\" (robot's own words)\n",
                               user_text);
                    } else {
                        t_final_ms = now_ms();
                        printf("[You] %s\n", user_text);
                        got_final = 1;
                        break;
                    }
                }
                /* STARTED/FINISHED 忽略 */
            }
            if (!asr_alive || got_final) {
                break;
            }

            if (frameCount % DIALOG_FRAMES_PER_SEC == 0) {
                printf("[dialog] listening, mic %.1f dBFS\n",
                       (double)rms_dbfs(capBuf, WAKE_PT_PER_FRM));
            }
            frameCount++;
            if (frameCount > idleFramesMax) {
                printf("[dialog] idle timeout (%ds no valid input), exit\n",
                       VOICE_CFG_IDLE_SEC);
                play_bye_local();
                rc = 0;
                goto CLEANUP;
            }
        }

        /* ASR 断线重连兜底: close 旧 + open 新 + 重新本轮 */
        if (!asr_alive) {
            if (!g_flow_running) {
                break;
            }
            voice_asr_close(asr);
            asr = NULL;
            printf("[dialog] reconnecting ASR...\n");
            asr = voice_asr_open(VOICE_CFG_QWEN_KEY);
            if (asr == NULL) {
                printf("[dialog] ASR reconnect FAILED, exit\n");
                goto CLEANUP;
            }
            continue;
        }
        if (!got_final) {
            break;   /* 防御: 请求退出时落在此处 */
        }

        /* ---- PROCESSING: agent 段(原始回复横幅打印 + 四行协议解析分发) ---- */
        t_llm_start_ms = now_ms();
        reply_len = agent_turn(user_text, raw_reply, DIALOG_LLM_RAW_CAP,
                               say_text, DIALOG_SAY_CAP);
        t_llm_done_ms = now_ms();
        if (reply_len < 0) {
            printf("[dialog] agent turn FAILED (ret=%d), skip turn\n", reply_len);
            continue;   /* 回 LISTEN */
        }
        printf("====== [agent] 原始回复(未解析) ======\r\n%s\r\n"
               "======================================\r\n", raw_reply);

        /* ---- SPEAKING: [SAY] 文本 → TTS → 喇叭 ---- */
        if (say_text[0] != '\0') {
            if (g_standby_requested && g_stream_spoken_len > 0) {
                /* 待机轮: 告别语已随流式实时播完(round1), 模型在工具返回后
                 * 常再生成一句重复告别(round2), 这里不再补播 —— 2026-10-21
                 * 板测"两次回复"根因。整轮无出声时走下方 bye.pcm 兜底 */
                printf("[dialog] standby: farewell already streamed, skip tail replay\n");
            } else if (tts_say_caption(tts, say_text, &st) != 0) {
                printf("[dialog] tts say FAILED (下轮 say 自动重连)\n");
            } else {
                said_by_tts = 1;
                /* 时延三段(同 mytest/voice 语义):
                 * S2 = agent入口→出口(LLM 思考), S3 = 出口→出声(TTS 首帧),
                 * total = 说完话(FINAL)→出声 */
                long s2 = t_llm_done_ms - t_llm_start_ms;
                long s3 = st.got_first ? (long)((st.t_first_audio.tv_sec * 1000L +
                                                 st.t_first_audio.tv_nsec / 1000000L) -
                                                t_llm_done_ms) : -1;
                long total = st.got_first ? (long)((st.t_first_audio.tv_sec * 1000L +
                                                    st.t_first_audio.tv_nsec / 1000000L) -
                                                   t_final_ms) : -1;
                printf("[timing] turn: S2(agent)=%ld S3(tts)=%ld total=%ld ms\n",
                       s2, s3, total);
            }
        }
        /* 记录本轮机器人说过的话(取尾部: 回声转写多来自 TTS 结尾) */
        if (say_text[0] != '\0') {
            size_t said_len = strlen(say_text);

            if (said_len >= sizeof(last_reply)) {
                (void)snprintf(last_reply, sizeof(last_reply), "%s",
                               say_text + said_len - (sizeof(last_reply) - 1));
            } else {
                (void)snprintf(last_reply, sizeof(last_reply), "%s", say_text);
            }
        } else {
            printf("[dialog] agent 无 [SAY] 文本, 跳过播报\n");
        }
        /* 待机请求: 告别语已播完, 立即回唤醒监听(与空闲超时同路径)。
         * 本轮已有 TTS 出声(流式冲刷 或 事后整段告别)则不再叠加本地 bye.pcm
         * (预录音另一音色); 整轮无任何 TTS 出声(失败/无文本)才用 bye 兜底 */
        if (g_standby_requested) {
            g_standby_requested = 0;
            if (g_stream_spoken_len > 0 || said_by_tts) {
                printf("[dialog] standby requested, farewell already spoken, back to wake\n");
            } else {
                printf("[dialog] standby requested, play local bye, back to wake\n");
                play_bye_local();
            }
            rc = 0;
            goto CLEANUP;
        }
        /* 不 usleep 排空 AO: 下一轮 drain(1.2s) 期间尾部自然排空 + 回声被清 */
    }
    rc = 0;

CLEANUP:
    g_stream_tts = NULL;   /* 先摘句柄, 防 delta 回调再用悬垂指针 */
    if (tts != NULL) {
        voice_tts_close(tts);
    }
    if (asr != NULL) {
        voice_asr_close(asr);
    }
    free(say_text);
    free(raw_reply);
    free(user_text);
    printf("[dialog] session ended\n");
    return rc;
}

/* ================= 唤醒 → 对话主循环(与旧实现一致) ================= */

/*
 * 唤醒监听循环: 采集 -> 喂 wake_word -> 检测到唤醒词返回。
 * 假定采集器已初始化。每秒打印 mic 电平+score(诊断用)。
 * 返回 0 = 检测到唤醒, 非 0 = 主循环被请求退出。
 */
static int wake_listen_loop(void)
{
    short buf[WAKE_PT_PER_FRM];
    int frameCount = 0;
    /* 回唤醒监听后丢弃前 1s(33 帧): 对话刚结束喇叭余响/回声未消, 刚 reset 的
     * 模型冷启动, 喂入余响易误触发。丢弃期让环境稳定后再检测。 */
    const int WAKE_WARMUP_FRAMES = 33;
    int warmup = WAKE_WARMUP_FRAMES;

    printf("[wake] listening for \"%s\"...\n", VOICE_CFG_WAKE_WORD);

    while (g_flow_running) {
        bool detected = false;
        int step;

        if (WakeCaptureRead(buf, WAKE_PT_PER_FRM) != 0) {
            printf("[wake] read failed, retry\n");
            continue;
        }
        /* 预热期: 丢弃音频不喂模型, 让喇叭余响/回声消退 */
        if (warmup > 0) {
            warmup--;
            if (warmup == 0) {
                printf("[wake] warmup done (1s), start detecting\n");
            }
            continue;
        }
        for (step = 0; step < WAKE_STEPS_PER_READ; step++) {
            if (wake_word_process(buf + step * WAKE_WORD_SAMPLES_PER_STEP)) {
                detected = true;
            }
        }
        if (frameCount % WAKE_LISTEN_LOG_FRAMES == 0) {
            float level = rms_dbfs(buf, WAKE_PT_PER_FRM);
            float score = wake_word_last_probability();

            printf("[wake] mic %.1f dBFS, score %.3f\n",
                   (double)level, (double)score);
        }
        if (detected) {
            g_t_wake_ms = now_ms();
            printf("[wake] *** DETECTED! \"%s\" ***\n", VOICE_CFG_WAKE_WORD);
            wake_word_reset();
            return 0;
        }
        frameCount++;
    }
    return 1;  /* g_flow_running=0, 退出 */
}

/*
 * 唤醒→对话主循环(阻塞, 由 voice_service 线程调用):
 *   唤醒监听 -> 检测到 -> WiFi 在线则进对话(空闲超时回监听) -> 循环,
 *   直到 VoiceDialogRequestStop。退出不反初始化(设备复用, 重启无需重初始化)。
 */
int VoiceDialogLoop(float gate_dbfs, unsigned int mic_vol)
{
    (void)gate_dbfs;   /* 三段式用 drain 回声抑制, 能量门控保留参数但未用 */

    printf("[voice] === wake -> dialog loop (3-stage: ASR->agent->TTS, mic_vol %u) ===\n",
           mic_vol);

    while (g_flow_running) {
        char ip[16];

        /* 等唤醒词(采集器归唤醒监听) */
        if (wake_listen_loop() != 0) {
            break;  /* 收到 stop 请求 */
        }
        /* WiFi 在线才进对话; 离线继续监听, 恢复后喊唤醒词即可 */
        if (WifiSvcGetIp(ip, sizeof(ip)) != 0) {
            int waited = 0;

            printf("[voice] WiFi 离线, 忽略本次唤醒(恢复后喊 %s)\n",
                   VOICE_CFG_WAKE_WORD);
            wake_word_reset();
            while (g_flow_running && waited < 200) {  /* 2s 防唤醒残响重触发 */
                usleep(10 * 1000);
                waited++;
            }
            continue;
        }

        /* 进入对话(采集器归对话; 问候后连续多轮; 空闲超时回唤醒) */
        printf("[voice] entering dialog...\n");
        (void)run_dialog_session(mic_vol);
        printf("[voice] dialog ended, back to wake listen\n");
        /* 恢复唤醒校准增益(对话可能调低了), 保证唤醒灵敏度 */
        WakeCaptureSetMicVol(WakeCaptureDefaultMicVol());
        /* 对话期间 wake_word 未被喂新数据, 但滑动窗口/前端滤波器状态残留,
         * reset 清空后重新冷启动检测, 防止回监听后第一个声音误触发 */
        wake_word_reset();
    }

    printf("[voice] loop exited\n");
    return 0;
}

/* ================= 启停控制(voice_service 调用, 与旧实现一致) ================= */

/* (重新)进入循环前复位: 循环标志 + 唤醒词状态(停止期间模型/前端状态已陈旧) */
void VoiceDialogArm(void)
{
    g_flow_running = 1;
    g_standby_requested = 0;
    wake_word_reset();
}

/* 线程安全: 请求退出循环。监听/对话循环各阻塞点均检查 g_flow_running。 */
void VoiceDialogRequestStop(void)
{
    g_flow_running = 0;
}

/* ================= 播放音量(串口 vol 命令调用) ================= */

/*
 * 设置播放音量(软件数字缩放, 不动硬件增益):
 *   0=静音 100=原始电平(默认), 越界值自动 clamp。
 * 返回 clamp 后的实际值。运行中对话立即生效(下一写入块起)。
 */
int VoiceDialogSetVol(int vol)
{
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    g_play_vol = vol;
    return vol;
}

int VoiceDialogGetVol(void)
{
    return g_play_vol;
}
