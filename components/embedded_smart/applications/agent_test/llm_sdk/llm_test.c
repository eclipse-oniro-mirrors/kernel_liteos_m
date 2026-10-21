/* llm_test.c - llm_sdk 全接口串口交互测试工具(v1.3 流式)
 * 运行: exec /bin/llm_test [gw_ip] [vhost] [api_key] [model]   ("-" 跳过该项)
 * 输入输出走串口控制台; 另提供 /dev/uartdev-N 透传桥观察原始收发字节
 * 问答接口: gw chat / gw direct 走 llm_chat_stream(SSE 增量, 打字机输出);
 *           relay ask 仍走 llm_ask(PC 中继不支持流式)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "wifi_client.h"
/* v1.3 流式头: 引号包含先搜本文件所在目录(会命中本目录 v1.1 旧头),
 * 必须相对路径直取同级 llm_stream 拷贝 */
#include "../llm_stream/llm_client.h"

#define CONSOLE_IOC_MAGIC              'c'
#define CONSOLE_CONTROL_RIGHTS_CAPTURE _IO(CONSOLE_IOC_MAGIC, 3)
#define CONSOLE_CONTROL_RIGHTS_RELEASE _IO(CONSOLE_IOC_MAGIC, 4)
#define CONSOLE_CONTROL_CAPTURE_LINE   _IO(CONSOLE_IOC_MAGIC, 5)
#define CONSOLE_CONTROL_CAPTURE_CHAR   _IO(CONSOLE_IOC_MAGIC, 6)

#define VER_STR    "llm_test 1.3"
#define MSG_MAX    2048
#define RPY_MAX    4096
#define BRIDGE_ESC 0x1d

static char g_gw_ip[64];
static char g_gw_vhost[128];
static char g_gw_key[256];
static char g_gw_model[128];
static int  g_gw_port = 443;
static char g_wifi_ssid[64];
static char g_wifi_psk[64];
static char g_wifi_conf[128];
static char g_wifi_daemon[128];
static char g_relay_ip[64];
static int  g_relay_port = 8443;

static char g_uart_dev[64];
static int  g_uart_fd = -1;
static int  g_uart_baud = 115200;
static int  g_uart_hex;
static unsigned long g_uart_tx;
static unsigned long g_uart_rx;

static char g_msg[MSG_MAX];
static char g_reply[RPY_MAX];

static int g_raw_console;

/* 流式回调: token 到达即上串口(打字机); 返回 0 继续收流
 * stdout 已 setvbuf 无缓冲, 每片直接可见; 内容含 % 不能走 printf */
static int stream_echo(const char *token, int len, void *user)
{
    (void)user;
    if (len > 0) {
        fwrite(token, 1, (size_t)len, stdout);
    }
    return 0;
}

static void console_raw_on(void)
{
    if (ioctl(0, CONSOLE_CONTROL_RIGHTS_CAPTURE, 0) != 0) {
        printf("[warn] console rights capture fail (shell may steal input)\r\n");
    }
    if (ioctl(0, CONSOLE_CONTROL_CAPTURE_CHAR, 0) == 0) {
        g_raw_console = 1;
    } else {
        printf("[warn] console CAPTURE_CHAR fail (echo may double)\r\n");
    }
}

static void console_line_restore(void)
{
    if (g_raw_console) {
        ioctl(0, CONSOLE_CONTROL_CAPTURE_LINE, 0);
        g_raw_console = 0;
    }
    ioctl(0, CONSOLE_CONTROL_RIGHTS_RELEASE, 0);
}

static const char *llm_errstr(int e)
{
    switch (e) {
    case LLM_ERR_CONNECT:
        return "connect gateway failed";
    case LLM_ERR_SEND:
        return "send failed";
    case LLM_ERR_NORESP:
        return "no response / timeout (stream: 60s idle, non-stream 90s)";
    case LLM_ERR_STATUS:
        return "HTTP non-200 (key invalid / quota out)";
    case LLM_ERR_TOOSMALL:
        return "reply buffer too small (truncated)";
    case LLM_ERR_ARGS:
        return "invalid args";
    case LLM_ERR_TLS:
        return "TLS handshake failed";
    case LLM_ERR_MEM:
        return "out of memory";
    default:
        return "unknown error";
    }
}

static const char *wifi_errstr(int e)
{
    switch (e) {
    case WIFI_ERR_ARGS:
        return "invalid args";
    case WIFI_ERR_FORK:
        return "fork failed";
    case WIFI_ERR_DAEMON:
        return "daemon binary missing (/bin/wifi_wpa)";
    case WIFI_ERR_NOIP:
        return "not connected / wait timeout";
    case WIFI_ERR_IF:
        return "netdev query failed";
    default:
        return "unknown error";
    }
}

static void print_reply(int n, const char *tag)
{
    if (n >= 0) {
        printf("[%s] reply %d chars:\r\n%s\r\n", tag, n, g_reply);
    } else {
        printf("[%s] FAIL %d (%s)\r\n", tag, n, llm_errstr(n));
        if (n == LLM_ERR_TOOSMALL && g_reply[0]) {
            printf("[%s] truncated reply:\r\n%s\r\n", tag, g_reply);
        }
    }
}

/* 流式结果汇总: 正文已由 stream_echo 实时打印, 这里只报收尾状态 */
static void print_stream_done(int n, const char *tag, long secs)
{
    if (n >= 0) {
        printf("\r\n[%s] done %d chars (%lds)\r\n", tag, n, secs);
    } else {
        printf("\r\n[%s] FAIL %d (%s)\r\n", tag, n, llm_errstr(n));
        if (g_reply[0]) {
            printf("[%s] partial reply:\r\n%s\r\n", tag, g_reply);
        }
    }
}

static void print_show(void)
{
    printf("gateway: ip=%s vhost=%s port=%d\r\n", g_gw_ip, g_gw_vhost, g_gw_port);
    printf("         model=%s key=%s\r\n", g_gw_model,
           g_gw_key[0] ? "(set)" : "(EMPTY, use: gw set key sk-xxx)");
    printf("relay:   %s:%d (mirror, real state inside lib)\r\n", g_relay_ip, g_relay_port);
    printf("wifi:    conf=%s\r\n",
           g_wifi_conf[0] ? g_wifi_conf : "(lib default /storage/wpa_supplicant.conf)");
    printf("         daemon=%s\r\n",
           g_wifi_daemon[0] ? g_wifi_daemon : "(lib default /bin/wifi_wpa)");
    if (g_wifi_ssid[0]) {
        printf("         ap=%s psk=%s\r\n", g_wifi_ssid,
               g_wifi_psk[0] ? "(set)" : "(unset)");
    }
}

static void print_errs(void)
{
    printf("llm_client:\r\n");
    printf("  -1 %s\r\n  -2 %s\r\n  -3 %s\r\n  -4 %s\r\n",
           llm_errstr(LLM_ERR_CONNECT), llm_errstr(LLM_ERR_SEND),
           llm_errstr(LLM_ERR_NORESP), llm_errstr(LLM_ERR_STATUS));
    printf("  -5 %s\r\n  -6 %s\r\n  -7 %s\r\n  -8 %s\r\n",
           llm_errstr(LLM_ERR_TOOSMALL), llm_errstr(LLM_ERR_ARGS),
           llm_errstr(LLM_ERR_TLS), llm_errstr(LLM_ERR_MEM));
    printf("wifi_client:\r\n");
    printf("  -1 %s\r\n  -2 %s\r\n  -3 %s\r\n  -4 %s\r\n  -5 %s\r\n",
           wifi_errstr(WIFI_ERR_ARGS), wifi_errstr(WIFI_ERR_FORK),
           wifi_errstr(WIFI_ERR_DAEMON), wifi_errstr(WIFI_ERR_NOIP),
           wifi_errstr(WIFI_ERR_IF));
}

static void print_help(void)
{
    printf("llm_sdk serial test console, all public APIs covered:\r\n");
    printf("wifi conf [path|clear]      show/set conf for wifi_connect\r\n");
    printf("wifi connect                wifi_connect (NULL = lib default conf)\r\n");
    printf("wifi ap <ssid> <psk>        wifi_connect_ap (writes conf, spawns daemon)\r\n");
    printf("wifi daemon [path|reset]    wifi_set_daemon (default /bin/wifi_wpa)\r\n");
    printf("wifi ip                     wifi_get_ip (instant)\r\n");
    printf("wifi wait [sec]             wifi_wait_connected (default 30s)\r\n");
    printf("wifi creds <ssid> <psk>     store ap for 'wifi demo'\r\n");
    printf("wifi demo                   connect_ap(creds) + wait 30s\r\n");
    printf("relay set <ip> <port>       llm_set_server (PC relay for llm_ask)\r\n");
    printf("relay reset                 llm_set_server back to 192.168.1.1:8443\r\n");
    printf("relay ask [msg]             llm_ask (no msg = type at next line; non-stream)\r\n");
    printf("gw show                     show current config\r\n");
    printf("gw set ip|vhost|port|key|model <v>\r\n");
    printf("gw chat [msg]               llm_chat_stream (SSE typewriter) with config\r\n");
    printf("gw direct <ip> <vhost> <port> <key> <model|-> [msg]\r\n");
    printf("                            one-shot llm_chat_stream, model '-' = default\r\n");
    printf("uart list                   probe /dev/uartdev-0..3 (0 = console)\r\n");
    printf("uart open <N|dev> [baud]    open uart for raw tx/rx\r\n");
    printf("uart baud <n>               change baud on the fly\r\n");
    printf("uart send text <s>          send raw text\r\n");
    printf("uart send line <s>          send raw text + CRLF\r\n");
    printf("uart send hex AA BB ..      send raw bytes\r\n");
    printf("uart rx [ms]                one-shot receive window (default 1000)\r\n");
    printf("uart mode text|hex          rx display mode\r\n");
    printf("uart bridge                 transparent console<->uart bridge\r\n");
    printf("                            press Ctrl+] (0x1d) to exit\r\n");
    printf("uart close                  close uart\r\n");
    printf("uart status                 show uart state and byte counters\r\n");
    printf("errs | show | ver | quit    error tables / config / version / exit\r\n");
    printf("tips: msg cap ~1KB, reply buf 4096, gateway must be IP not domain\r\n");
}

static speed_t baud_const(int baud)
{
    switch (baud) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 921600:
        return B921600;
    default:
        return B115200;
    }
}

static void uart_apply(int baud)
{
    struct termios t;
    if (g_uart_fd < 0) {
        printf("[uart] not open, use: uart open <N> [baud]\r\n");
        return;
    }
    if (tcgetattr(g_uart_fd, &t) != 0) {
        printf("[uart] tcgetattr fail (HCS baudrate kept)\r\n");
        return;
    }
    cfmakeraw(&t);
    cfsetispeed(&t, baud_const(baud));
    cfsetospeed(&t, baud_const(baud));
    if (tcsetattr(g_uart_fd, TCSANOW, &t) == 0) {
        g_uart_baud = baud;
        printf("[uart] baud -> %d\r\n", baud);
    } else {
        printf("[uart] tcsetattr fail (HCS baudrate kept)\r\n");
    }
}

static void uart_open_dev(const char *dev, int baud)
{
    char path[64];
    int fd;
    if (isdigit((unsigned char)dev[0])) {
        snprintf(path, sizeof(path), "/dev/uartdev-%s", dev);
    } else {
        snprintf(path, sizeof(path), "%s", dev);
    }
    if (g_uart_fd >= 0) {
        close(g_uart_fd);
    }
    fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("[uart] open %s fail: %s\r\n", path, strerror(errno));
        return;
    }
    g_uart_fd = fd;
    snprintf(g_uart_dev, sizeof(g_uart_dev), "%s", path);
    g_uart_tx = 0;
    g_uart_rx = 0;
    printf("[uart] %s opened (fd=%d)\r\n", path, fd);
    uart_apply(baud > 0 ? baud : g_uart_baud);
}

static void hexdump(const unsigned char *p, int n)
{
    for (int i = 0; i < n; i += 16) {
        printf("%04x  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < n) {
                printf("%02x ", p[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) {
                printf(" ");
            }
        }
        printf(" |");
        for (int j = 0; j < 16 && i + j < n; j++) {
            unsigned char c = p[i + j];
            putchar((c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("|\r\n");
    }
}

static void uart_show_rx(const unsigned char *buf, int n)
{
    if (n <= 0) {
        return;
    }
    if (g_uart_hex) {
        hexdump(buf, n);
    } else {
        printf("[RX] ");
        fwrite(buf, 1, (size_t)n, stdout);
        printf("\r\n");
    }
}

static int console_readline(const char *prompt, char *buf, int size)
{
    int len = 0;
    int eof_streak = 0;
    printf("%s", prompt);
    fflush(stdout);
    buf[0] = '\0';
    for (;;) {
        unsigned char ch;
        struct pollfd p;
        int r;
        ssize_t n;
        p.fd = 0;
        p.events = POLLIN;
        r = poll(&p, 1, 500);
        if (r <= 0) {
            continue;
        }
        n = read(0, &ch, 1);
        if (n <= 0) {
            if (++eof_streak > 600) {
                break;
            }
            usleep(10000);
            continue;
        }
        eof_streak = 0;
        if (ch == '\r' || ch == '\n') {
            printf("\r\n");
            break;
        }
        if (ch == 0x08 || ch == 0x7f) {
            if (len > 0) {
                len--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (ch < 0x20) {
            continue;
        }
        if (len < size - 1) {
            buf[len++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
    buf[len] = '\0';
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
    return len;
}

static int split(char *line, char **v, int maxv)
{
    int n = 0;
    char *p = line;
    while (*p != '\0' && n < maxv) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        v[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return n;
}

static void cmd_uart(void)
{
    char *sub = strtok(NULL, " ");
    if (sub == NULL || strcmp(sub, "help") == 0) {
        printf("uart list/open/baud/send/rx/mode/bridge/close/status\r\n");
        printf("example: uart open 1 9600 ; uart bridge ; Ctrl+] exits\r\n");
        return;
    }
    if (strcmp(sub, "list") == 0) {
        for (int i = 0; i < 4; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/uartdev-%d", i);
            printf("%s  %s%s\r\n", path,
                   access(path, F_OK) == 0 ? "exists" : "absent",
                   i == 0 ? "  (console, do not touch)" : "");
        }
        return;
    }
    if (strcmp(sub, "open") == 0) {
        char *d = strtok(NULL, " ");
        char *b = strtok(NULL, " ");
        if (d == NULL) {
            printf("usage: uart open <N|dev> [baud]\r\n");
            return;
        }
        uart_open_dev(d, b ? atoi(b) : 0);
        return;
    }
    if (strcmp(sub, "baud") == 0) {
        char *b = strtok(NULL, " ");
        if (b == NULL) {
            printf("usage: uart baud <n>\r\n");
            return;
        }
        uart_apply(atoi(b));
        return;
    }
    if (strcmp(sub, "send") == 0) {
        char *kind = strtok(NULL, " ");
        unsigned char out[512];
        int n;
        int w;
        if (kind == NULL) {
            printf("usage: uart send text|line|hex <..>\r\n");
            return;
        }
        if (g_uart_fd < 0) {
            printf("[uart] not open\r\n");
            return;
        }
        if (strcmp(kind, "hex") == 0) {
            char *tok;
            n = 0;
            while ((tok = strtok(NULL, " ")) != NULL && n < (int)sizeof(out)) {
                out[n++] = (unsigned char)strtoul(tok, NULL, 16);
            }
            if (n == 0) {
                printf("usage: uart send hex AA BB ..\r\n");
                return;
            }
        } else {
            char *rest = strtok(NULL, "");
            if (rest == NULL) {
                printf("usage: uart send text|line <string>\r\n");
                return;
            }
            n = snprintf((char *)out, sizeof(out) - 2, "%s", rest);
            if (n < 0) {
                n = 0;
            }
            if (n > (int)sizeof(out) - 3) {
                n = (int)sizeof(out) - 3;
            }
            if (strcmp(kind, "line") == 0) {
                out[n++] = '\r';
                out[n++] = '\n';
            }
        }
        w = write(g_uart_fd, out, (size_t)n);
        if (w < 0) {
            printf("[TX] fail: %s\r\n", strerror(errno));
            return;
        }
        g_uart_tx += (unsigned long)w;
        printf("[TX] %d/%d bytes\r\n", w, n);
        if (g_uart_hex) {
            hexdump(out, w);
        } else {
            printf("[TX] content: ");
            fwrite(out, 1, (size_t)w, stdout);
            printf("\r\n");
        }
        return;
    }
    if (strcmp(sub, "rx") == 0) {
        char *ms = strtok(NULL, " ");
        int win = ms ? atoi(ms) : 1000;
        time_t end;
        unsigned char buf[256];
        int total = 0;
        if (g_uart_fd < 0) {
            printf("[uart] not open\r\n");
            return;
        }
        if (win <= 0) {
            win = 1000;
        }
        printf("[uart] rx window %d ms (mode=%s)\r\n", win, g_uart_hex ? "hex" : "text");
        end = time(NULL) + (win + 999) / 1000;
        for (;;) {
            struct pollfd p;
            int r;
            p.fd = g_uart_fd;
            p.events = POLLIN;
            r = poll(&p, 1, 200);
            if (r > 0 && (p.revents & POLLIN)) {
                int n = read(g_uart_fd, buf, sizeof(buf));
                if (n > 0) {
                    total += n;
                    g_uart_rx += (unsigned long)n;
                    uart_show_rx(buf, n);
                }
            }
            if (time(NULL) >= end) {
                break;
            }
        }
        printf("[uart] rx done, %d bytes\r\n", total);
        return;
    }
    if (strcmp(sub, "mode") == 0) {
        char *m = strtok(NULL, " ");
        if (m == NULL || (strcmp(m, "hex") != 0 && strcmp(m, "text") != 0)) {
            printf("usage: uart mode text|hex (now %s)\r\n", g_uart_hex ? "hex" : "text");
            return;
        }
        g_uart_hex = (m[0] == 'h');
        printf("[uart] rx mode -> %s\r\n", g_uart_hex ? "hex" : "text");
        return;
    }
    if (strcmp(sub, "bridge") == 0) {
        unsigned char buf[256];
        if (g_uart_fd < 0) {
            printf("[uart] not open, use: uart open <N> [baud]\r\n");
            return;
        }
        printf("[bridge] ON dev=%s baud=%d mode=%s\r\n",
               g_uart_dev, g_uart_baud, g_uart_hex ? "hex" : "text");
        printf("[bridge] keys go to uart, uart shows here, Ctrl+] exits\r\n");
        for (;;) {
            struct pollfd pf[2];
            int r;
            pf[0].fd = 0;
            pf[0].events = POLLIN;
            pf[1].fd = g_uart_fd;
            pf[1].events = POLLIN;
            r = poll(pf, 2, 200);
            if (r < 0) {
                break;
            }
            if (pf[0].revents & POLLIN) {
                int n = read(0, buf, sizeof(buf));
                int esc = 0;
                for (int i = 0; i < n; i++) {
                    if (buf[i] == BRIDGE_ESC) {
                        esc = 1;
                        n = i;
                        break;
                    }
                }
                if (n > 0) {
                    write(g_uart_fd, buf, (size_t)n);
                    g_uart_tx += (unsigned long)n;
                    if (g_uart_hex) {
                        printf("[TX] ");
                        hexdump(buf, n);
                    } else {
                        fwrite(buf, 1, (size_t)n, stdout);
                    }
                }
                if (esc) {
                    break;
                }
            }
            if (pf[1].revents & POLLIN) {
                int n = read(g_uart_fd, buf, sizeof(buf));
                if (n > 0) {
                    g_uart_rx += (unsigned long)n;
                    uart_show_rx(buf, n);
                }
            }
        }
        printf("\r\n[bridge] OFF tx=%lu rx=%lu\r\n", g_uart_tx, g_uart_rx);
        return;
    }
    if (strcmp(sub, "close") == 0) {
        if (g_uart_fd >= 0) {
            close(g_uart_fd);
            g_uart_fd = -1;
            printf("[uart] closed\r\n");
        } else {
            printf("[uart] not open\r\n");
        }
        return;
    }
    if (strcmp(sub, "status") == 0) {
        printf("dev=%s fd=%d baud=%d mode=%s tx=%lu rx=%lu\r\n",
               g_uart_dev, g_uart_fd, g_uart_baud, g_uart_hex ? "hex" : "text",
               g_uart_tx, g_uart_rx);
        return;
    }
    printf("unknown uart sub: %s\r\n", sub);
}

static void cmd_wifi(void)
{
    char *sub = strtok(NULL, " ");
    if (sub == NULL || strcmp(sub, "help") == 0) {
        printf("wifi conf/connect/ap/daemon/ip/wait/creds/demo\r\n");
        return;
    }
    if (strcmp(sub, "conf") == 0) {
        char *p = strtok(NULL, " ");
        if (p == NULL) {
            printf("wifi conf = %s\r\n",
                   g_wifi_conf[0] ? g_wifi_conf : "(lib default)");
            return;
        }
        if (strcmp(p, "clear") == 0) {
            g_wifi_conf[0] = '\0';
            printf("[wifi] conf cleared (lib default)\r\n");
            return;
        }
        snprintf(g_wifi_conf, sizeof(g_wifi_conf), "%s", p);
        printf("[wifi] conf = %s\r\n", g_wifi_conf);
        return;
    }
    if (strcmp(sub, "connect") == 0) {
        int r = wifi_connect(g_wifi_conf[0] ? g_wifi_conf : NULL);
        if (r == 0) {
            printf("[wifi] connect ok (daemon up / already connected)\r\n");
        } else {
            printf("[wifi] connect FAIL %d (%s)\r\n", r, wifi_errstr(r));
        }
        return;
    }
    if (strcmp(sub, "ap") == 0) {
        char *ssid = strtok(NULL, " ");
        char *psk = strtok(NULL, " ");
        int r;
        if (ssid == NULL || psk == NULL) {
            printf("usage: wifi ap <ssid> <psk>\r\n");
            return;
        }
        snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", ssid);
        snprintf(g_wifi_psk, sizeof(g_wifi_psk), "%s", psk);
        r = wifi_connect_ap(ssid, psk);
        if (r == 0) {
            printf("[wifi] ap ok (daemon up), then: wifi wait 30\r\n");
        } else {
            printf("[wifi] ap FAIL %d (%s)\r\n", r, wifi_errstr(r));
        }
        return;
    }
    if (strcmp(sub, "daemon") == 0) {
        char *p = strtok(NULL, " ");
        int r;
        if (p == NULL) {
            printf("wifi daemon = %s\r\n",
                   g_wifi_daemon[0] ? g_wifi_daemon : "(lib default /bin/wifi_wpa)");
            return;
        }
        if (strcmp(p, "reset") == 0) {
            wifi_set_daemon(NULL);
            g_wifi_daemon[0] = '\0';
            printf("[wifi] daemon reset to lib default\r\n");
            return;
        }
        r = wifi_set_daemon(p);
        if (r == 0) {
            snprintf(g_wifi_daemon, sizeof(g_wifi_daemon), "%s", p);
            printf("[wifi] daemon -> %s\r\n", p);
        } else {
            printf("[wifi] set_daemon FAIL %d (path must start with /)\r\n", r);
        }
        return;
    }
    if (strcmp(sub, "ip") == 0) {
        char ip[32];
        int r = wifi_get_ip(ip, sizeof(ip));
        if (r == 0) {
            printf("[wifi] wlan0 = %s\r\n", ip);
        } else {
            printf("[wifi] get_ip FAIL %d (%s)\r\n", r, wifi_errstr(r));
        }
        return;
    }
    if (strcmp(sub, "wait") == 0) {
        char *s = strtok(NULL, " ");
        int sec = s ? atoi(s) : 30;
        char ip[32];
        time_t t0 = time(NULL);
        int r;
        if (sec <= 0) {
            sec = 30;
        }
        r = wifi_wait_connected(sec, ip, sizeof(ip));
        if (r == 0) {
            printf("[wifi] connected wlan0 = %s (%lds)\r\n", ip,
                   (long)(time(NULL) - t0));
        } else {
            printf("[wifi] wait FAIL %d (%s)\r\n", r, wifi_errstr(r));
        }
        return;
    }
    if (strcmp(sub, "creds") == 0) {
        char *ssid = strtok(NULL, " ");
        char *psk = strtok(NULL, " ");
        if (ssid == NULL || psk == NULL) {
            printf("usage: wifi creds <ssid> <psk>\r\n");
            return;
        }
        snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", ssid);
        snprintf(g_wifi_psk, sizeof(g_wifi_psk), "%s", psk);
        printf("[wifi] creds stored (%s)\r\n", g_wifi_ssid);
        return;
    }
    if (strcmp(sub, "demo") == 0) {
        char ip[32];
        int r;
        if (g_wifi_ssid[0] == '\0') {
            printf("store first: wifi creds <ssid> <psk>\r\n");
            return;
        }
        r = wifi_connect_ap(g_wifi_ssid, g_wifi_psk);
        printf("[wifi] connect_ap(%s) = %d\r\n", g_wifi_ssid, r);
        r = wifi_wait_connected(30, ip, sizeof(ip));
        if (r == 0) {
            printf("[wifi] demo OK wlan0 = %s\r\n", ip);
        } else {
            printf("[wifi] demo FAIL %d (%s)\r\n", r, wifi_errstr(r));
        }
        return;
    }
    printf("unknown wifi sub: %s\r\n", sub);
}

static void cmd_relay(void)
{
    char *sub = strtok(NULL, " ");
    if (sub == NULL) {
        printf("relay = %s:%d (mirror; lib default 192.168.1.1:8443)\r\n",
               g_relay_ip, g_relay_port);
        printf("relay set <ip> <port> | reset | ask [msg]\r\n");
        return;
    }
    if (strcmp(sub, "set") == 0) {
        char *ip = strtok(NULL, " ");
        char *port = strtok(NULL, " ");
        int p;
        int r;
        if (ip == NULL || port == NULL) {
            printf("usage: relay set <ip> <port>\r\n");
            return;
        }
        p = atoi(port);
        r = llm_set_server(ip, p);
        if (r == 0) {
            snprintf(g_relay_ip, sizeof(g_relay_ip), "%s", ip);
            g_relay_port = p;
            printf("[relay] llm_set_server(%s, %d) ok\r\n", ip, p);
        } else {
            printf("[relay] llm_set_server FAIL %d\r\n", r);
        }
        return;
    }
    if (strcmp(sub, "reset") == 0) {
        int r = llm_set_server("192.168.1.1", 8443);
        snprintf(g_relay_ip, sizeof(g_relay_ip), "192.168.1.1");
        g_relay_port = 8443;
        printf("[relay] reset = %d (192.168.1.1:8443)\r\n", r);
        return;
    }
    if (strcmp(sub, "ask") == 0) {
        char *rest = strtok(NULL, "");
        time_t t0;
        int n;
        if (rest == NULL) {
            console_readline("msg> ", g_msg, MSG_MAX);
        } else {
            snprintf(g_msg, MSG_MAX, "%s", rest);
        }
        if (g_msg[0] == '\0') {
            printf("[relay] empty msg\r\n");
            return;
        }
        printf("[relay] >> %s\r\n", g_msg);
        t0 = time(NULL);
        n = llm_ask(g_msg, g_reply, RPY_MAX);
        printf("[relay] elapsed %lds\r\n", (long)(time(NULL) - t0));
        print_reply(n, "relay");
        return;
    }
    printf("unknown relay sub: %s\r\n", sub);
}

static void cmd_gw(void)
{
    char *sub = strtok(NULL, " ");
    if (sub == NULL || strcmp(sub, "show") == 0) {
        print_show();
        return;
    }
    if (strcmp(sub, "set") == 0) {
        char *field = strtok(NULL, " ");
        char *val = strtok(NULL, "");
        if (field == NULL || val == NULL) {
            printf("usage: gw set ip|vhost|port|key|model <value>\r\n");
            return;
        }
        if (strcmp(field, "ip") == 0) {
            snprintf(g_gw_ip, sizeof(g_gw_ip), "%s", val);
        } else if (strcmp(field, "vhost") == 0) {
            snprintf(g_gw_vhost, sizeof(g_gw_vhost), "%s", val);
        } else if (strcmp(field, "port") == 0) {
            int p = atoi(val);
            if (p <= 0 || p > 65535) {
                printf("bad port\r\n");
                return;
            }
            g_gw_port = p;
        } else if (strcmp(field, "key") == 0) {
            snprintf(g_gw_key, sizeof(g_gw_key), "%s", val);
        } else if (strcmp(field, "model") == 0) {
            snprintf(g_gw_model, sizeof(g_gw_model), "%s", val);
        } else {
            printf("unknown field: %s\r\n", field);
            return;
        }
        printf("[gw] %s set\r\n", field);
        return;
    }
    if (strcmp(sub, "chat") == 0) {
        llm_gateway_t gw;
        char *rest = strtok(NULL, "");
        time_t t0;
        int n;
        if (g_gw_key[0] == '\0') {
            printf("[gw] key empty, first: gw set key sk-xxx\r\n");
            return;
        }
        if (rest == NULL) {
            console_readline("msg> ", g_msg, MSG_MAX);
        } else {
            snprintf(g_msg, MSG_MAX, "%s", rest);
        }
        if (g_msg[0] == '\0') {
            printf("[gw] empty msg\r\n");
            return;
        }
        gw.ip = g_gw_ip;
        gw.vhost = g_gw_vhost;
        gw.port = g_gw_port;
        gw.api_key = g_gw_key;
        gw.model = g_gw_model;
        printf("[gw] %s:%d %s model=%s\r\n", gw.ip, gw.port, gw.vhost, gw.model);
        printf("[gw] >> %s\r\n", g_msg);
        t0 = time(NULL);
        n = llm_chat_stream(&gw, g_msg, stream_echo, NULL, g_reply, RPY_MAX);
        print_stream_done(n, "gw", (long)(time(NULL) - t0));
        return;
    }
    if (strcmp(sub, "direct") == 0) {
        char *ip = strtok(NULL, " ");
        char *vhost = strtok(NULL, " ");
        char *port = strtok(NULL, " ");
        char *key = strtok(NULL, " ");
        char *model = strtok(NULL, " ");
        char *rest;
        llm_gateway_t gw;
        time_t t0;
        int n;
        if (ip == NULL || vhost == NULL || port == NULL || key == NULL || model == NULL) {
            printf("usage: gw direct <ip> <vhost> <port> <key> <model|-> [msg]\r\n");
            return;
        }
        rest = strtok(NULL, "");
        if (rest == NULL) {
            console_readline("msg> ", g_msg, MSG_MAX);
        } else {
            snprintf(g_msg, MSG_MAX, "%s", rest);
        }
        gw.ip = ip;
        gw.vhost = vhost;
        gw.port = atoi(port);
        gw.api_key = key;
        gw.model = (strcmp(model, "-") == 0) ? NULL : model;
        printf("[direct] %s:%d %s model=%s\r\n", gw.ip, gw.port, gw.vhost,
               gw.model ? gw.model : "(lib default)");
        printf("[direct] >> %s\r\n", g_msg);
        t0 = time(NULL);
        n = llm_chat_stream(&gw, g_msg, stream_echo, NULL, g_reply, RPY_MAX);
        print_stream_done(n, "direct", (long)(time(NULL) - t0));
        return;
    }
    printf("unknown gw sub: %s\r\n", sub);
}

int main(int argc, char *argv[])
{
    char line[MSG_MAX];
    char *v[16];
    setvbuf(stdout, NULL, _IONBF, 0);
    console_raw_on();
    snprintf(g_gw_ip, sizeof(g_gw_ip), "47.102.215.139");
    snprintf(g_gw_vhost, sizeof(g_gw_vhost), "api.siliconflow.cn");
    g_gw_key[0] = '\0';
    snprintf(g_gw_model, sizeof(g_gw_model), "deepseek-ai/DeepSeek-V4-Flash");
    snprintf(g_relay_ip, sizeof(g_relay_ip), "192.168.1.1");
    g_relay_port = 8443;
    for (int i = 1; i <= 4 && i < argc; i++) {
        if (argv[i] == NULL || strcmp(argv[i], "-") == 0) {
            continue;
        }
        if (i == 1) {
            snprintf(g_gw_ip, sizeof(g_gw_ip), "%s", argv[1]);
        } else if (i == 2) {
            snprintf(g_gw_vhost, sizeof(g_gw_vhost), "%s", argv[2]);
        } else if (i == 3) {
            snprintf(g_gw_key, sizeof(g_gw_key), "%s", argv[3]);
        } else {
            snprintf(g_gw_model, sizeof(g_gw_model), "%s", argv[4]);
        }
    }
    printf("\r\n=== %s (%s %s) ===\r\n", VER_STR, __DATE__, __TIME__);
    printf("llm_sdk serial test console, type 'help'\r\n");
    print_show();
    for (;;) {
        int len = console_readline("llm> ", line, sizeof(line));
        int nv;
        if (len == 0) {
            continue;
        }
        nv = split(line, v, 16);
        if (nv == 0) {
            continue;
        }
        if (strcmp(v[0], "help") == 0 || strcmp(v[0], "?") == 0) {
            print_help();
        } else if (strcmp(v[0], "ver") == 0) {
            printf("%s (%s %s)\r\n", VER_STR, __DATE__, __TIME__);
        } else if (strcmp(v[0], "errs") == 0) {
            print_errs();
        } else if (strcmp(v[0], "show") == 0) {
            print_show();
        } else if (strcmp(v[0], "wifi") == 0) {
            cmd_wifi();
        } else if (strcmp(v[0], "uart") == 0) {
            cmd_uart();
        } else if (strcmp(v[0], "relay") == 0) {
            cmd_relay();
        } else if (strcmp(v[0], "gw") == 0) {
            cmd_gw();
        } else if (strcmp(v[0], "quit") == 0 || strcmp(v[0], "exit") == 0) {
            console_line_restore();
            printf("bye\r\n");
            break;
        } else {
            printf("unknown cmd: %s (try 'help')\r\n", v[0]);
        }
    }
    if (g_uart_fd >= 0) {
        close(g_uart_fd);
    }
    console_line_restore();
    return 0;
}

