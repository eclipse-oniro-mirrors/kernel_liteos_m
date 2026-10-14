/* wifi_client.c — WiFi 连接客户端实现
 * 守护: fork + exec /bin/wifi_wpa -q -i wlan0 -c <conf> (关联热点+自动重连)
 * 查询: socket + ioctl(SIOCGIFADDR) 读 wlan0 IP
 * 单实例: 发新守护前必杀上一只(SIGKILL+收尸)。实测踩坑: 长时间离线时
 * 监控线程每轮重发守护而不杀旧的, 几十只 supplicant 同时扫同一网卡,
 * CPU 耗尽(整机 UI 卡死)且热点恢复后互相抢网卡永远连不上。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "wifi_client.h"

#define WIFI_IFNAME       "wlan0"
#define WIFI_DAEMON_BIN   "/bin/wifi_wpa"
#define WIFI_DEFAULT_CONF "/storage/wpa_supplicant.conf"
#define WIFI_POLL_MS      1000

static char g_daemon_bin[64] = WIFI_DAEMON_BIN;
static pid_t g_wpaPid = -1;   /* 本进程最近一次 spawn 的守护 pid */

int wifi_set_daemon(const char *path)
{
    if (path == NULL) {
        snprintf(g_daemon_bin, sizeof(g_daemon_bin), "%s", WIFI_DAEMON_BIN);
        return 0;
    }
    if (path[0] != '/') {
        return -1;
    }
    snprintf(g_daemon_bin, sizeof(g_daemon_bin), "%s", path);
    return 0;
}

static int get_ip(char *out, int out_len)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return WIFI_ERR_IF;
    }
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, WIFI_IFNAME, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) != 0) {
        close(fd);
        return WIFI_ERR_NOIP;
    }
    close(fd);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    if (sin->sin_family != AF_INET || sin->sin_addr.s_addr == 0) {
        return WIFI_ERR_NOIP;
    }
    if (inet_ntop(AF_INET, &sin->sin_addr, out, (size_t)out_len) == NULL) {
        return WIFI_ERR_IF;
    }
    if (strcmp(out, "0.0.0.0") == 0) {
        return WIFI_ERR_NOIP;
    }
    return 0;
}

int wifi_get_ip(char *ip, int ip_len)
{
    if (ip == NULL || ip_len < 16) {
        return WIFI_ERR_ARGS;
    }
    return get_ip(ip, ip_len);
}

/* 杀掉上一只守护并等它退干净(至多 1s)。宿主的 SIGCHLD reaper 也会收尸,
 * 这里双保险; 已被收尸(ECHILD)同样视为退出完成 */
static void kill_prev_daemon(void)
{
    int i;

    if (g_wpaPid <= 0) {
        return;
    }
    (void)kill(g_wpaPid, SIGKILL);
    for (i = 0; i < 10; i++) {
        pid_t r = waitpid(g_wpaPid, NULL, WNOHANG);

        if (r == g_wpaPid || (r < 0 && errno == ECHILD)) {
            break;
        }
        poll(NULL, 0, 100);
    }
    g_wpaPid = -1;
}

/* fork + exec 守护进程(conf 已就位); 发新前必杀旧, 全程至多一只守护 */
static int spawn_daemon(const char *conf)
{
    pid_t pid;

    if (access(g_daemon_bin, X_OK) != 0) {
        return WIFI_ERR_DAEMON;
    }
    kill_prev_daemon();
    pid = fork();
    if (pid < 0) {
        return WIFI_ERR_FORK;
    }
    if (pid == 0) {
        execl(g_daemon_bin, "wifi_wpa", "-q", "-i", WIFI_IFNAME, "-c", conf, (char *)NULL);
        _exit(1);   /* exec 失败: 子进程直接退出, 不影响调用方 */
    }
    g_wpaPid = pid;
    return 0;
}

int wifi_connect(const char *conf_path)
{
    char tmp[INET_ADDRSTRLEN];
    if (get_ip(tmp, (int)sizeof(tmp)) == 0) {
        return 0;   /* 已连接, 幂等返回 */
    }
    const char *conf = (conf_path != NULL) ? conf_path : WIFI_DEFAULT_CONF;
    return spawn_daemon(conf);
}

int wifi_connect_ap(const char *ssid, const char *psk)
{
    char tmp[INET_ADDRSTRLEN];
    if (get_ip(tmp, (int)sizeof(tmp)) == 0) {
        return 0;   /* 已连接, 幂等返回 */
    }
    if (ssid == NULL || psk == NULL || ssid[0] == '\0' || psk[0] == '\0') {
        return WIFI_ERR_ARGS;
    }
    /* 库内部代写配置(裸 syscall: 避免 stdio FILE 改变二进制布局踩 ldso 坑) */
    const char *conf = "/userdata/.wifi_ap.conf";
    int fd = open(conf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return WIFI_ERR_IF;
    }
    char buf[600];
    int n = snprintf(buf, sizeof(buf),
                     "ctrl_interface=/userdata/wpa_ctrl\n"
                     "network={\n    ssid=\"%s\"\n    psk=\"%s\"\n}\n",
                     ssid, psk);
    if (n <= 0 || n >= (int)sizeof(buf) || write(fd, buf, (size_t)n) != n) {
        close(fd);
        return WIFI_ERR_IF;
    }
    close(fd);
    return spawn_daemon(conf);
}

int wifi_wait_connected(int timeout_sec, char *ip, int ip_len)
{
    if (ip == NULL || ip_len < 16 || timeout_sec <= 0) {
        return WIFI_ERR_ARGS;
    }
    int rounds = timeout_sec * 1000 / WIFI_POLL_MS;
    int ret = get_ip(ip, ip_len);
    for (int i = 0; i < rounds && ret == WIFI_ERR_NOIP; i++) {
        poll(NULL, 0, WIFI_POLL_MS);
        ret = get_ip(ip, ip_len);
    }
    return ret;
}
