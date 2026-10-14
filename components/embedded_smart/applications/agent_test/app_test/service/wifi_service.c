/*
 * wifi_service.c - WiFi 连接/状态/后台监控服务实现
 */
/* musl 的 struct ifreq/IFF_* 藏在 _GNU_SOURCE/_BSD_SOURCE 门后,
 * 本构建只有 -D_XOPEN_SOURCE=700, 必须先补宏(NicBounce 需要) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "wifi_service.h"
#include "app_console.h"
#include "wifi_client.h"

static char g_ip[32];
static volatile int  g_was_up = 0;
static volatile long g_up_sec = 0;
static volatile int  g_mon_skip = 0;
static long   g_off_sec = 0;                /* 离线持续秒数(重试计时) */
static int    g_retry_cnt = 0;              /* 重试次数(打印用) */
static long g_boot = 0;                     /* 程序启动时刻(报警/心跳时间戳) */
static WifiRecoverHook g_hook = NULL;

/* 守护(wifi_wpa)是 fork 出来的子进程: 反复重发连接会积累僵尸,
 * SIGCHLD 里回收(wifi_connect_ap 离线时每次都会 fork 新守护) */
static void ReaperSig(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

static void alarm_print(const char *what)
{
    /* 注意: time_t 是 64 位, 必须强转 long 再进 printf,
     * 否则 64 位参数占两个 varargs 寄存器, 把后面的 %s 错位读
     * (实测: 秒数=0 打出 (null), 秒数=11 把 0xb 当指针直接 data abort) */
    printf("\r\n[wifi] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
    printf("[wifi] !!! 报警 [%lds]: %s\r\n", (long)(time(NULL) - g_boot), what);
    printf("[wifi] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n\r\n");
}

/* 本地等 IP 轮询: lib 的 wifi_wait_connected 遇 NOIP 之外的错误码会立即
 * 返回(实测开机早期 socket 偶发失败 -> 0 秒假超时), 这里无视错误码,
 * 一律轮询到拿到 IP 或超时 */
int WifiSvcWaitIp(int sec)
{
    int left = sec;
    int r = wifi_get_ip(g_ip, sizeof(g_ip));

    while (r != 0 && left-- > 0) {
        sleep(1);
        r = wifi_get_ip(g_ip, sizeof(g_ip));
    }
    if (r == 0) {
        g_was_up = 1;
        g_up_sec = 0;
    }
    return r;
}

const char *WifiSvcErrstr(int e)
{
    switch (e) {
    case WIFI_ERR_ARGS:
        return "参数非法(检查 SSID/PSK)";
    case WIFI_ERR_FORK:
        return "fork 失败";
    case WIFI_ERR_DAEMON:
        return "守护进程缺失(/bin/wifi_wpa 不存在)";
    case WIFI_ERR_NOIP:
        return "未拿到 IP/等待超时(热点在线吗?)";
    case WIFI_ERR_IF:
        return "网卡查询失败";
    default:
        return "未知错误";
    }
}

int WifiSvcGetIp(char *ip, int len)
{
    return wifi_get_ip(ip, len);
}

int WifiSvcConnect(const char *ssid, const char *psk)
{
    return wifi_connect_ap(ssid, psk);
}

int WifiSvcStart(void)
{
    int r;
    int round;
    int ok = 0;

    if (g_boot == 0) {
        g_boot = time(NULL);
    }
    (void)signal(SIGCHLD, ReaperSig);
    printf("\r\n[wifi] ===== 1/3 连接 =====\r\n");
    r = WifiSvcGetIp(g_ip, sizeof(g_ip));
    if (r == 0) {
        printf("[wifi] 已有 IP(守护自动重连过): wlan0 = %s, 跳过连接\r\n", g_ip);
        g_was_up = 1;
        g_up_sec = 0;
        return 0;
    }
    if (WIFI_CFG_SSID[0] == '\0' || WIFI_CFG_PSK[0] == '\0') {
        printf("[wifi] 配置区 SSID/PSK 为空, 跳过连接(控制台 apx 手动连)\r\n");
        return -1;
    }
    /* 分轮重试: 热点慢上电/守护早夭都有机会在启动段内恢复 */
    for (round = 1; round <= WIFI_CFG_BOOT_ROUNDS && !ok; round++) {
        if (round > 1) {
            printf("[wifi] 启动重试 %d/%d ...\r\n", round, WIFI_CFG_BOOT_ROUNDS);
        }
        r = WifiSvcConnect(WIFI_CFG_SSID, WIFI_CFG_PSK);
        if (r != 0) {
            printf("[wifi] connect_ap FAIL %d (%s)\r\n", r, WifiSvcErrstr(r));
            continue;
        }
        printf("[wifi] ok, 守护已启动, 自动关联+DHCP(轮 %d, 等 %ds)...\r\n",
               round, WIFI_CFG_WAIT_SEC / WIFI_CFG_BOOT_ROUNDS);
        if (WifiSvcWaitIp(WIFI_CFG_WAIT_SEC / WIFI_CFG_BOOT_ROUNDS) == 0) {
            ok = 1;
        }
    }
    if (!ok) {
        alarm_print("WiFi 等待超时, 连接失败");
        printf("[wifi] 后台每 %ds 自动重试, 连上自动恢复, 无需操作\r\n",
               WIFI_CFG_RETRY_SEC);
        return -1;
    }
    g_off_sec = 0;
    g_retry_cnt = 0;
    printf("[wifi] 已连接: wlan0 = %s\r\n", g_ip);
    return 0;
}

/* 硬复位网卡(down/up, 触发驱动 stop/start): 长时间离线后驱动/守护状态
 * 可能卡死, 只重发守护救不回来(实测: 断网半天后热点恢复也连不上)。
 * 守护的套接字随 down 失效自行退出, 随后的重发守护是全新状态 */
static void NicBounce(void)
{
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        return;
    }
    (void)memset(&ifr, 0, sizeof(ifr));
    (void)strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags = (short)(ifr.ifr_flags & ~IFF_UP);
        (void)ioctl(fd, SIOCSIFFLAGS, &ifr);   /* down */
        sleep(1);
        ifr.ifr_flags = (short)(ifr.ifr_flags | IFF_UP);
        (void)ioctl(fd, SIOCSIFFLAGS, &ifr);   /* up */
        printf("[wifi] wlan0 已复位(down/up), 重新发起连接\n");
    } else {
        printf("[wifi] wlan0 复位失败(ioctl %d)\n", fd);
    }
    (void)close(fd);
}

/* WiFi 监控一步(后台线程每约 1 秒调一次)
 * g_mon_skip: 阻塞操作(问答/动作/等待)期间置位 */
static void monitor_tick(void)
{
    int r;

    if (g_mon_skip) {
        return;
    }
    r = wifi_get_ip(g_ip, sizeof(g_ip));
    if (r == 0) {
        if (!g_was_up) {
            printf("\r\n[wifi] 连接恢复 [%lds]: wlan0 = %s (重试 %d 次后成功)\r\n",
                   (long)(time(NULL) - g_boot), g_ip, g_retry_cnt);
            g_was_up = 1;
            g_up_sec = 0;
            g_off_sec = 0;
            g_retry_cnt = 0;
            if (g_hook != NULL) {
                g_hook();
            }
        } else {
            g_up_sec++;
            if ((g_up_sec % 30) == 0) {
                printf("\r\n[wifi] 在线 %lds (ip=%s)\r\n", g_up_sec, g_ip);
            }
        }
    } else if (g_was_up) {
        char why[96];
        snprintf(why, sizeof(why), "WiFi 掉线 (%s), 守护自动重连中...",
                 WifiSvcErrstr(r));
        alarm_print(why);
        g_was_up = 0;
        g_up_sec = 0;
        g_off_sec = 0;
        g_retry_cnt = 0;
    } else {
        /* 从未在线(启动段失败/守护死亡): 后台定期重试, 连上自动恢复 */
        g_off_sec++;
        if (WIFI_CFG_SSID[0] == '\0' || WIFI_CFG_PSK[0] == '\0') {
            return;
        }
        if ((g_off_sec % WIFI_CFG_RETRY_SEC) == 0) {
            g_retry_cnt++;
            if ((g_retry_cnt % WIFI_CFG_BOUNCE_EVERY) == 0) {
                NicBounce();   /* 长期连不上: 定期硬复位网卡换全新驱动状态 */
            }
            /* 每轮都重发守护: lib 会先杀旧守护(单实例), 守护死了则立即补位 */
            r = WifiSvcConnect(WIFI_CFG_SSID, WIFI_CFG_PSK);
            printf("\r\n[wifi] 自动重试第%d次(离线 %lds): %s\r\n",
                   g_retry_cnt, g_off_sec,
                   (r == 0) ? "守护已发起, 等待关联" : WifiSvcErrstr(r));
        }
    }
}

static void *monitor_thread(void *arg)
{
    (void)arg;
    for (;;) {
        sleep(1);
        monitor_tick();
    }
    return NULL;
}

void WifiSvcMonitorStart(void)
{
    pthread_t tid;

    if (pthread_create(&tid, NULL, monitor_thread, NULL) != 0) {
        printf("[wifi] !! 监控线程创建失败(无自动监控)\r\n");
        return;
    }
    pthread_detach(tid);
}

void WifiSvcMonPause(void)
{
    g_mon_skip = 1;
}

void WifiSvcMonResume(void)
{
    g_mon_skip = 0;
    g_up_sec = 0;    /* 阻塞操作耗时不算在线时长, 也避免立刻误报掉线 */
}

void WifiSvcSetRecoverHook(WifiRecoverHook hook)
{
    g_hook = hook;
}

/* ---------------- shell 指令 ---------------- */

int WifiCmdIp(int argc, char **argv)
{
    int r = WifiSvcGetIp(g_ip, sizeof(g_ip));

    (void)argc;
    (void)argv;
    if (r == 0) {
        printf("[wifi] 在线: wlan0 = %s\r\n", g_ip);
        g_was_up = 1;
    } else {
        printf("[wifi] 离线: %d (%s)\r\n", r, WifiSvcErrstr(r));
        g_was_up = 0;
    }
    return 0;
}

int WifiCmdAp(int argc, char **argv)
{
    int r;

    if (argc < 3) {
        printf("[wifi] 用法: apx <热点> <密码>\r\n");
        return 0;
    }
    r = WifiSvcConnect(argv[1], argv[2]);
    printf("[wifi] apx -> %d%s\r\n", r,
           (r == 0) ? ", 用 wait 等拿 IP" : "");
    return 0;
}

int WifiCmdWait(int argc, char **argv)
{
    int32_t sec = 20;
    int r;

    if (argc >= 2 && ShellParseInt(argv[1], &sec) != 0) {
        sec = 20;
    }
    WifiSvcMonPause();
    r = WifiSvcWaitIp((int)sec);
    WifiSvcMonResume();
    if (r == 0) {
        printf("[wifi] 已连接: wlan0 = %s\r\n", g_ip);
    } else {
        printf("[wifi] wait FAIL %d (%s)\r\n", r, WifiSvcErrstr(r));
    }
    return 0;
}
