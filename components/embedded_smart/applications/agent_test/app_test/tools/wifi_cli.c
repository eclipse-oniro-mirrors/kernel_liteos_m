#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_client.h"
int main(int argc, char *argv[])
{
    const char *cmd = (argc > 1) ? argv[1] : "status";
    char ip[32];
    if (strcmp(cmd, "connect") == 0) {
        const char *conf = (argc > 2) ? argv[2] : NULL;
        int r = wifi_connect(conf);
        printf("[wifi] connect -> %d\r\n", r);
        return (r == 0) ? 0 : 1;
    }
    if (strcmp(cmd, "apx") == 0) {
        int r = wifi_connect_ap(argv[2], argv[3]);
        printf("[wifi] apx -> %d\r\n", r);
        return (r == 0) ? 0 : 1;
    }
    if (strcmp(cmd, "status") == 0) {
        int r = wifi_get_ip(ip, sizeof(ip));
        printf("[wifi] status -> %d\r\n", r);
        return r;
    }
    if (strcmp(cmd, "wait") == 0) {
        int secs = (argc > 2) ? atoi(argv[2]) : 20;
        int r = wifi_wait_connected(secs, ip, sizeof(ip));
        printf("[wifi] wait -> %d\r\n", r);
        return r;
    }
    return 1;
}
