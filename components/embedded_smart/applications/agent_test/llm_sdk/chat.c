/* chat.c — llm_client 库的官方示例(CLI 封装, v1.3 流式)
 * 演示镜像内模块如何使用 llm_chat_stream(): SSE 增量收包, token 到达即打印
 * (打字机效果), 完整回复同时落在缓冲区。
 * 注: PC 中继通道 llm_ask 不支持流式, 本示例走客户网关 TLS 直连;
 *     网关配置在下方 g_gw(换服务商时同步修改), key 同 customer_test.c。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
/* v1.3 流式头(相对路径直取同级 llm_stream 拷贝; "llm_client.h" 会命中本目录
 * v1.1 旧头 —— 引号包含先搜本文件所在目录, -I 顺序救不了) */
#include "../llm_stream/llm_client.h"

/* 演示网关(同 customer_test.c) */
static const llm_gateway_t g_gw = {
    .ip = "47.102.215.139",
    .vhost = "api.siliconflow.cn",
    .port = 443,
    .api_key = "sk-jcojwurdcgxlgtrgaiuibttmopjzzxxrmnrfoieawqpkqwrj",
    .model = "deepseek-ai/DeepSeek-V4-Flash",
};

/* 流式回调: 每个 token 到达即打印; 返回 0 继续收流, 非 0 = 请求中止 */
static int on_token(const char *token, int len, void *user)
{
    (void)user;
    if (len > 0) {
        fwrite(token, 1, (size_t)len, stdout);
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: chat <message...>\r\n");
        return 1;
    }
    static char msg[1024];
    int len = 0;
    for (int i = 1; i < argc; i++) {
        int n = snprintf(msg + len, sizeof(msg) - len, "%s%s",
                         (i > 1) ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(msg) - (size_t)len) {
            break;
        }
        len += n;
    }
    printf("[chat] >> %s\r\n", msg);

    static char reply[4096];
    time_t t0 = time(NULL);
    int r = llm_chat_stream(&g_gw, msg, on_token, NULL, reply, sizeof(reply));
    if (r < 0) {
        printf("\r\n[chat] error %d\r\n", r);
        if (reply[0] != '\0') {
            printf("[chat] partial:\r\n%s\r\n", reply);
        }
        return 1;
    }
    printf("\r\n[chat] done (%d chars, %lds)\r\n",
           r, (long)(time(NULL) - t0));
    return 0;
}
