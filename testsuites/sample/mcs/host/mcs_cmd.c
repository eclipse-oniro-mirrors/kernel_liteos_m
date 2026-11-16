/*
 * mcs_cmd.c — mcs 测试客户端小工具（2026-11-09 新架构）
 * 用法（板上）：mcs_cmd <命令...>  例：
 *   mcs_cmd run np 1 / mcs_cmd run np all / mcs_cmd run all
 *   mcs_cmd status / mcs_cmd stop
 * 连 host_server 的 AF_UNIX socket，发一行，读一行应答，退出。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define HOST_SOCK_PATH "/data/mcs_test.sock"

int main(int argc, char **argv)
{
    int fd, i;
    struct sockaddr_un addr;
    char buf[512];
    ssize_t n;
    size_t len = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: mcs_cmd <run|status|stop> [args...]\n");
        return 2;
    }
    buf[0] = '\0';
    for (i = 1; i < argc; i++) {
        int w = snprintf(buf + len, sizeof(buf) - len - 1, "%s%s",
                         i > 1 ? " " : "", argv[i]);
        if (w <= 0 || (size_t)w >= sizeof(buf) - len - 1) {
            break;
        }
        len += (size_t)w;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", HOST_SOCK_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect " HOST_SOCK_PATH);
        close(fd);
        return 1;
    }
    if (write(fd, buf, len) != (ssize_t)len) {
        perror("write");
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(fd);
    return 0;
}
