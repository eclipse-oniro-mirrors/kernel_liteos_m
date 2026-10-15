/* llm_chat_raw.c — libllmclient 1.1 的 llm_chat_raw 原始通道实现(本包补齐)
 * agent 组件(src/kernel/llm.c)依赖 network 目录的 llm_chat_raw 做 LLM 工具调用,
 * 但 network/libllmclient.a 不在本源码树内 —— 此处以 wake_sdk/llm_client.c
 * 的通道1为模板补等价实现: TLS/鉴权/链路完全一致, 差别仅两点:
 *   1. body 由调用方给完整 JSON(不再内部拼 messages), 无 4KB 消息上限
 *   2. response 收 HTTP 正文原文(chat completion JSON, 含 tool_calls),
 *      由调用方自行解析 —— 不做 content 提取
 * 注: 大缓冲一律 malloc(堆) — liteos_a 链接器对超大静态段会错位放置(实测页错误)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
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

#define RAW_REQ_MAX        (32 * 1024)  /* body 上限约 30KB(tools JSON 较大) */
#define RAW_ACC_MAX        (64 * 1024)  /* HTTP 响应累积上限 */
#define RAW_FD_TIMEOUT_SEC 5
#define RAW_RETRY_BUDGET   18           /* 18 x 5s = 90s 总预算 */
#define RAW_WIRE_DEBUG     1            /* 1=打印线上收发摘要 */

static pthread_mutex_t g_raw_lock = PTHREAD_MUTEX_INITIALIZER;

static int raw_locked(const llm_gateway_t *gw, const char *body,
                      char *response, int response_len)
{
    char portstr[8];
    char *req = NULL;
    char *acc = NULL;
    int ret = LLM_ERR_TLS;
    int blen = (int)strlen(body);
    int rl;
    size_t len;
    size_t sent = 0;
    int acc_len = 0;
    int n;
    int budget;
    const char *resp_body = NULL;
    int body_len;

    if (blen <= 0 || blen >= RAW_REQ_MAX) {
        return LLM_ERR_ARGS;
    }
    req = (char *)malloc((size_t)blen + 512);
    acc = (char *)malloc(RAW_ACC_MAX);
    if (req == NULL || acc == NULL) {
        free(req);
        free(acc);
        return LLM_ERR_MEM;
    }

    {
        mbedtls_net_context server_fd;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;

        mbedtls_net_init(&server_fd);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);

        const char *pers = "llm_chat_raw";
        if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  (const unsigned char *)pers, strlen(pers)) != 0) {
            goto tls_out;
        }
        if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            goto tls_out;
        }
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
        if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
            goto tls_out;
        }
        if (mbedtls_ssl_set_hostname(&ssl, gw->vhost) != 0) {
            goto tls_out;
        }
        (void)snprintf(portstr, sizeof(portstr), "%d", (gw->port > 0) ? gw->port : 443);
        if (mbedtls_net_connect(&server_fd, gw->ip, portstr, MBEDTLS_NET_PROTO_TCP) != 0) {
            ret = LLM_ERR_CONNECT;
            goto tls_out;
        }
        {
            struct timeval tv = { RAW_FD_TIMEOUT_SEC, 0 };

            setsockopt(server_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

        budget = RAW_RETRY_BUDGET;
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_NORESP;
                    goto tls_out;
                }
                continue;
            }
            ret = LLM_ERR_TLS;
            goto tls_out;
        }

        rl = snprintf(req, (size_t)blen + 512,
                      "POST /v1/chat/completions HTTP/1.0\r\nHost: %s\r\n"
                      "Content-Type: application/json\r\nAuthorization: Bearer %s\r\n"
                      "Content-Length: %d\r\n\r\n%s",
                      gw->vhost, gw->api_key, blen, body);
        if (rl <= 0 || rl >= blen + 512) {
            ret = LLM_ERR_SEND;
            goto tls_out;
        }
        len = (size_t)rl;
        budget = RAW_RETRY_BUDGET;
        while (sent < len) {
            ret = mbedtls_ssl_write(&ssl, (unsigned char *)req + sent, len - sent);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_SEND;
                    goto tls_out;
                }
                continue;
            }
            if (ret <= 0) {
                ret = LLM_ERR_SEND;
                goto tls_out;
            }
            sent += (size_t)ret;
        }

        budget = RAW_RETRY_BUDGET;
        for (;;) {
            n = mbedtls_ssl_read(&ssl, (unsigned char *)acc + acc_len,
                                 RAW_ACC_MAX - 1 - (size_t)acc_len);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (--budget < 0) {
                    ret = LLM_ERR_NORESP;
                    goto tls_out;
                }
                continue;
            }
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n <= 0) {
                break;
            }
            acc_len += n;
            if (acc_len >= RAW_ACC_MAX - 1) {
                break;
            }
        }
        acc[acc_len] = '\0';
#if RAW_WIRE_DEBUG
        printf("[llmr] 发送 %dB body, 头60: %.60s\r\n", blen, body);
        printf("[llmr]        尾60: %s\r\n", (blen > 60) ? body + blen - 60 : body);
        printf("[llmr] 收到 %dB, 头72: %.72s\r\n", acc_len, acc);
#endif

        if (strncmp(acc, "HTTP/1.0 200", 12) != 0 && strncmp(acc, "HTTP/1.1 200", 12) != 0) {
            ret = LLM_ERR_STATUS;
            goto tls_out;
        }
        for (int i = 0; i + 4 <= acc_len; i++) {
            if (acc[i] == '\r' && acc[i + 1] == '\n' &&
                acc[i + 2] == '\r' && acc[i + 3] == '\n') {
                resp_body = acc + i + 4;
                break;
            }
        }
        if (resp_body == NULL) {
            ret = LLM_ERR_NORESP;
            goto tls_out;
        }
        body_len = acc_len - (int)(resp_body - acc);
        if (body_len >= response_len) {
            memcpy(response, resp_body, (size_t)response_len - 1);
            response[response_len - 1] = '\0';
            ret = LLM_ERR_TOOSMALL;
            goto tls_out;
        }
        memcpy(response, resp_body, (size_t)body_len);
        response[body_len] = '\0';
        ret = body_len;

tls_out:
        mbedtls_net_free(&server_fd);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
    free(req);
    free(acc);
    return ret;
}

int llm_chat_raw(const llm_gateway_t *gw, const char *body,
                 char *response, int response_len)
{
    if (gw == NULL || gw->ip == NULL || gw->vhost == NULL || gw->api_key == NULL ||
        body == NULL || response == NULL || response_len <= 1) {
        return LLM_ERR_ARGS;
    }
    pthread_mutex_lock(&g_raw_lock);
    int r = raw_locked(gw, body, response, response_len);
    pthread_mutex_unlock(&g_raw_lock);
    return r;
}
