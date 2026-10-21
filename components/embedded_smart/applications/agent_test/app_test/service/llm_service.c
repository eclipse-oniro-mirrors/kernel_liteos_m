/*
 * llm_service.c - LLM 云端问答服务实现(v1.3: 问答流式 + 长消息 raw 通道)
 * - 用户问答(LlmSvcAsk/LlmSvcGreet, 消息 <=1KB): llm_chat_stream, SSE 增量
 *   收包, token 到达即上串口(打字机), 完整回复仍落 out 缓冲;
 * - 超长消息(agent 四行协议组包 prompt 实测 ~2.2-2.9KB): v1.3 库单消息接口
 *   上限 1KB, 自动转 llm_chat_raw 自拼 JSON(库 body 上限 3072B), 响应自行
 *   提取 content。raw 通道不做流式: 四行协议须完整到手才能解析。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "llm_service.h"
#include "llm_client.h"
#include "wifi_service.h"

/* 消息上限: v1.3 库单消息接口(llm_chat/llm_chat_stream)库内截断为 1KB
 * (v1.3 头文件不再提供 LLM_MSG_MAX 宏) */
#define LLM_SVC_MSG_MAX      1024
/* raw 通道 body 预算(库上限 3072B, 超限直接 -6): 缓冲即按 3072 分配,
 * 转义写入时预留收尾空间, 天然不越限 */
#define LLM_SVC_RAW_BODY_MAX 3072
/* raw 通道响应累积缓冲(与库内 ACC 同规格) */
#define LLM_SVC_RAW_ACC      8192

static char g_reply[LLM_CFG_REPLY_MAX];
static char g_ipbuf[32];
static volatile int g_busy = 0;    /* 问答互斥: 监控线程与用户 ask 不并发用缓冲 */

static const char *llm_errstr(int e)
{
    switch (e) {
    case LLM_ERR_CONNECT:
        return "连不上网关";
    case LLM_ERR_SEND:
        return "发送失败";
    case LLM_ERR_NORESP:
        return "无响应/超时(流式60s无数据, 非流式90s)";
    case LLM_ERR_STATUS:
        return "HTTP 非200(key失效/额度用完)";
    case LLM_ERR_TOOSMALL:
        return "回复缓冲区太小(截断)";
    case LLM_ERR_ARGS:
        return "参数非法";
    case LLM_ERR_TLS:
        return "TLS 握手失败";
    case LLM_ERR_MEM:
        return "内存不足";
    default:
        return "未知错误";
    }
}

/* 流式回调: token 到达即上串口(打字机); 返回 0 继续收流, 非 0 = 请求中止
 * token 是正文片段(含 % 等任意字符), 必须 fwrite 不能 printf */
static int stream_echo(const char *token, int len, void *user)
{
    (void)user;
    if (len > 0) {
        (void)fwrite(token, 1, (size_t)len, stdout);
    }
    return 0;
}

/* ============ raw 通道(agent 四行协议长消息专用, 非流式) ============ */

/* JSON 字符串转义(同库内 json_escape 规则), *used 回传消费的源字节数
 * (源未消费完 = prompt 超预算被截断, 供调用方告警) */
static int svc_json_escape(const char *src, char *dst, int cap, int *used)
{
    int o = 0;
    const char *p = src;

    for (; *p != '\0' && o < cap - 8; p++) {
        switch (*p) {
        case '"':  dst[o++] = '\\'; dst[o++] = '"';  break;
        case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
        case '\n': dst[o++] = '\\'; dst[o++] = 'n';  break;
        case '\r': dst[o++] = '\\'; dst[o++] = 'r';  break;
        case '\t': dst[o++] = '\\'; dst[o++] = 't';  break;
        default:
            if ((unsigned char)*p >= 0x20) {
                dst[o++] = *p;
            }
            break;
        }
    }
    dst[o] = '\0';
    *used = (int)(p - src);
    return o;
}

/* 从 chat.completion 响应 JSON 提取 content(同库内 extract_content 规则;
 * "content":" 带前导引号, 不会误配 reasoning_content), 提取不到填空串 */
static void svc_extract_content(const char *resp, char *out, int out_len)
{
    const char *p = strstr(resp, "\"content\":\"");
    int o = 0;

    out[0] = '\0';
    if (p == NULL) {
        return;
    }
    p += strlen("\"content\":\"");
    while (*p != '\0' && *p != '"' && o < out_len - 1) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
            switch (*p) {
            case 'n':  out[o++] = '\n'; break;
            case 't':  out[o++] = '\t'; break;
            case '"':  out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; break;
            case 'r':  break;
            case 'u':
                /* \uXXXX 丢弃(同库内规则; 正文为 UTF-8 原文, 低频出现) */
                if (p[1] != '\0' && p[2] != '\0' && p[3] != '\0' && p[4] != '\0') {
                    p += 4;
                }
                break;
            default:
                break;
            }
            p++;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

/* raw 通道一次问答: 自拼 body -> llm_chat_raw -> 提取 content 进 out
 * prompt 超预算时截尾部(四行模板在头部不受影响), 返回 >=0 回复长度 */
static int chat_raw_send(const llm_gateway_t *gw, const char *prompt,
                         char *out, int out_len)
{
    /* body 缓冲静态区: g_busy 保证一次一问串行, 3KB 级缓冲不过线程栈 */
    static char body[LLM_SVC_RAW_BODY_MAX];
    char *raw;
    int used, esc_used, n, olen;

    used = snprintf(body, sizeof(body),
                    "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"",
                    LLM_CFG_MODEL);
    (void)svc_json_escape(prompt, body + used,
                          (int)sizeof(body) - used - 8, &esc_used);
    used += (int)strlen(body + used);
    (void)snprintf(body + used, sizeof(body) - (size_t)used, "\"}]}");
    if (esc_used < (int)strlen(prompt)) {
        printf("[llm] !! prompt %d 字节超 raw body 预算, 尾部 %d 字节截断"
               "(四行模板在头部不受影响)\r\n",
               (int)strlen(prompt), (int)strlen(prompt) - esc_used);
    }

    raw = (char *)malloc(LLM_SVC_RAW_ACC);
    if (raw == NULL) {
        return LLM_ERR_MEM;
    }
    n = llm_chat_raw(gw, body, raw, LLM_SVC_RAW_ACC);
    if (n < 0) {
        free(raw);
        return n;
    }
    svc_extract_content(raw, out, out_len);
    olen = (int)strlen(out);
    if (olen == 0) {
        /* 提取失败(响应结构异常): 原文交回供上层解析/诊断 */
        printf("[llm] !! 响应未提取到 content, 原文头 256B:\r\n%.256s\r\n", raw);
        (void)snprintf(out, (size_t)out_len, "%s", raw);
        olen = (int)strlen(out);
    }
    free(raw);
    return olen;
}

/* 问答总入口: 公共检查(互斥/WiFi) + 双通道分发
 * <=1KB 走流式(打字机), 超限自动转 raw 通道(非流式) */
static int chat_dispatch(const char *msg, char *out, int out_len)
{
    llm_gateway_t gw = {
        .ip = LLM_CFG_IP, .vhost = LLM_CFG_VHOST, .port = LLM_CFG_PORT,
        .api_key = LLM_CFG_KEY, .model = LLM_CFG_MODEL,
    };
    time_t t0;
    int n;
    int streamed;

    if (msg == NULL || msg[0] == '\0') {
        printf("[llm] 空问题 (ask 内容 / uni 4f60 597d)\r\n");
        return -1;
    }
    if (g_busy) {
        printf("[llm] 上一问答还在进行, 稍候\r\n");
        return -99;
    }
    if (WifiSvcGetIp(g_ipbuf, sizeof(g_ipbuf)) != 0) {
        printf("[llm] WiFi 未连接, 先 ip 查看 / apx 重连\r\n");
        return -1;
    }
    printf("[llm] >> %s\r\n", msg);
    WifiSvcMonPause();
    g_busy = 1;
    t0 = time(NULL);
    streamed = ((int)strlen(msg) <= LLM_SVC_MSG_MAX);
    if (streamed) {
        n = llm_chat_stream(&gw, msg, stream_echo, NULL, out, out_len);
    } else {
        n = chat_raw_send(&gw, msg, out, out_len);
    }
    g_busy = 0;
    WifiSvcMonResume();
    /* time_t 是 64 位, 必须强转 long 再进 printf(64 位参数占两个
     * varargs 寄存器, 会把后面的 %s 错位读, 见 wifi_service.c 注释) */
    if (n >= 0) {
        if (streamed) {
            /* 正文已由 stream_echo 边收边打印, 这里只报收尾 */
            printf("\r\n[llm] 完成 %d 字, 耗时 %lds\r\n",
                   n, (long)(time(NULL) - t0));
        } else {
            printf("[llm] 云端应答, 耗时 %lds\r\n", (long)(time(NULL) - t0));
            printf("[llm] << 回复(%d字):\r\n%s\r\n", n, out);
        }
    } else {
        printf("\r\n[llm] FAIL %d (%s), 耗时 %lds\r\n",
               n, llm_errstr(n), (long)(time(NULL) - t0));
        if (out[0] != '\0') {
            printf("[llm] 已收部分:\r\n%s\r\n", out);
        }
    }
    return n;
}

int LlmSvcGreet(void)
{
    return chat_dispatch(LLM_CFG_GREETING, g_reply, LLM_CFG_REPLY_MAX);
}

int LlmSvcAsk(const char *msg)
{
    return chat_dispatch(msg, g_reply, LLM_CFG_REPLY_MAX);
}

int LlmSvcChat(const char *msg, char *out, int out_len)
{
    /* agent 等带解析的调用方与普通问答同一入口: <=1KB 流式, 超限自动转 raw */
    return chat_dispatch(msg, out, out_len);
}

/* ---- shell 指令 ---- */

int LlmCmdAsk(int argc, char **argv)
{
    static char q[2048];
    size_t used = 0;
    int i;

    if (argc < 2) {
        printf("[llm] 用法: ask <内容...>\r\n");
        return 0;
    }
    q[0] = '\0';
    for (i = 1; i < argc; i++) {
        size_t len = strlen(argv[i]);

        if (used + len + 2 >= sizeof(q)) {
            break;
        }
        if (i > 1) {
            q[used++] = ' ';
        }
        memcpy(q + used, argv[i], len);
        used += len;
        q[used] = '\0';
    }
    (void)LlmSvcAsk(q);
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* uni/chatu 共用: 每个参数按 4 位 hex 码点解析并拼成 UTF-8 文本
 * 返回字节数(>0), -1=无有效码点/参数非法 */
int LlmSvcUtf8FromArgs(int argc, char **argv, char *out, int out_size)
{
    int used = 0;
    int i, k;

    if (out == NULL || out_size <= 0) {
        return -1;
    }
    out[0] = '\0';
    for (i = 1; i < argc; i++) {
        const char *s = argv[i];
        while (*s) {
            unsigned int cp = 0;
            for (k = 0; k < 4 && *s; k++) {
                int d = hexval(*s);
                if (d < 0) {
                    break;
                }
                cp = (cp << 4) | (unsigned int)d;
                s++;
            }
            if (k == 0) {
                s++;
                continue;
            }
            /* 码点 -> UTF-8 */
            if (cp < 0x80) {
                if (used + 1 < out_size) {
                    out[used++] = (char)cp;
                }
            } else if (cp < 0x800) {
                if (used + 2 < out_size) {
                    out[used++] = (char)(0xc0 | (cp >> 6));
                    out[used++] = (char)(0x80 | (cp & 0x3f));
                }
            } else if (cp < 0x10000) {
                if (used + 3 < out_size) {
                    out[used++] = (char)(0xe0 | (cp >> 12));
                    out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                    out[used++] = (char)(0x80 | (cp & 0x3f));
                }
            } else {
                if (used + 4 < out_size) {
                    out[used++] = (char)(0xf0 | (cp >> 18));
                    out[used++] = (char)(0x80 | ((cp >> 12) & 0x3f));
                    out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                    out[used++] = (char)(0x80 | (cp & 0x3f));
                }
            }
        }
    }
    out[used] = '\0';
    return (used > 0) ? used : -1;
}

/* uni 命令: 中文码点 -> UTF-8 -> 问答 */
int LlmCmdUni(int argc, char **argv)
{
    char q[2048];

    if (argc < 2) {
        printf("[llm] 用法: uni <码点...>  例: uni 4f60 597d (你好)\r\n");
        return 0;
    }
    if (LlmSvcUtf8FromArgs(argc, argv, q, sizeof(q)) < 0) {
        printf("[llm] 码点解析失败\r\n");
        return 0;
    }
    (void)LlmSvcAsk(q);
    return 0;
}
