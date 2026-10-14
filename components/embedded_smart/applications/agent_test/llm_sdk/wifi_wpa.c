/* wifi_wpa.c — wpa_supplicant 宿主 (官方 wpa_sample 改)
 * 用法: wifi_wpa -i wlan0 -c /userdata/wpa_supplicant.conf
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "libwpa.h"

pthread_t g_wpaThread;

char *g_wpaArg[20] = {0};
int g_wpaArgc = 0;

static void *ThreadMain()
{
    printf("[WpaSample] init enter.\r\n");
    wpa_main(g_wpaArgc, g_wpaArg);
    printf("[WpaSample] wpa_main exited.\r\n");
    return NULL;
}

int main(int argc, char *argv[])
{
    g_wpaArgc = argc;
    for (int i = 0; i < g_wpaArgc; i++) {
        g_wpaArg[i] = argv[i];
    }
    int ret = pthread_create(&g_wpaThread, NULL, ThreadMain, NULL);
    if (ret != 0) {
        printf("[WpaSample] create thread failed: %s.\r\n", strerror(ret));
        return 1;
    }
    pthread_join(g_wpaThread, NULL);
    return 0;
}
