/* llm_client.c — LLM 问答客户端库实现(v1.3: 流式 SSE 增量回调)
 * llm_chat        : 板 -> 客户网关 IP:443 (mbedTLS TLS1.2, SNI/Host=vhost, Bearer=key)
 *                   库内封装 JSON(单条 user 消息), 回复自动提取 content
 * llm_chat_raw    : 同一传输通道, 客户自带完整 JSON body, 响应原样返回(不解析)
 * llm_chat_stream : 同一通道 + "stream":true, SSE 增量收包(llm_stream.c 解码),
 *                   正文片段实时回调, 支持 callback 中止; 思考过程不透传
 * llm_ask         : 板 -> PC 中继 /say (明文 HTTP, 免 key, 每次短连接)
 *
 * v1.2 长连接复用:
 *   - TLS 连接缓存在库内(g_lock 保护), 按 (ip,vhost,port) 匹配, 重复调用免握手
 *   - HTTP/1.1 + Connection: keep-alive, 按 Content-Length/chunked 帧边界收包
 *   - 空闲 > LLM_CONN_IDLE_SEC 主动断开; 半开连接(零字节响应)自动重建重试一次
 *   - API 签名与错误码不变; 最坏耗时变为 2x 预算(重试一次时)
 *
 * v1.3 流式:
 *   - 超时语义变化: 流式用"空闲预算"(60s 无数据判死), 总时长不设限;
 *     非流式接口仍为 90s 总预算, 不受影响
 *   - [DONE] 后继续排干到 chunked 终结(宽限 10s), 连接保持可复用
 *
 * 注: 大缓冲一律 malloc(堆) — liteos_a 链接器对超大静态段会错位放置(实测页错误)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "llm_client.h"
#include "llm_stream.h"

#define LLM_DEFAULT_MODEL   "deepseek-ai/DeepSeek-V4-Flash"
#define LLM_MSG_MAX         1024
#define LLM_FD_TIMEOUT_SEC  5
#define LLM_RETRY_BUDGET    18    /* 18 x 5s = 90s 总预算(每次尝试) */
#define LLM_ACC_SIZE        8192
#define LLM_REQ_SIZE        3584
#define LLM_BODY_SIZE       3072
#define LLM_RAWBODY_MAX     3072
#define LLM_ESCJ_SIZE       2048
#define LLM_ASKREQ_SIZE     1200
#define LLM_CONN_IDLE_SEC   30
/* v1.3 流式(预算宏可被 -D 覆盖, 供主机测试加速) */
#ifndef LLM_STREAM_IDLE_BUDGET
#define LLM_STREAM_IDLE_BUDGET  12    /* 12 x 5s = 60s 空闲判死(总时长不设限) */
#endif
#ifndef LLM_STREAM_GRACE_BUDGET
#define LLM_STREAM_GRACE_BUDGET 2     /* [DONE] 后等 chunked 终结的宽限(10s) */
#endif
#define LLS_FRAG_SIZE           512   /* 单个 delta 正文片段缓冲 */
#ifndef LLM_HTTP_LINE
#define LLM_HTTP_LINE "HTTP/1.1"
#endif
#ifndef LLM_CONN_HDR
#define LLM_CONN_HDR "Connection: keep-alive\r\n"
#endif    /* 空闲超时: 主动断开重连 */

static char g_relay_ip[32] = "192.168.1.1";
static int g_relay_port = 8443;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ============ TLS 连接缓存(g_lock 保护, llm_chat/llm_chat_raw 共用) ============ */
typedef struct {
    int alive;
    char ip[32];
    char vhost[64];
    int port;
    time_t last_used;
    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
} llm_conn_t;

static llm_conn_t g_conn;

int llm_set_server(const char *ip, int port)
{
    if (ip != NULL) {
        if (inet_addr(ip) == INADDR_NONE) {
            return -1;
        }
        snprintf(g_relay_ip, sizeof(g_relay_ip), "%s", ip);
    }
    if (port > 0 && port < 65536) {
        g_relay_port = port;
    }
    return 0;
}

static const char *extract_content(const char *s, char *out, int out_size)
{
    const char *p = strstr(s, "\"content\":\"");
    if (p == NULL) {
        return NULL;
    }
    p += strlen("\"content\":\"");
    int o = 0;
    while (*p != '\0' && *p != '"' && o < out_size - 1) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
            switch (*p) {
            case 'n':  out[o++] = '\n'; break;
            case 't':  out[o++] = '\t'; break;
            case '"':  out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; break;
            case 'r':  break;
            case 'u':
                if (p[1] && p[2] && p[3] && p[4]) {
                    p += 4;
                }
                break;
            default:   out[o++] = *p; break;
            }
        } else {
            out[o++] = *p;
        }
        p++;
    }
    out[o] = '\0';
    return out;
}

/* JSON 字符串转义(消息内嵌进 JSON body 用) */
static int json_escape(const char *src, char *dst, int cap)
{
    int o = 0;
    for (; *src != '\0' && o < cap - 8; src++) {
        switch (*src) {
        case '"':  dst[o++] = '\\'; dst[o++] = '"';  break;
        case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
        case '\n': dst[o++] = '\\'; dst[o++] = 'n';  break;
        case '\r': dst[o++] = '\\'; dst[o++] = 'r';  break;
        case '\t': dst[o++] = '\\'; dst[o++] = 't';  break;
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

/* 定位 HTTP body(跳过头部) */
static const char *http_body(const char *acc, int acc_len)
{
    for (int i = 0; i + 4 <= acc_len; i++) {
        if (acc[i] == '\r' && acc[i + 1] == '\n' &&
            acc[i + 2] == '\r' && acc[i + 3] == '\n') {
            return acc + i + 4;
        }
    }
    return NULL;
}

static int http_ok(const char *acc)
{
    if (strncmp(acc, "HTTP/1.0 200", 12) != 0 && strncmp(acc, "HTTP/1.1 200", 12) != 0) {
        return LLM_ERR_STATUS;
    }
    return 0;
}

/* 大小写不敏感比较(头部名匹配) */
static int hdr_ieq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

/* 在头部区找字段值: 返回值起始指针(跳过冒号后空格), 不存在返回 NULL */
static const char *hdr_value(const char *headers, int headers_len, const char *name)
{
    int nlen = (int)strlen(name);
    const char *line = headers;
    const char *end = headers + headers_len;
    while (line < end) {
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        if (eol == NULL) eol = end;
        int llen = (int)(eol - line);
        if (llen > nlen && line[nlen] == ':' && hdr_ieq(line, name, nlen)) {
            const char *v = line + nlen + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            return v;
        }
        line = eol + 1;
    }
    return NULL;
}

/* 公共收尾: 从完整 HTTP 响应(acc)提取 content 填 out */
static int finish_reply(const char *acc, int acc_len, char *out, int out_len)
{
    const char *body = http_body(acc, acc_len);
    if (body == NULL) {
        return LLM_ERR_NORESP;
    }
    int r = http_ok(acc);
    if (r != 0) {
        return r;
    }
    char *cbuf = (char *)malloc(4096);
    if (cbuf == NULL) {
        return LLM_ERR_MEM;
    }
    const char *c = extract_content(body, cbuf, 4096);
    if (c == NULL) {
        c = body;   /* 提取失败兜底: 原样给 body */
    }
    int clen = (int)strlen(c);
    int ret;
    if (clen >= out_len) {
        memcpy(out, c, (size_t)out_len - 1);
        out[out_len - 1] = '\0';
        ret = LLM_ERR_TOOSMALL;
    } else {
        memcpy(out, c, (size_t)clen);
        out[clen] = '\0';
        ret = clen;
    }
    free(cbuf);
    return ret;
}

/* 公共收尾(raw 版): HTTP body 原样拷给 out, 不做任何 JSON 解析 */
static int finish_raw(const char *acc, int acc_len, char *out, int out_len)
{
    const char *body = http_body(acc, acc_len);
    if (body == NULL) {
        return LLM_ERR_NORESP;
    }
    int r = http_ok(acc);
    if (r != 0) {
        return r;
    }
    int blen = acc_len - (int)(body - acc);
    if (blen >= out_len) {
        memcpy(out, body, (size_t)out_len - 1);
        out[out_len - 1] = '\0';
        return LLM_ERR_TOOSMALL;
    }
    memcpy(out, body, (size_t)blen);
    out[blen] = '\0';
    return blen;
}

/* ============ 连接缓存管理 ============ */

static void conn_teardown(void)
{
    if (!g_conn.alive) {
        return;
    }
    mbedtls_net_free(&g_conn.server_fd);
    mbedtls_ssl_free(&g_conn.ssl);
    mbedtls_ssl_config_free(&g_conn.conf);
    mbedtls_ctr_drbg_free(&g_conn.ctr_drbg);
    mbedtls_entropy_free(&g_conn.entropy);
    memset(&g_conn, 0, sizeof(g_conn));
}

/* 建连(完整握手), 成功返回 0 */
static int conn_open(const llm_gateway_t *gw)
{
    char portstr[8];
    conn_teardown();

    mbedtls_net_init(&g_conn.server_fd);
    mbedtls_entropy_init(&g_conn.entropy);
    mbedtls_ctr_drbg_init(&g_conn.ctr_drbg);
    mbedtls_ssl_init(&g_conn.ssl);
    mbedtls_ssl_config_init(&g_conn.conf);

    const char *pers = "llm_client";
    if (mbedtls_ctr_drbg_seed(&g_conn.ctr_drbg, mbedtls_entropy_func, &g_conn.entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        goto fail;
    }
    if (mbedtls_ssl_config_defaults(&g_conn.conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto fail;
    }
    mbedtls_ssl_conf_authmode(&g_conn.conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&g_conn.conf, mbedtls_ctr_drbg_random, &g_conn.ctr_drbg);
    if (mbedtls_ssl_setup(&g_conn.ssl, &g_conn.conf) != 0) {
        goto fail;
    }
    if (mbedtls_ssl_set_hostname(&g_conn.ssl, gw->vhost) != 0) {
        goto fail;
    }
    snprintf(portstr, sizeof(portstr), "%d", (gw->port > 0) ? gw->port : 443);
    if (mbedtls_net_connect(&g_conn.server_fd, gw->ip, portstr, MBEDTLS_NET_PROTO_TCP) != 0) {
        conn_teardown();
        return LLM_ERR_CONNECT;
    }
    {
        struct timeval tv = { LLM_FD_TIMEOUT_SEC, 0 };
        setsockopt(g_conn.server_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(g_conn.server_fd.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    mbedtls_ssl_set_bio(&g_conn.ssl, &g_conn.server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    int budget = LLM_RETRY_BUDGET;
    int r;
    while ((r = mbedtls_ssl_handshake(&g_conn.ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (--budget < 0) {
                conn_teardown();
                return LLM_ERR_NORESP;
            }
            continue;
        }
        conn_teardown();
        return LLM_ERR_TLS;
    }
    /* 记录连接 key(深拷贝, 调用方的字符串可能是临时的) */
    snprintf(g_conn.ip, sizeof(g_conn.ip), "%s", gw->ip);
    snprintf(g_conn.vhost, sizeof(g_conn.vhost), "%s", gw->vhost);
    g_conn.port = (gw->port > 0) ? gw->port : 443;
    g_conn.last_used = time(NULL);
    g_conn.alive = 1;
    return 0;

fail:
    conn_teardown();
    return LLM_ERR_TLS;
}

/* 确保缓存的连接可用: key 匹配 + 未超空闲; 否则重建 */
static int conn_ensure(const llm_gateway_t *gw)
{
    int port = (gw->port > 0) ? gw->port : 443;
    if (g_conn.alive &&
        strcmp(g_conn.ip, gw->ip) == 0 &&
        strcmp(g_conn.vhost, gw->vhost) == 0 &&
        g_conn.port == port) {
        time_t now = time(NULL);
        if (now != (time_t)-1 && (now - g_conn.last_used) < LLM_CONN_IDLE_SEC) {
            return 0;   /* 复用 */
        }
    }
    return conn_open(gw);
}

/* ============ 帧式收发(在缓存连接上执行一次 POST) ============
 * 返回: >=0 响应总字节数(acc); 负数 = LLM_ERR_*
 * *stale = 1: 连接已死且未收到任何响应字节(可安全重试)
 */

/* 公共请求拼装(非流式/流式共用, 格式严格一致) */
static int build_post_req(char *req, int cap, const llm_gateway_t *gw,
                          const char *body, int blen)
{
    return snprintf(req, (size_t)cap,
                    "POST /v1/chat/completions " LLM_HTTP_LINE "\r\nHost: %s\r\n"
                    "Content-Type: application/json\r\nAuthorization: Bearer %s\r\n"
                    LLM_CONN_HDR "Content-Length: %d\r\n\r\n%s",
                    gw->vhost, gw->api_key, blen, body);
}

static int https_post_once(const llm_gateway_t *gw, const char *body, int blen,
                           char *acc, int acc_size, int *stale)
{
    char *req = (char *)malloc(LLM_REQ_SIZE);
    int ret = LLM_ERR_MEM;
    int acc_len = 0;
    *stale = 0;

    if (req == NULL) {
        return LLM_ERR_MEM;
    }

    int rl = build_post_req(req, LLM_REQ_SIZE, gw, body, blen);
    if (rl <= 0 || rl >= LLM_REQ_SIZE) {
        ret = LLM_ERR_SEND;
        goto out;
    }

    /* ---- 写 ---- */
    {
        size_t len = (size_t)rl;
        size_t sent = 0;
        int budget = LLM_RETRY_BUDGET;
        while (sent < len) {
            ret = mbedtls_ssl_write(&g_conn.ssl, (unsigned char *)req + sent, len - sent);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_SEND;
                    goto out;
                }
                continue;
            }
            if (ret <= 0) {
                /* 写失败: 请求未完整送达, 服务端不可能处理(无完整帧), 可安全重试 */
                *stale = 1;
                ret = LLM_ERR_SEND;
                goto out;
            }
            sent += (size_t)ret;
        }
    }

    /* ---- 读: 帧式 ---- */
    {
        int budget = LLM_RETRY_BUDGET;
        int hdr_end = -1;      /* \r\n\r\n 结束位置(body 起点) */
        /* 阶段1: 收齐头部 */
        while (hdr_end < 0) {
            if (acc_len >= acc_size - 1) {
                ret = LLM_ERR_TOOSMALL;    /* 头部都超了, 异常 */
                goto out;
            }
            int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                     (size_t)acc_size - 1 - (size_t)acc_len);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = (acc_len == 0) ? LLM_ERR_NORESP : LLM_ERR_NORESP;
                    goto out;
                }
                continue;
            }
            if (n <= 0) {
                if (acc_len == 0) {
                    *stale = 1;            /* 一个字节都没有: 连接死 */
                }
                ret = LLM_ERR_NORESP;
                goto out;
            }
            acc_len += n;
            acc[acc_len] = '\0';
            const char *bodyp = http_body(acc, acc_len);
            if (bodyp != NULL) {
                hdr_end = (int)(bodyp - acc);
                break;
            }
        }

        /* 解析头部: Content-Length / chunked / Connection: close */
        long clen = -1;
        int chunked = 0;
        int srv_close = 0;
        {
            const char *v = hdr_value(acc, hdr_end, "Content-Length");
            if (v != NULL) {
                clen = strtol(v, NULL, 10);
            }
            v = hdr_value(acc, hdr_end, "Transfer-Encoding");
            if (v != NULL) {
                /* 值域以 \r 结尾; 不精确匹配 chunked 关键字 */
                int vl = 0;
                while (v[vl] && v[vl] != '\r' && v[vl] != '\n' && v[vl] != ',' && v[vl] != ' ') vl++;
                if (vl == 7 && hdr_ieq(v, "chunked", 7)) {
                    chunked = 1;
                }
            }
            v = hdr_value(acc, hdr_end, "Connection");
            if (v != NULL) {
                /* 找 close 子串 */
                for (const char *p = v; *p && *p != '\r' && *p != '\n'; p++) {
                    if (hdr_ieq(p, "close", 5)) { srv_close = 1; break; }
                }
            }
        }

        /* 阶段2: 按 framing 收 body */
        if (clen >= 0) {
            /* Content-Length: 恰好收满 */
            long need = hdr_end + clen;
            while (acc_len < need) {
                if (acc_len >= acc_size - 1) {
                    /* 响应超缓冲: 放弃本连接(不排空), 返回已有内容截断 */
                    srv_close = 1;
                    break;
                }
                int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                         (size_t)acc_size - 1 - (size_t)acc_len);
                if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    if (--budget < 0) break;
                    continue;
                }
                if (n <= 0) break;   /* 服务端提前断: 用已有数据 */
                acc_len += n;
            }
        } else if (chunked) {
            /* chunked: 流式解块(原地, rd 恒 >= wr) */
            int rd = hdr_end;
            int wr = hdr_end;
            for (;;) {
                /* 找一个完整的 chunk-size 行 */
                char *line_end = NULL;
                while ((line_end = memchr(acc + rd, '\n', (size_t)(acc_len - rd))) == NULL) {
                    if (acc_len >= acc_size - 1) { srv_close = 1; goto dech_done; }
                    int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                             (size_t)acc_size - 1 - (size_t)acc_len);
                    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        if (--budget < 0) { srv_close = 1; goto dech_done; }
                        continue;
                    }
                    if (n <= 0) { srv_close = 1; goto dech_done; }
                    acc_len += n;
                }
                long sz = strtol(acc + rd, NULL, 16);
                int line_len = (int)(line_end - (acc + rd)) + 1;   /* 含 \n */
                if (sz == 0) {
                    rd += line_len;
                    /* 0 块后的 trailer: 吞到空行(尽力, 不强求) */
                    for (int guard = 0; guard < 8; guard++) {
                        char *nl = memchr(acc + rd, '\n', (size_t)(acc_len - rd));
                        if (nl == NULL) {
                            if (acc_len >= acc_size - 1) break;
                            int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                                     (size_t)acc_size - 1 - (size_t)acc_len);
                            if (n <= 0) break;
                            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
                            acc_len += n;
                            continue;
                        }
                        int l = (int)(nl - (acc + rd)) + 1;
                        if (l <= 2) break;   /* 空行: chunked 结束 */
                        rd += l;
                    }
                    goto dech_done;
                }
                /* chunk 数据是否已完整(含尾 \r\n) */
                while (acc_len - (rd + line_len) < sz + 2) {
                    if (acc_len >= acc_size - 1) { srv_close = 1; goto dech_done; }
                    int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                             (size_t)acc_size - 1 - (size_t)acc_len);
                    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        if (--budget < 0) { srv_close = 1; goto dech_done; }
                        continue;
                    }
                    if (n <= 0) { srv_close = 1; goto dech_done; }
                    acc_len += n;
                }
                memmove(acc + wr, acc + rd + line_len, (size_t)sz);
                wr += (int)sz;
                rd += line_len + (int)sz + 2;   /* 跳数据与尾 \r\n */
            }
        dech_done:
            acc_len = wr;
            acc[acc_len] = '\0';
        } else {
            /* 无 Content-Length 也非 chunked(异常/1.0 降级): 读到断开 */
            for (;;) {
                if (acc_len >= acc_size - 1) break;
                int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)acc + acc_len,
                                         (size_t)acc_size - 1 - (size_t)acc_len);
                if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    if (--budget < 0) break;
                    continue;
                }
                if (n <= 0) break;
                acc_len += n;
            }
            srv_close = 1;   /* 无法判断边界, 连接不可复用 */
        }

        ret = acc_len;
        if (srv_close) {
            conn_teardown();   /* 读完再拆: 响应数据已齐 */
        }
    }

out:
    if (ret < 0) {
        conn_teardown();   /* 任何失败: 连接不可信, 清掉 */
    }
    free(req);
    return ret;
}

/* ============ keepalive 传输入口: 复用/重试策略(g_lock 内调用) ============ */
static int llm_https_post(const llm_gateway_t *gw, const char *body, int blen,
                          char *acc, int acc_size)
{
    int stale = 0;
    int r = conn_ensure(gw);
    if (r != 0) {
        return r;
    }
    int ret = https_post_once(gw, body, blen, acc, acc_size, &stale);
    if (ret >= 0) {
        g_conn.last_used = time(NULL);
        return ret;
    }
    if (stale) {
        /* 半开连接: 重建一次再试 */
        r = conn_open(gw);
        if (r != 0) {
            return r;
        }
        stale = 0;
        ret = https_post_once(gw, body, blen, acc, acc_size, &stale);
        if (ret >= 0) {
            g_conn.last_used = time(NULL);
            return ret;
        }
    }
    return ret;
}

/* ============ 通道1c: llm_chat_stream — 流式 SSE 增量回调(v1.3) ============ */

/* 流式会话上下文(一次 llm_chat_stream 的全程状态) */
typedef struct {
    llm_stream_cb user_cb;   /* 客户回调(NULL=不要回调) */
    void         *user;
    char         *frag;      /* 片段缓冲(LLS_FRAG_SIZE, malloc) */
    char         *out;       /* 完整回复缓冲(可 NULL=纯回调模式) */
    int           out_len;
    int           out_pos;   /* out 已写长度 */
    int           toosmall;  /* out 溢出标志(回调仍完整送达) */
    int           total;     /* 正文总长(回调口径, 与 out 无关) */
    int           done_seen; /* 已见 data: [DONE] */
    int           canceled;  /* 客户回调请求中止 */
} lstr_ctx_t;

/* SSE data 行处理(协议层回调, 每个 "data:" 事件一次)
 * 返回 0 继续; 非 0 = 请求停止回调(解码器 muted, 继续解析到 chunked 终结) */
static int lstr_on_line(void *user, const char *line, int len)
{
    lstr_ctx_t *c = (lstr_ctx_t *)user;
    if (len == 6 && memcmp(line, "[DONE]", 6) == 0) {
        c->done_seen = 1;
        return 1;
    }
    int fl = llm_sse_content_frag(line, c->frag, LLS_FRAG_SIZE);
    if (fl <= 0) {
        return 0;   /* reasoning/role/usage/finish 事件 或 首事件空片段: 跳过 */
    }
    /* DeepSeek 系模型会把 </think> 思考边界标记泄进 content(实测 V4-Flash,
     * 三种形态: 独立片段 "</think>" / 带残渣 "</think>>" / 游离尖括号 ">")。
     * 处理: 剔除片段内全部 "</think>"; 剩余若纯为 '<'/'>' 残渣则整片丢弃。
     * 已知局限: 标记被 tokenizer 拆到相邻两个片段的情形未处理(未实测出现) */
    for (;;) {
        int pos = -1;
        for (int i = 0; i + 8 <= fl; i++) {
            if (c->frag[i] == '<' && memcmp(c->frag + i, "</think>", 8) == 0) {
                pos = i;
                break;
            }
        }
        if (pos < 0) {
            break;
        }
        memmove(c->frag + pos, c->frag + pos + 8, (size_t)(fl - pos - 8) + 1);
        fl -= 8;
    }
    if (fl == 0) {
        return 0;
    }
    {
        int only_angle = 1;
        for (int i = 0; i < fl; i++) {
            if (c->frag[i] != '<' && c->frag[i] != '>') {
                only_angle = 0;
                break;
            }
        }
        if (only_angle) {
            return 0;   /* 纯尖括号残渣: 丢弃(中文问答场景无合法裸尖括号片段) */
        }
    }
    c->total += fl;
    if (c->out != NULL) {
        if (c->out_pos + fl < c->out_len) {
            memcpy(c->out + c->out_pos, c->frag, (size_t)fl);
            c->out_pos += fl;
            c->out[c->out_pos] = '\0';
        } else {
            c->toosmall = 1;   /* out 满: 回调继续, out 截断 */
        }
    }
    if (c->user_cb != NULL && c->user_cb(c->frag, fl, c->user) != 0) {
        c->canceled = 1;
        return 1;
    }
    return 0;
}

/* 流式 POST 一次(在缓存连接上): 写请求 -> 增量收 -> 逐 token 回调
 * 返回 >=0 正文总长(中止时=已收长度); 负数 LLM_ERR_*
 * *stale = 1: 零响应(复用连接已死), 可安全重试 */
static int https_stream_once(const llm_gateway_t *gw, const char *body, int blen,
                             lstr_ctx_t *c, int *stale)
{
    int ret = LLM_ERR_MEM;
    *stale = 0;
    char *req  = (char *)malloc(LLM_REQ_SIZE);
    char *hbuf = (char *)malloc(1024);            /* 响应头增量缓冲 */
    char *rbuf = (char *)malloc(2048);            /* body 读缓冲 */
    llm_sse_t *sse = (llm_sse_t *)malloc(sizeof(llm_sse_t));
    if (req == NULL || hbuf == NULL || rbuf == NULL || sse == NULL) {
        goto out;
    }

    /* ---- 写(与非流式同一请求格式) ---- */
    int rl = build_post_req(req, LLM_REQ_SIZE, gw, body, blen);
    if (rl <= 0 || rl >= LLM_REQ_SIZE) {
        ret = LLM_ERR_SEND;
        goto out;
    }
    {
        size_t len = (size_t)rl;
        size_t sent = 0;
        int budget = LLM_RETRY_BUDGET;
        while (sent < len) {
            ret = mbedtls_ssl_write(&g_conn.ssl, (unsigned char *)req + sent, len - sent);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_SEND;
                    goto out;
                }
                continue;
            }
            if (ret <= 0) {
                *stale = 1;   /* 请求未完整送达: 可安全重试 */
                ret = LLM_ERR_SEND;
                goto out;
            }
            sent += (size_t)ret;
        }
    }

    /* ---- 头部: 增量收到 \r\n\r\n(同批多读的 body 字节留给解码器) ---- */
    int hlen = 0;
    int body_start = -1;
    {
        int budget = LLM_STREAM_IDLE_BUDGET;
        while (body_start < 0) {
            if (hlen >= 1023) {
                ret = LLM_ERR_STATUS;   /* 头部超长: 异常 */
                goto out;
            }
            int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)hbuf + hlen,
                                     (size_t)(1023 - hlen));
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_NORESP;
                    goto out;
                }
                continue;
            }
            if (n <= 0) {
                if (hlen == 0) {
                    *stale = 1;   /* 零字节: 复用连接已死 */
                }
                ret = LLM_ERR_NORESP;
                goto out;
            }
            hlen += n;
            hbuf[hlen] = '\0';
            const char *b = http_body(hbuf, hlen);
            if (b != NULL) {
                body_start = (int)(b - hbuf);
            }
        }
    }
    if (http_ok(hbuf) != 0) {
        ret = LLM_ERR_STATUS;
        goto out;
    }

    /* 帧型判定: chunked(SSE 正常形态) / Content-Length(兜底) / 裸流读到断开 */
    int chunked = 0;
    long clen = -1;
    {
        const char *v = hdr_value(hbuf, body_start, "Transfer-Encoding");
        if (v != NULL) {
            int vl = 0;
            while (v[vl] && v[vl] != '\r' && v[vl] != '\n' && v[vl] != ',' && v[vl] != ' ') {
                vl++;
            }
            if (vl == 7 && hdr_ieq(v, "chunked", 7)) {
                chunked = 1;
            }
        }
        if (!chunked) {
            v = hdr_value(hbuf, body_start, "Content-Length");
            if (v != NULL) {
                clen = strtol(v, NULL, 10);
                if (clen < 0) {
                    clen = -1;
                }
            }
        }
    }
    llm_sse_init(sse, chunked, lstr_on_line, c);

    /* 头部阶段多读的 body 字节先喂解码器 */
    if (hlen > body_start) {
        int extra = hlen - body_start;
        if (llm_sse_feed(sse, hbuf + body_start, extra) != 0) {
            ret = LLM_ERR_NORESP;   /* 协议错误: 连接已不可信 */
            goto out;
        }
        if (clen >= 0) {
            clen -= extra;
        }
    }

    /* ---- body 增量收发循环 ---- */
    {
        int idle = (c->done_seen) ? LLM_STREAM_GRACE_BUDGET : LLM_STREAM_IDLE_BUDGET;
        for (;;) {
            if (sse->done || c->canceled) {
                break;
            }
            if (clen == 0) {
                sse->done = 1;   /* Content-Length 收满: 干净终结(连接可复用) */
                break;
            }
            int n = mbedtls_ssl_read(&g_conn.ssl, (unsigned char *)rbuf, 2048);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--idle < 0) {
                    /* 空闲超时: 已有内容(或已 DONE)按提前完成, 否则失败 */
                    if (c->total > 0 || c->done_seen) {
                        break;
                    }
                    ret = LLM_ERR_NORESP;
                    goto out;
                }
                continue;
            }
            if (n <= 0) {
                break;   /* 断开: 已有内容按提前完成, 空手则收尾判 NORESP */
            }
            if (llm_sse_feed(sse, rbuf, n) != 0) {
                ret = LLM_ERR_NORESP;
                goto out;
            }
            if (clen > 0) {
                clen -= n;
                if (clen < 0) {
                    clen = 0;
                }
            }
            if (c->done_seen) {
                idle = LLM_STREAM_GRACE_BUDGET;   /* [DONE] 后切宽限, 排干等 chunked 终结 */
            }
        }
    }

    /* ---- 收尾 ---- */
    ret = c->total;
    if (!c->canceled) {
        if (c->total == 0 && !sse->done && !c->done_seen) {
            ret = LLM_ERR_NORESP;     /* 空手而归且非正常终结 */
        }
        if (c->toosmall) {
            ret = LLM_ERR_TOOSMALL;   /* out 截断(回调已完整送达) */
        }
    }
    if (!sse->done) {
        conn_teardown();              /* chunked 未正常终结(含取消): 不可复用 */
    }

out:
    if (ret < 0) {
        conn_teardown();              /* 失败路径: 连接不可信, 清掉(幂等) */
    }
    free(req);
    free(hbuf);
    free(rbuf);
    free(sse);
    return ret;
}

/* 流式传输入口: 复用/重试策略(g_lock 内调用), 与 llm_https_post 对称
 * stale 只在"零响应字节"时置位, 重试时 ctx 无污染, 可安全重发 */
static int llm_https_stream(const llm_gateway_t *gw, const char *body, int blen,
                            lstr_ctx_t *c)
{
    int stale = 0;
    int r = conn_ensure(gw);
    if (r != 0) {
        return r;
    }
    int ret = https_stream_once(gw, body, blen, c, &stale);
    if (ret >= 0) {
        g_conn.last_used = time(NULL);
        return ret;
    }
    if (stale) {
        r = conn_open(gw);   /* 半开连接: 重建一次再试 */
        if (r != 0) {
            return r;
        }
        ret = https_stream_once(gw, body, blen, c, &stale);
        if (ret >= 0) {
            g_conn.last_used = time(NULL);
            return ret;
        }
    }
    return ret;
}

int llm_chat_stream(const llm_gateway_t *gw, const char *message,
                    llm_stream_cb cb, void *user, char *out, int out_len)
{
    if (gw == NULL || gw->ip == NULL || gw->vhost == NULL ||
        gw->api_key == NULL || message == NULL) {
        return LLM_ERR_ARGS;
    }
    if (out != NULL && out_len <= 1) {
        return LLM_ERR_ARGS;
    }

    char *esc  = (char *)malloc(LLM_MSG_MAX + 1);
    char *escj = (char *)malloc(LLM_ESCJ_SIZE);
    char *body = (char *)malloc(LLM_BODY_SIZE);
    lstr_ctx_t c;
    memset(&c, 0, sizeof(c));
    int ret = LLM_ERR_MEM;
    if (esc == NULL || escj == NULL || body == NULL) {
        goto out;
    }
    c.frag = (char *)malloc(LLS_FRAG_SIZE);
    if (c.frag == NULL) {
        goto out;
    }
    c.user_cb = cb;
    c.user = user;
    c.out = out;
    c.out_len = (out != NULL) ? out_len : 0;
    if (out != NULL) {
        out[0] = '\0';
    }

    int mlen = (int)strlen(message);
    if (mlen > LLM_MSG_MAX) {
        mlen = LLM_MSG_MAX;
    }
    memcpy(esc, message, (size_t)mlen);
    esc[mlen] = '\0';
    json_escape(esc, escj, LLM_ESCJ_SIZE);

    int blen = snprintf(body, LLM_BODY_SIZE,
                        "{\"model\":\"%s\",\"stream\":true,\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
                        (gw->model != NULL) ? gw->model : LLM_DEFAULT_MODEL, escj);
    if (blen <= 0 || blen >= LLM_BODY_SIZE) {
        ret = LLM_ERR_ARGS;
        goto out;
    }

    pthread_mutex_lock(&g_lock);
    ret = llm_https_stream(gw, body, blen, &c);
    pthread_mutex_unlock(&g_lock);

out:
    free(esc);
    free(escj);
    free(body);
    free(c.frag);
    return ret;
}

/* ============ 通道1: llm_chat — 客户网关直连 TLS(库内封装 JSON) ============ */
static int llm_chat_locked(const llm_gateway_t *gw, const char *message, char *out, int out_len)
{
    char *esc = (char *)malloc(LLM_MSG_MAX + 1);
    char *escj = (char *)malloc(LLM_ESCJ_SIZE);
    char *body = (char *)malloc(LLM_BODY_SIZE);
    char *acc = (char *)malloc(LLM_ACC_SIZE);
    if (esc == NULL || escj == NULL || body == NULL || acc == NULL) {
        free(esc);
        free(escj);
        free(body);
        free(acc);
        return LLM_ERR_MEM;
    }

    int ret = LLM_ERR_ARGS;

    int mlen = (int)strlen(message);
    if (mlen > LLM_MSG_MAX) {
        mlen = LLM_MSG_MAX;
    }
    memcpy(esc, message, (size_t)mlen);
    esc[mlen] = '\0';
    json_escape(esc, escj, LLM_ESCJ_SIZE);

    int blen = snprintf(body, LLM_BODY_SIZE,
                        "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
                        (gw->model != NULL) ? gw->model : LLM_DEFAULT_MODEL, escj);
    if (blen <= 0 || blen >= LLM_BODY_SIZE) {
        goto buf_out;
    }

    {
        int acc_len = llm_https_post(gw, body, blen, acc, LLM_ACC_SIZE);
        if (acc_len < 0) {
            ret = acc_len;
            goto buf_out;
        }
        acc[acc_len] = '\0';
        ret = finish_reply(acc, acc_len, out, out_len);
    }

buf_out:
    free(esc);
    free(escj);
    free(body);
    free(acc);
    return ret;
}

int llm_chat(const llm_gateway_t *gw, const char *message, char *out, int out_len)
{
    if (gw == NULL || gw->ip == NULL || gw->vhost == NULL ||
        gw->api_key == NULL || message == NULL || out == NULL || out_len <= 1) {
        return LLM_ERR_ARGS;
    }
    pthread_mutex_lock(&g_lock);
    int r = llm_chat_locked(gw, message, out, out_len);
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* ============ 通道1b: llm_chat_raw — 客户自带 JSON body ============ */
static int llm_chat_raw_locked(const llm_gateway_t *gw, const char *json_body,
                               char *out, int out_len)
{
    char *acc = (char *)malloc(LLM_ACC_SIZE);
    if (acc == NULL) {
        return LLM_ERR_MEM;
    }
    int ret;

    int blen = (int)strlen(json_body);
    int acc_len = llm_https_post(gw, json_body, blen, acc, LLM_ACC_SIZE);
    if (acc_len < 0) {
        ret = acc_len;
        goto raw_out;
    }
    acc[acc_len] = '\0';
    ret = finish_raw(acc, acc_len, out, out_len);

raw_out:
    free(acc);
    return ret;
}

int llm_chat_raw(const llm_gateway_t *gw, const char *json_body, char *out, int out_len)
{
    if (gw == NULL || gw->ip == NULL || gw->vhost == NULL ||
        gw->api_key == NULL || json_body == NULL || out == NULL || out_len <= 1) {
        return LLM_ERR_ARGS;
    }
    if (strlen(json_body) > LLM_RAWBODY_MAX) {
        return LLM_ERR_ARGS;
    }
    pthread_mutex_lock(&g_lock);
    int r = llm_chat_raw_locked(gw, json_body, out, out_len);
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* ============ 通道2: llm_ask — PC 中继(免 key, 短连接不复用) ============ */
static int llm_ask_locked(const char *msg, char *out, int out_len)
{
    int ret;
    int fd = -1;
    char *req = (char *)malloc(LLM_ASKREQ_SIZE);
    char *acc = (char *)malloc(LLM_ACC_SIZE);
    if (req == NULL || acc == NULL) {
        free(req);
        free(acc);
        return LLM_ERR_MEM;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ret = LLM_ERR_CONNECT;
        goto ask_out;
    }
    struct timeval tv = { LLM_FD_TIMEOUT_SEC * LLM_RETRY_BUDGET, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)g_relay_port);
    sa.sin_addr.s_addr = inet_addr(g_relay_ip);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ret = LLM_ERR_CONNECT;
        goto ask_out;
    }

    int mlen = (int)strlen(msg);
    if (mlen > LLM_MSG_MAX) {
        mlen = LLM_MSG_MAX;
    }
    int rl = snprintf(req, LLM_ASKREQ_SIZE,
                      "POST /say HTTP/1.0\r\nHost: %s\r\nContent-Type: text/plain\r\n"
                      "Content-Length: %d\r\n\r\n%.*s",
                      g_relay_ip, mlen, mlen, msg);
    if (rl <= 0 || rl >= LLM_ASKREQ_SIZE || send(fd, req, (size_t)rl, 0) < 0) {
        ret = LLM_ERR_SEND;
        goto ask_out;
    }

    int acc_len = 0;
    char buf[2048];
    int n;
    while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        int copy = n;
        if (acc_len + copy > LLM_ACC_SIZE - 1) {
            copy = LLM_ACC_SIZE - 1 - acc_len;
        }
        memcpy(acc + acc_len, buf, (size_t)copy);
        acc_len += copy;
    }
    acc[acc_len] = '\0';
    ret = finish_reply(acc, acc_len, out, out_len);

ask_out:
    if (fd >= 0) {
        close(fd);
    }
    free(req);
    free(acc);
    return ret;
}

int llm_ask(const char *msg, char *out, int out_len)
{
    if (msg == NULL || out == NULL || out_len <= 1) {
        return LLM_ERR_ARGS;
    }
    pthread_mutex_lock(&g_lock);
    int r = llm_ask_locked(msg, out, out_len);
    pthread_mutex_unlock(&g_lock);
    return r;
}
