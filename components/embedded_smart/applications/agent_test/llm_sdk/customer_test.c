/* customer_test.c — 模拟客户首次接入的验收用例(v1.3 流式)
 * 完全客户视角: 只 include 公开头文件, 只调公开接口, 含错误处理
 * 用法: customer_test <热点名> <密码>
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "wifi_client.h"
/* v1.3 流式头: 引号包含先搜本文件所在目录(会命中本目录 v1.1 旧头),
 * 必须相对路径直取同级 llm_stream 拷贝 */
#include "../llm_stream/llm_client.h"

/* 流式回调: 每个 token 到达即打印(打字机); 返回 0 继续收流 */
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
    if (argc < 3) {
        printf("usage: customer_test <ssid> <psk>\r\n");
        return 1;
    }

    printf("[customer] 步骤1: 连接 WiFi (%s)\r\n", argv[1]);
    int r = wifi_connect_ap(argv[1], argv[2]);
    if (r != 0) {
        printf("[customer] wifi_connect_ap 失败: %d\r\n", r);
        return 1;
    }

    printf("[customer] 步骤2: 等待链路(最多30s)...\r\n");
    char ip[32];
    r = wifi_wait_connected(30, ip, sizeof(ip));
    if (r != 0) {
        printf("[customer] 等待超时: %d (热点在线吗?)\r\n", r);
        return 1;
    }
    printf("[customer] WiFi 已连: wlan0 = %s\r\n", ip);

    printf("[customer] 步骤3: 问大模型(TLS直连, SSE流式打字机)...\r\n");
    llm_gateway_t gw = {
        .ip = "47.102.215.139",
        .vhost = "api.siliconflow.cn",
        .port = 443,
        .api_key = "sk-jcojwurdcgxlgtrgaiuibttmopjzzxxrmnrfoieawqpkqwrj",
        .model = "deepseek-ai/DeepSeek-V4-Flash",
    };
    char reply[4096];
    time_t t0 = time(NULL);
    r = llm_chat_stream(&gw, "你好，我是客户模块的首次接入验收测试，请用一句话确认收到。",
                        on_token, NULL, reply, sizeof(reply));
    if (r < 0) {
        printf("\r\n[customer] llm_chat_stream 失败: %d\r\n", r);
        if (reply[0] != '\0') {
            printf("[customer] 已收部分:\r\n%s\r\n", reply);
        }
        return 1;
    }
    printf("\r\n[customer] 大模型回复(%d字, 耗时%lds), 流式增量已实时打印\r\n",
           r, (long)(time(NULL) - t0));
    printf("[customer] === 验收通过 ===\r\n");
    return 0;
}
