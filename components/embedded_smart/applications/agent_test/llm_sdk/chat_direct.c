/* chat_direct.c — llm_chat_stream() 客户网关直连流式示例(SSE 打字机输出)
 * 用法: chat_direct <gw_ip> <vhost> <api_key> <message...>          (默认模型)
 *       chat_direct <gw_ip> <vhost> <api_key> <model> <message...>  (指定模型)
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
/* v1.3 流式头: 引号包含先搜本文件所在目录(会命中本目录 v1.1 旧头),
 * 必须相对路径直取同级 llm_stream 拷贝 */
#include "../llm_stream/llm_client.h"

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
    if (argc < 5) {
        printf("usage: chat_direct <gw_ip> <vhost> <api_key> <message...>\r\n");
        printf("       chat_direct <gw_ip> <vhost> <api_key> <model> <message...>\r\n");
        return 1;
    }
    llm_gateway_t gw;
    memset(&gw, 0, sizeof(gw));
    gw.ip = argv[1];
    gw.vhost = argv[2];
    gw.api_key = argv[3];
    gw.port = 443;

    static char msg[1024];
    int len = 0;
    int first;
    if (argc == 5) {
        gw.model = NULL;
        first = 4;
    } else {
        gw.model = argv[4];
        first = 5;
    }
    for (int i = first; i < argc; i++) {
        int n = snprintf(msg + len, sizeof(msg) - len, "%s%s",
                         (i > first) ? " " : "", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(msg) - (size_t)len) {
            break;
        }
        len += n;
    }
    printf("[direct] gw=%s:%d vhost=%s model=%s\r\n", gw.ip, gw.port, gw.vhost,
           (gw.model != NULL) ? gw.model : "(default)");
    printf("[direct] >> %s\r\n", msg);

    static char reply[4096];
    time_t t0 = time(NULL);
    int r = llm_chat_stream(&gw, msg, on_token, NULL, reply, sizeof(reply));
    if (r < 0) {
        printf("\r\n[direct] error %d\r\n", r);
        if (reply[0] != '\0') {
            printf("[direct] partial:\r\n%s\r\n", reply);
        }
        return 1;
    }
    printf("\r\n[direct] done (%d chars, %lds)\r\n",
           r, (long)(time(NULL) - t0));
    return 0;
}
