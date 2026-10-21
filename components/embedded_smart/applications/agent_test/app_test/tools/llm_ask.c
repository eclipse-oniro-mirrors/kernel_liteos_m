/* llm_ask.c v2 - 一次性 LLM 问答（参数由 shell 传入，不读串口）
 * 三种问法:
 *   exec /bin/llm_ask hello world            ASCII 直接问
 *   exec /bin/llm_ask uni 4f60 597d          Unicode 码点模式(中文推荐): "你好"
 *   exec /bin/llm_ask hex e4bda0e5a5bd       UTF-8 字节 hex 模式
 * 中文转码点: 任意"中文转Unicode"工具, 你好=4f60 597d, 每字一组可多个参数
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "wifi_client.h"
#include "llm_client.h"

#define GW_IP    "47.102.215.139"
#define GW_VHOST "api.siliconflow.cn"
#define GW_PORT  443
#define GW_KEY   "sk-jcojwurdcgxlgtrgaiuibttmopjzzxxrmnrfoieawqpkqwrj"
#define GW_MODEL "deepseek-ai/DeepSeek-V4-Flash"

#define Q_MAX 2048
#define R_MAX 4096

static const char *llm_errstr(int e)
{
    switch (e) {
    case LLM_ERR_CONNECT:  return "connect gateway failed";
    case LLM_ERR_SEND:     return "send failed";
    case LLM_ERR_NORESP:   return "no response / timeout (internal 90s)";
    case LLM_ERR_STATUS:   return "HTTP non-200 (key invalid / quota out)";
    case LLM_ERR_TOOSMALL: return "reply buffer too small (truncated)";
    case LLM_ERR_ARGS:     return "invalid args";
    case LLM_ERR_TLS:      return "TLS handshake failed";
    case LLM_ERR_MEM:      return "out of memory";
    default:               return "unknown error";
    }
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

static int put_byte(char *buf, size_t size, size_t *used, unsigned char b)
{
    if (*used + 1 >= size) {
        return -1;
    }
    buf[(*used)++] = (char)b;
    buf[*used] = '\0';
    return 0;
}

static int put_cp(char *buf, size_t size, size_t *used, unsigned int cp)
{
    unsigned char tmp[4];
    int n, i;
    if (cp < 0x80) {
        tmp[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (unsigned char)(0xc0 | (cp >> 6));
        tmp[1] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (unsigned char)(0xe0 | (cp >> 12));
        tmp[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        tmp[2] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        tmp[0] = (unsigned char)(0xf0 | (cp >> 18));
        tmp[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        tmp[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        tmp[3] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 4;
    }
    for (i = 0; i < n; i++) {
        if (put_byte(buf, size, used, tmp[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    static char q[Q_MAX];
    static char rep[R_MAX];
    llm_gateway_t gw;
    size_t used = 0;
    time_t t0;
    char ip[32];
    int n, i;
    const char *mode = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    q[0] = '\0';
    if (argc >= 2) {
        mode = argv[1];
    }
    if (mode != NULL && strcmp(mode, "uni") == 0) {
        for (i = 2; i < argc && argv[i] != NULL; i++) {
            const char *s = argv[i];
            while (*s) {
                unsigned int cp = 0;
                int d, k;
                for (k = 0; k < 4; k++) {
                    if (!*s) {
                        break;
                    }
                    d = hexval(*s);
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
                if (put_cp(q, Q_MAX, &used, cp) != 0) {
                    break;
                }
            }
        }
    } else if (mode != NULL && strcmp(mode, "hex") == 0) {
        for (i = 2; i < argc && argv[i] != NULL; i++) {
            const char *s = argv[i];
            while (s[0] && s[1]) {
                int hi = hexval(s[0]);
                int lo = hexval(s[1]);
                if (hi < 0 || lo < 0) {
                    s++;
                    continue;
                }
                if (put_byte(q, Q_MAX, &used,
                             (unsigned char)((hi << 4) | lo)) != 0) {
                    break;
                }
                s += 2;
            }
        }
    } else {
        for (i = 1; i < argc && argv[i] != NULL; i++) {
            size_t len = strlen(argv[i]);
            if (used + len + 2 >= Q_MAX) {
                break;
            }
            if (used > 0) {
                q[used++] = ' ';
            }
            memcpy(q + used, argv[i], len);
            used += len;
            q[used] = '\0';
        }
    }
    if (used == 0) {
        printf("[llm] usage:\r\n");
        printf("  exec /bin/llm_ask hello world      (ASCII)\r\n");
        printf("  exec /bin/llm_ask uni 4f60 597d    (unicode, ni hao)\r\n");
        printf("  exec /bin/llm_ask hex e4bda0e5a5bd (utf8 hex)\r\n");
        return 1;
    }
    if (wifi_get_ip(ip, sizeof(ip)) != 0) {
        printf("[wifi] not connected (run: exec /bin/wifi_cli apx <ssid> <psk> then wait)\r\n");
        return 1;
    }
    printf("[wifi] wlan0 = %s\r\n", ip);
    gw.ip = GW_IP;
    gw.vhost = GW_VHOST;
    gw.port = GW_PORT;
    gw.api_key = GW_KEY;
    gw.model = GW_MODEL;
    printf("[llm] gw=%s:%d %s model=%s\r\n", gw.ip, gw.port, gw.vhost, gw.model);
    printf("[llm] >> %s\r\n", q);
    t0 = time(NULL);
    n = llm_chat(&gw, q, rep, R_MAX);
    printf("[llm] elapsed %lds\r\n", (long)(time(NULL) - t0));
    if (n >= 0) {
        printf("[llm] << reply(%d):\r\n%s\r\n", n, rep);
        return 0;
    }
    printf("[llm] FAIL %d (%s)\r\n", n, llm_errstr(n));
    if (n == LLM_ERR_TOOSMALL && rep[0]) {
        printf("[llm] truncated reply:\r\n%s\r\n", rep);
    }
    return 1;
}
