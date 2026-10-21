/* llm.c — LLM 调用层 (用 libllmclient.a 的 llm_chat_raw 替代 OpenSSL)
 * 用 mbedtls TLS (不用 OpenSSL, 避免 getrandom syscall 384 问题)
 * 自己拼 OpenAI tools JSON body (messages 多轮 + tools 数组)
 * 自己解析响应 JSON (提取 content 和 tool_calls)
 *
 * 流式(SPEC_AGENT_ADAPTATION 修改点①参考实现, 2026-10-15): 注册了增量钩子
 * (LlmSetTextDeltaHook)时改走 llm_chat_stream_raw(SSE), 正文 delta 实时回调
 * (TTS 逐句出声); tool_calls 分片按 index 拼接, 收完把 content+tool_calls
 * 重组回非流式响应格式喂 parse_response — 引擎其余解析路径零改动。
 */
#include "litecrab/json.h"
#include "litecrab/kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdatomic.h>

#include "llm_client.h"  /* llmclient_stream 包(v1.4): llm_chat_raw / llm_chat_stream_raw */
#include "llm_stream.h"  /* llm_sse_content_frag(SSE 正文片段提取, 流式分支用) */

#define LLM_WIRE_RESPONSE_MAX (2U * 1024U * 1024U)
#define LLM_HTTP_HEADER_MAX (32U * 1024U)
static LlmConfig cfg;
static llm_gateway_t cfg_gateway;  /* libllmclient 的 gateway (LlmInit 填充) */
static _Thread_local int64_t requestDeadlineMs;
static _Thread_local int requestCallsRemaining;
static _Thread_local unsigned long requestCancelEpoch;
static atomic_ulong cancelEpoch;

static int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int remaining_ms(void) {
    if (requestDeadlineMs <= 0)
        return -1;
    int64_t rem = requestDeadlineMs - monotonic_ms();
    return rem > 0 ? (int) rem : 0;
}

int LlmInit(const LlmConfig* c) {
    memset(&cfg, 0, sizeof cfg);
    const char* url = c && c->baseUrl ? c->baseUrl : getenv("LITECRAB_BASE_URL");
    const char* k = c && c->apiKey ? c->apiKey : getenv("LITECRAB_API_KEY");
    const char* m = c && c->model ? c->model : getenv("LITECRAB_MODEL");
    if (!url)
        url = "http://127.0.0.1:18080/v1/chat/completions";
    if (!k)
        k = "";
    if (!m)
        m = "litecrab-test";

    /* 解析 URL 提取 host/IP 和 port */
    /* URL 格式: https://IP:port/path 或 https://IP/path */
    static char host_buf[256];
    static char base_url_buf[768];
    snprintf(base_url_buf, sizeof base_url_buf, "%s", url);
    cfg.baseUrl = base_url_buf;
    cfg.apiKey = k;
    cfg.model = m;
    cfg.reasoningEffort = c && c->reasoningEffort ? c->reasoningEffort : "";
    cfg.maxTokens = c && c->maxTokens > 0 ? c->maxTokens : 2048;
    cfg.temperature = c ? c->temperature : 0.2;
    cfg.stream = c ? c->stream : 1;
    cfg.timeoutMs = c && c->timeoutMs > 0 ? c->timeoutMs : 60000;

    /* 从 URL 提取 IP (跳过 https://) */
    const char* p = url;
    int useTls = 0;
    if (!strncmp(url, "https://", 8)) { p = url + 8; useTls = 1; }
    else if (!strncmp(url, "http://", 7)) { p = url + 7; useTls = 0; }

    /* 提取 host (到 : 或 / 为止) */
    size_t hl = 0;
    while (p[hl] && p[hl] != ':' && p[hl] != '/' && hl < sizeof(host_buf) - 1)
        host_buf[hl] = p[hl], hl++;
    host_buf[hl] = 0;
    cfg.port = useTls ? 443 : 80;
    cfg.useTls = useTls;
    if (p[hl] == ':')
        cfg.port = atoi(p + hl + 1);

    /* vhost: 如果 config 里设了就用, 否则用 host */
    if (c && c->vhost[0])
        snprintf(cfg.vhost, sizeof cfg.vhost, "%s", c->vhost);
    else
        snprintf(cfg.vhost, sizeof cfg.vhost, "%s", host_buf);
    snprintf(cfg.host, sizeof cfg.host, "%s", host_buf);

    /* 构造 llm_gateway_t (libllmclient 用) */
    /* 注意: llm_gateway_t 的 ip/vhost/api_key/model 是指针, 不能指向局部变量 */
    static llm_gateway_t gw;
    static char gw_ip[256], gw_vhost[256], gw_key[512], gw_model[256];
    snprintf(gw_ip, sizeof gw_ip, "%s", host_buf);
    snprintf(gw_vhost, sizeof gw_vhost, "%s", cfg.vhost);
    snprintf(gw_key, sizeof gw_key, "%s", k);
    snprintf(gw_model, sizeof gw_model, "%s", m);
    gw.ip = gw_ip;
    gw.vhost = gw_vhost;
    gw.port = cfg.port;
    gw.api_key = gw_key;
    gw.model = gw_model;
    /* 保存到全局供 LlmChatToolsEx 用 */
    memcpy(&cfg_gateway, &gw, sizeof(gw));
    return 0;
}

const LlmConfig* LlmGetConfig(void) { return &cfg; }
void LlmRequestScopeBegin(long long deadlineMs, int maxCalls) {
    requestDeadlineMs = deadlineMs;
    requestCallsRemaining = maxCalls;
    requestCancelEpoch = atomic_fetch_add(&cancelEpoch, 1) + 1;
}
void LlmRequestScopeEnd(void) { requestDeadlineMs = 0; }
void LlmCancelActiveRequests(void) {
    requestCancelEpoch = atomic_fetch_add(&cancelEpoch, 1) + 1;
}

void LlmResponseClear(LlmResponse* r) {
    if (!r) return;
    r->text[0] = 0;
    r->textLen = 0;
    r->callCount = 0;
    r->toolUse = 0;
    r->promptTokens = r->completionTokens = r->totalTokens = r->reasoningTokens = 0;
    for (int i = 0; i < r->callCount; i++) {
        free(r->calls[i].input);
        r->calls[i].input = NULL;
        r->calls[i].inputLen = r->calls[i].inputCapacity = 0;
    }
}

/* 从 LLM 响应 JSON 解析 content 和 tool_calls */
static int parse_response(const char* response_json, LlmResponse* resp) {
    LlmResponseClear(resp);
    /* 找 "content":"..." */
    const char* content_start = strstr(response_json, "\"content\":\"");
    if (content_start) {
        content_start += strlen("\"content\":\"");
        /* 处理转义, 提取 content */
        size_t o = 0;
        const char* p = content_start;
        while (*p && *p != '"' && o < sizeof(resp->text) - 1) {
            if (*p == '\\' && *(p+1)) {
                p++;
                switch (*p) {
                case 'n': resp->text[o++] = '\n'; break;
                case 't': resp->text[o++] = '\t'; break;
                case '"': resp->text[o++] = '"'; break;
                case '\\': resp->text[o++] = '\\'; break;
                case 'r': break;
                default: resp->text[o++] = *p; break;
                }
            } else {
                resp->text[o++] = *p;
            }
            p++;
        }
        resp->text[o] = 0;
        resp->textLen = o;
    }

    /* 找 "tool_calls":[ */
    const char* tc_start = strstr(response_json, "\"tool_calls\":[");
    if (tc_start) {
        resp->toolUse = 1;
        /* 简单解析 tool_calls: 找 "name":"..." 和 "arguments":"..." */
        const char* p = tc_start;
        while (resp->callCount < LLM_MAX_TOOL_CALLS) {
            const char* name_start = strstr(p, "\"name\":\"");
            if (!name_start || name_start > strstr(p, "]"))
                break;
            name_start += strlen("\"name\":\"");
            size_t nl = 0;
            while (name_start[nl] && name_start[nl] != '"' && nl < sizeof(resp->calls[resp->callCount].name) - 1)
                resp->calls[resp->callCount].name[nl] = name_start[nl], nl++;
            resp->calls[resp->callCount].name[nl] = 0;

            const char* args_start = strstr(name_start, "\"arguments\":\"");
            if (args_start) {
                args_start += strlen("\"arguments\":\"");
                /* 提取 arguments (可能含转义) */
                size_t al = 0;
                const char* q = args_start;
                char* buf = malloc(8192);
                if (buf) {
                    while (*q && *q != '"' && al < 8191) {
                        if (*q == '\\' && *(q+1)) {
                            q++;
                            switch (*q) {
                            case 'n': buf[al++] = '\n'; break;
                            case 't': buf[al++] = '\t'; break;
                            case '"': buf[al++] = '"'; break;
                            case '\\': buf[al++] = '\\'; break;
                            default: buf[al++] = *q; break;
                            }
                        } else {
                            buf[al++] = *q;
                        }
                        q++;
                    }
                    buf[al] = 0;
                    resp->calls[resp->callCount].input = buf;
                    resp->calls[resp->callCount].inputLen = al;
                    resp->calls[resp->callCount].inputCapacity = 8192;
                }
            }
            /* 生成 call id */
            snprintf(resp->calls[resp->callCount].id, sizeof(resp->calls[resp->callCount].id),
                     "call_%d", resp->callCount);
            resp->callCount++;
            p = args_start ? args_start : name_start;
            /* 找下一个 "name":" 或 ] */
            const char* next = strstr(p, "\"name\":\"");
            const char* bracket = strstr(p, "]");
            if (!next || (bracket && next > bracket))
                break;
            p = next;
        }
    }

    /* 提取 token 信息 */
    const char* pt = strstr(response_json, "\"prompt_tokens\":");
    if (pt) resp->promptTokens = atoi(pt + strlen("\"prompt_tokens\":"));
    const char* ct = strstr(response_json, "\"completion_tokens\":");
    if (ct) resp->completionTokens = atoi(ct + strlen("\"completion_tokens\":"));
    const char* tt = strstr(response_json, "\"total_tokens\":");
    if (tt) resp->totalTokens = atoi(tt + strlen("\"total_tokens\":"));
    return 0;
}

/* ================= 流式分支(SPEC_AGENT_ADAPTATION 修改点①) ================= */

static LlmTextDeltaFn g_delta_fn = NULL;
static void* g_delta_ud = NULL;

void LlmSetTextDeltaHook(LlmTextDeltaFn fn, void* ud) {
    g_delta_fn = fn;
    g_delta_ud = ud;
}

/* SSE 流重组缓冲(单轮): content 原样(带转义 — parse_response 自己反转义)
 * 累积; tool_calls 按 index 拼接(name 一次到齐, arguments 分片追加, 均原样)。
 * 堆分配(llm.c 顶部惯例: 大缓冲不进静态段)。 */
#define LS_CONTENT_MAX  8192
#define LS_ARGS_MAX      4096
#define LS_NAME_MAX        64
#define LS_MAX_CALLS        4
#define LS_FRAG_MAX        512   /* 单事件正文片段缓冲(与 llm_client 的 LLS_FRAG_SIZE 同量级) */
typedef struct {
    char content[LS_CONTENT_MAX];
    int  content_len;
    struct {
        char name[LS_NAME_MAX];
        char args[LS_ARGS_MAX];
        int  args_len;
    } calls[LS_MAX_CALLS];
    int n_calls;    /* 已见到的最大 index+1 */
    int aborted;    /* 增量钩子请求中止(播放期打断) */
} ls_acc_t;

/* 从 line 里取 "key":"..." 的原始串(不反转义, 转义对原样保留 — 重组时仍是
 * 合法 JSON 字符串内容; 到未转义引号止)。
 * 返回长度(可 0), 未命中 -1, 放不下 -2。 */
static int ls_raw_str(const char* line, const char* key, char* out, int cap) {
    char pat[40];
    if (snprintf(pat, sizeof pat, "\"%s\":\"", key) >= (int)sizeof pat) return -1;
    const char* p = strstr(line, pat);
    if (p == NULL) return -1;
    p += strlen(pat);
    int n = 0;
    while (*p) {
        if (*p == '\\') {           /* 转义对原样保留 */
            if (n + 2 > cap - 1) return -2;
            out[n++] = *p++;
            if (*p == '\0') break;
            out[n++] = *p++;
            continue;
        }
        if (*p == '"') break;       /* 未转义引号 = 字符串结束 */
        if (n + 1 > cap - 1) return -2;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n;
}

/* llm_chat_stream_raw 的行消费者(每 SSE 事件一次, 同步在调用线程):
 *  1) 正文片段: llm_sse_content_frag 提取(已反转义/过滤 reasoning)→ 转发
 *     增量钩子(TTS); 另取原始(带转义)串累积进重组缓冲。
 *  2) tool_calls 增量: index 定位, name 到齐即存, arguments 分片追加。
 * 返回非 0 = 中止(钩子请求 — 播放期打断), 传输层停读。 */
static int ls_on_line(void* ud, const char* line, int len) {
    ls_acc_t* a = (ls_acc_t*)ud;
    (void)len;
    char frag[LS_FRAG_MAX];
    int fl = llm_sse_content_frag(line, frag, sizeof frag);
    if (fl > 0) {
        if (g_delta_fn != NULL && g_delta_fn(g_delta_ud, frag, fl) != 0) {
            a->aborted = 1;
            return 1;               /* 中止生成: 停止读取(已收保留) */
        }
        char raw[LS_FRAG_MAX * 2];
        int rl = ls_raw_str(line, "content", raw, sizeof raw);
        if (rl > 0 && a->content_len + rl < LS_CONTENT_MAX) {
            memcpy(a->content + a->content_len, raw, (size_t)rl);
            a->content_len += rl;
        }
    }
    if (strstr(line, "\"tool_calls\"") != NULL) {
        int idx = 0;
        const char* ix = strstr(line, "\"index\":");
        if (ix != NULL) idx = atoi(ix + 8);
        if (idx < 0 || idx >= LS_MAX_CALLS) idx = 0;
        char tmp[LS_FRAG_MAX * 2];
        if (a->calls[idx].name[0] == '\0') {
            int nl = ls_raw_str(line, "name", tmp, sizeof tmp);
            if (nl > 0) snprintf(a->calls[idx].name, LS_NAME_MAX, "%s", tmp);
        }
        int al = ls_raw_str(line, "arguments", tmp, sizeof tmp);
        if (al > 0 && a->calls[idx].args_len + al < LS_ARGS_MAX) {
            memcpy(a->calls[idx].args + a->calls[idx].args_len, tmp, (size_t)al);
            a->calls[idx].args_len += al;
        }
        if (idx + 1 > a->n_calls) a->n_calls = idx + 1;
    }
    return 0;
}

/* 流式轮次: 传输(SSE 行消费) + 重组非流式响应到 response[]。
 * 返回 0 成功(含钩子中止 — resp 里是已收部分); <0 传输错误(已收部分仍重组)。
 * 每轮流结束信号(钩子 delta=NULL)在所有出口前必调一次。 */
static int llm_chat_tools_stream(const char* body, int blen, char* response, int resp_cap) {
    ls_acc_t* acc = (ls_acc_t*)malloc(sizeof(ls_acc_t));
    if (acc == NULL) return LLM_ERR_MEM;
    memset(acc, 0, sizeof *acc);
    printf("[llm] calling llm_chat_stream_raw, body_len=%d\n", blen);
    fflush(stdout);
    int r = llm_chat_stream_raw(&cfg_gateway, body, ls_on_line, acc);
    printf("[llm] llm_chat_stream_raw returned: %d (aborted=%d, content=%d, calls=%d)\n",
           r, acc->aborted, acc->content_len, acc->n_calls);
    fflush(stdout);
    /* 本轮流结束信号(voice 侧收尾 TTS 流任务) — 即便中止/出错也补 */
    if (g_delta_fn != NULL) g_delta_fn(g_delta_ud, NULL, 0);
    if (acc->aborted) {
        free(acc);
        /* 钩子请求中止(播放期打断): 本轮作废, 返回错误让 run_round 停止 —
         * 不再跑工具循环下一轮, 部分回复也不进会话历史。 */
        printf("[llm] stream aborted by delta hook, round discarded\n");
        fflush(stdout);
        return LLM_ERR_NORESP;
    }
    if (r < 0 && acc->content_len == 0 && acc->n_calls == 0) {
        free(acc);
        return r;                   /* 空手网络错: 原样上抛 */
    }
    /* 重组非流式响应(parse_response 口径: content / tool_calls[].name+arguments)。
     * content 与 args 都是带转义的原始串, 直接嵌入引号内即合法 JSON。 */
    int pos = 0;
    int truncated = 0;
    #define LS_APPEND(...) do { \
        if (!truncated) { \
            int _n = snprintf(response + pos, (size_t)(resp_cap - pos), __VA_ARGS__); \
            if (_n < 0 || _n >= resp_cap - pos) truncated = 1; \
            else pos += _n; \
        } \
    } while (0)
    LS_APPEND("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"%.*s\"",
              acc->content_len, acc->content);
    if (acc->n_calls > 0) {
        LS_APPEND(",\"tool_calls\":[");
        int emitted = 0;
        for (int i = 0; i < acc->n_calls; i++) {
            if (acc->calls[i].name[0] == '\0') continue;
            LS_APPEND("%s{\"name\":\"%s\",\"arguments\":\"%.*s\"}",
                      emitted ? "," : "", acc->calls[i].name,
                      acc->calls[i].args_len, acc->calls[i].args);
            emitted = 1;
        }
        LS_APPEND("]");
    }
    LS_APPEND("}}]}");
    #undef LS_APPEND
    free(acc);
    if (truncated) {
        printf("[llm] reconstructed response truncated (cap=%d)\n", resp_cap);
        fflush(stdout);
        return LLM_ERR_TOOSMALL;
    }
    printf("[llm] response reconstructed: %.200s\n", response);
    fflush(stdout);
    return 0;
}

int LlmChatToolsEx(const char* systemPrompt,
                   const char* messagesJson,
                   const char* toolsJson,
                   LlmResponse* resp,
                   int stream) {
    if (!messagesJson || !resp)
        return -1;

    /* 拼接 OpenAI tools JSON body
     * 格式: {"model":"...","messages":[...],"tools":[...],"tool_choice":"auto"}
     * messagesJson 已经是 [...] 格式
     * toolsJson 已经是 [...] 格式
     */
    /* 先构造 messages 数组 (加 system prompt) */
    /* messagesJson 是 [...] 格式, 在前面插入 system 消息。
     * 2026-10-19 放宽: 工具轮旁白回填历史后 messages 随轮数增长, 原 8192
     * 会静默截断成非法 JSON; 对齐 kernel.c 会话缓冲(24K 会话 + 32K 轮内余量)。 */
    static char messages_with_system[65536];
    if (systemPrompt && *systemPrompt) {
        /* 转义 system prompt */
        char escaped_system[8192];
        size_t el = 0;
        for (const char* p = systemPrompt; *p && el < sizeof(escaped_system) - 8; p++) {
            switch (*p) {
            case '"': escaped_system[el++] = '\\'; escaped_system[el++] = '"'; break;
            case '\\': escaped_system[el++] = '\\'; escaped_system[el++] = '\\'; break;
            case '\n': escaped_system[el++] = '\\'; escaped_system[el++] = 'n'; break;
            case '\r': escaped_system[el++] = '\\'; escaped_system[el++] = 'r'; break;
            default: escaped_system[el++] = *p; break;
            }
        }
        escaped_system[el] = 0;

        /* messagesJson 是 [...] 格式, 插入 system 消息 */
        /* 去掉开头的 [, 加 system 消息, 再加回 [ */
        const char* inner = messagesJson;
        while (*inner && *inner != '[') inner++;
        if (*inner == '[') inner++;
        while (*inner && strchr(" \r\n\t", *inner)) inner++;

        int mw = snprintf(messages_with_system, sizeof messages_with_system,
                          "[{\"role\":\"system\",\"content\":\"%s\"},%s",
                          escaped_system, *inner ? inner : "]");
        if (mw <= 0 || mw >= (int)sizeof messages_with_system) {
            printf("[llm] messages(+system) too large: %d (buf %d)\n",
                   mw, (int)sizeof messages_with_system);
            fflush(stdout);
            return LLM_ERR_TOOSMALL;
        }
    } else {
        int mw = snprintf(messages_with_system, sizeof messages_with_system, "%s", messagesJson);
        if (mw <= 0 || mw >= (int)sizeof messages_with_system) {
            printf("[llm] messages too large: %d (buf %d)\n",
                   mw, (int)sizeof messages_with_system);
            fflush(stdout);
            return LLM_ERR_TOOSMALL;
        }
    }

    /* 拼完整 body (注意 llm_chat_raw 的 body 上限是 3KB)。
     * 2026-10-19 放宽: 同 messages_with_system, 且与流式客户端
     * llm_client.c 的 LLM_RAWBODY_MAX(68K) 对齐。 */
    static char body[69632];
    int blen;
    if (toolsJson && toolsJson[0] == '[' && toolsJson[1] != ']') {
        blen = snprintf(body, sizeof body,
                         "{\"model\":\"%s\",\"messages\":%s,\"tools\":%s,\"tool_choice\":\"auto\",\"max_tokens\":%d,\"temperature\":%.1f}",
                         cfg.model, messages_with_system, toolsJson, cfg.maxTokens, cfg.temperature);
    } else {
        blen = snprintf(body, sizeof body,
                         "{\"model\":\"%s\",\"messages\":%s,\"max_tokens\":%d,\"temperature\":%.1f}",
                         cfg.model, messages_with_system, cfg.maxTokens, cfg.temperature);
    }
    if (blen <= 0 || blen >= (int)sizeof body) {
        printf("[llm] body too small/large: blen=%d bufsize=%d\n", blen, (int)sizeof body);
        fflush(stdout);
        return LLM_ERR_TOOSMALL;
    }

    /* 调 LLM: 注册了增量钩子走 SSE 流式(修改点①), 否则原非流式 llm_chat_raw。
     * static 复用须先清空: 失败路径(如 -6 参数拒发, 未写 response)会把
     * 上一轮残留当"服务器错误响应"打印出来, 误导排障。 */
    static char response[32768];
    response[0] = 0;
    int r;
    if (g_delta_fn != NULL) {
        r = llm_chat_tools_stream(body, blen, response, sizeof response);
    } else {
        printf("[llm] calling llm_chat_raw, body_len=%d\n", blen);
        fflush(stdout);
        r = llm_chat_raw(&cfg_gateway, body, response, sizeof response);
        printf("[llm] llm_chat_raw returned: %d\n", r);
        fflush(stdout);
    }
    if (r < 0) {
        /* 打印服务器返回的错误内容 (帮助诊断 -4 等错误) */
        printf("[llm] error response: %.500s\n", response);
        fflush(stdout);
        return r;
    }

    /* 解析响应 */
    printf("[llm] response first 200 chars: %.200s\n", response);
    fflush(stdout);
    parse_response(response, resp);
    return 0;
}

int LlmParseResponseBody(const char* body, int stream, LlmResponse* resp) {
    return parse_response(body, resp);
}
