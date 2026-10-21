/* llm_stream.c — chunked + SSE 增量解码器 + delta 正文提取(纯协议层实现)
 * 逐字节状态机: 对任意撕裂点(1 字节喂入)天然免疫
 * 两级行缓冲严格独立: meta(chunk-size/trailer 帧层行) 与 buf(SSE 事件行)——
 *   chunk 数据边界可以落在 SSE 事件行中间(实测常见), 帧层状态转换不得触碰
 *   SSE 行装配状态(历史 bug: DLF->SIZE 时误清 s->len, 半行被吞)
 * 性能: ~100KB 响应 = 10万次轻量 switch, Cortex-A7 上可忽略
 */
#include <string.h>
#include <stdlib.h>
#include "llm_stream.h"

/* chunked 状态机状态 */
enum {
    LLS_SIZE = 0,   /* 攒 chunk-size 行(至 \n) */
    LLS_DATA,       /* chunk 数据字节(SSE 行装配) */
    LLS_DCR,        /* chunk 数据尾 \r */
    LLS_DLF,        /* chunk 数据尾 \n */
    LLS_TRAILER,    /* 0 块后: 吞 trailer 行直到空行 */
    LLS_DEAD        /* 正常终结, 后续字节忽略 */
};

/* chunk 大小合理上限(1MB): SSE 事件远小于此, 超过视为流已损坏 */
#define LLS_CHUNK_MAX (1L << 20)

void llm_sse_init(llm_sse_t *s, int chunked, llm_sse_cb cb, void *user)
{
    memset(s, 0, sizeof(*s));
    s->cb = cb;
    s->user = user;
    s->chunked = chunked;
    if (!chunked) {
        s->state = LLS_DATA;   /* 裸流: 无 chunk 包装, 全部字节进 SSE 行装配 */
    }
}

/* 帧层行完结(meta 缓冲): chunk-size 解析 / trailer 空行判定
 * 返回: 0 正常; -1 协议错误(非法 chunk-size) */
static int meta_line_done(llm_sse_t *s)
{
    int l = s->mlen;
    s->mlen = 0;
    if (l > 0 && s->meta[l - 1] == '\r') {
        l--;
    }
    s->meta[l] = '\0';

    if (s->state == LLS_SIZE) {
        /* chunk-size 行: 十六进制, 可带 ";" extension, 可带 \r(已剥) */
        char *end = NULL;
        long sz = strtol(s->meta, &end, 16);
        if (end == s->meta || sz < 0 || sz > LLS_CHUNK_MAX) {
            return -1;
        }
        s->chunk_rem = sz;
        s->state = (sz == 0) ? LLS_TRAILER : LLS_DATA;
        return 0;
    }
    /* LLS_TRAILER: 空行 = chunked 终结; 字段行忽略 */
    if (l == 0) {
        s->done = 1;
        s->state = LLS_DEAD;
    }
    return 0;
}

/* SSE 行完结(buf 缓冲): "data:" 载荷回调; 其他行类型忽略
 * 恒返回 0(SSE 行层无协议错误概念, 载荷内容由上层解释) */
static int line_done(llm_sse_t *s)
{
    int l = s->len;
    s->len = 0;
    if (l > 0 && s->buf[l - 1] == '\r') {
        l--;
    }
    s->buf[l] = '\0';

    if (!s->muted && s->cb != NULL && l > 5 && s->buf[0] == 'd' &&
        strncmp(s->buf, "data:", 5) == 0) {
        const char *line = s->buf + 5;
        int plen = l - 5;
        if (plen > 0 && line[0] == ' ') {   /* 去一个可选前导空格 */
            line++;
            plen--;
        }
        if (s->cb(s->user, line, plen) != 0) {
            s->muted = 1;   /* 停止回调, 继续解析冲到 chunked 终结 */
        }
    }
    return 0;
}

int llm_sse_feed(llm_sse_t *s, const char *data, int n)
{
    for (int i = 0; i < n; i++) {
        char ch = data[i];
        if (s->state == LLS_DEAD) {
            break;
        }

        if (s->chunked && s->state != LLS_DATA) {
            /* 帧层: DCR/DLF/SIZE/TRAILER——不产 SSE 字节, 不碰 s->len */
            if (s->state == LLS_DCR) {
                if (ch == '\r') {
                    s->state = LLS_DLF;      /* 标准 CRLF */
                } else if (ch == '\n') {
                    s->state = LLS_SIZE;     /* 容忍裸 \n: 直接进下一 chunk-size */
                    s->mlen = 0;
                } else {
                    return -1;               /* chunk 数据后既非 \r 也非 \n */
                }
                continue;
            }
            if (s->state == LLS_DLF) {
                if (ch != '\n') {
                    return -1;               /* \r 后必须是 \n */
                }
                s->state = LLS_SIZE;
                s->mlen = 0;
                continue;
            }
            /* LLS_SIZE / LLS_TRAILER: 攒帧层行(meta) */
            if (ch == '\n') {
                if (meta_line_done(s) != 0) {
                    return -1;
                }
                continue;
            }
            if (s->mlen < LLS_META_SIZE - 1) {
                s->meta[s->mlen++] = ch;
            }
            /* 帧层行超长: 静默丢弃(仅 hex 前缀/空行判定有意义) */
            continue;
        }

        /* LLS_DATA(chunked) 或裸流: SSE 行装配, 兼 chunk 计数 */
        if (s->chunked) {
            if (s->chunk_rem <= 0) {
                return -1;   /* 状态机不变式被破坏 */
            }
            s->chunk_rem--;
            if (s->chunk_rem == 0) {
                s->state = LLS_DCR;
            }
        }

        if (ch == '\n') {
            if (s->overflow) {
                s->overflow = 0;   /* 丢弃的行到此为止, 恢复 */
                s->len = 0;
                continue;
            }
            if (line_done(s) != 0) {
                return -1;
            }
            continue;
        }
        if (s->overflow) {
            continue;   /* 超长行丢弃模式 */
        }
        if (s->len >= LLS_BUF_SIZE - 1) {
            s->overflow = 1;
            s->len = 0;
            continue;
        }
        s->buf[s->len++] = ch;
    }
    return 0;
}

int llm_sse_content_frag(const char *line, char *frag, int cap)
{
    const char *p = strstr(line, "\"content\":\"");
    if (p == NULL) {
        return -1;
    }
    p += 11;   /* strlen("\"content\":\"") */
    int o = 0;
    while (*p != '\0' && *p != '"') {
        if (o >= cap - 1) {
            break;   /* 片段截断: 回调语义是拼接, 客户端拼后无损 */
        }
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
            switch (*p) {
            case 'n':  frag[o++] = '\n'; break;
            case 't':  frag[o++] = '\t'; break;
            case 'r':  break;                              /* 丢弃 \r */
            case '"':  frag[o++] = '"';  break;
            case '\\': frag[o++] = '\\'; break;
            case 'u':
                if (p[1] && p[2] && p[3] && p[4]) {
                    p += 4;   /* 跳过 \uXXXX(与 extract_content 口径一致) */
                }
                break;
            default:   frag[o++] = *p; break;
            }
        } else {
            frag[o++] = *p;
        }
        p++;
    }
    frag[o] = '\0';
    return o;
}
