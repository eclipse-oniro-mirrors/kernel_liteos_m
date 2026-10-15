/*
 * app_console.c - 串口控制台与 shell 小工具实现
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "app_console.h"

int ConsoleReadline(const char *prompt, char *buf, int size)
{
    int len = 0;

    printf("%s", prompt);
    fflush(stdout);
    buf[0] = '\0';
    for (;;) {
        ssize_t n = read(0, buf, size - 1);

        if (n > 0) {
            buf[n] = '\0';
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        /* n == 0 或持续异常: 稍歇再试, 防忙转 */
        usleep(100000);
    }
    len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' '  || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
    return len;
}

int ShellSplit(char *line, char **argv, int maxv)
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
        argv[n++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return n;
}

int ShellParseInt(const char *s, int32_t *val)
{
    char *end = NULL;
    long x;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    x = strtol(s, &end, 10);
    if (end == NULL || *end != '\0') {
        return -1;
    }
    *val = (int32_t)x;
    return 0;
}
