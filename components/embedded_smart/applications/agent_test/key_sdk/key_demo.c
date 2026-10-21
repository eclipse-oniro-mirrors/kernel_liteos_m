/*
 * key_demo.c - 按键使用层示例(用户态版):两键加减计数,一次性命令行程序
 *
 * 移植自内核版 mytest/key/key_demo.c:按键表/事件回调/计数逻辑与内核版
 * 完全一致;差异是外壳——内核版开机自启(LOS_MODULE_INIT)+ shell 命令 key1,
 * 用户态没有这两样,改为一次性命令行程序:
 *  - 不读串口输入,参数全由 argv 传(绕开 LiteOS shell 抢输入的坑,
 *    经验来自 pca_demo 交付)
 *  - 动作类命令阻塞执行完再退出
 *  - 计数状态只存在于本进程(跨进程不共享,每次 exec 都是新进程)
 *
 * ================= 如何设计自己的按键功能(3 步模板) ========================
 *  1.定义按键表:static const KeyDesc 表[] = {{名字, GPIO全局号, 按下是否低电平},...}
 *     引脚号不确定时,板上敲 exec /bin/key_demo watch 30 再按键看跳变
 *  2.写事件回调:void XxxKeyCallback(const KeyDesc *key, KeyEvent event)
 *     - 在驱动扫描线程上下文执行,可 printf/短延时,不要长阻塞
 *     - 用 key == &表[i] 区分是哪个按键;只处理 KEY_EVENT_PRESS(按下)即可
 *     - 回调里写你的业务:计数/控舵机/切显示...
 *  3.启动:KeyDriverInit(表, 个数, 回调) —— 本文件包成 KeyDemoStart()
 *     供常驻业务进程调用;详细说明见 SDK库包/README.md
 *
 * ================= 当前示例行为 ==============================================
 *  - KEY_ADD = GPIO0_2(全局 2,板上第一个按键):按下 count+1
 *  - KEY_SUB = GPIO0_1(全局 1,板上第二个按键):按下 count-1
 *  - 每次按下串口0(115200)打印:[key] KEY_ADD, count = 3
 *
 * ================= 命令行用法 ==============================================
 *   exec /bin/key_demo run [秒]     计数演示(默认 10s,期间按下即打印,结束报总数)
 *   exec /bin/key_demo watch [秒]   监视全部 96 个 GPIO 电平跳变(默认 20s,上限 60)
 *   exec /bin/key_demo state        一次性读两键当前按下/松开状态
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "key_demo.h"
#include "key_driver.h"
#include "key_port.h"      /* KeyDelayMs */

/* ===== 第 1 步:按键表(换引脚/换按键只改这里) =====
 * 实测定位(2026-09-20,watch):板上两个按键在 GPIO0_2 和 GPIO0_1,
 * 按下为低电平;vendor input_config.hcs 的 volUp/volDown(31/32)不是本板按键 */
#define KEY_ADD_GPIO  2    /* 加键:GPIO0_2(全局 2),实测按下 1->0->1 */
#define KEY_SUB_GPIO  1    /* 减键:GPIO0_1(全局 1),实测按下 1->0->1 */

static const KeyDesc g_demoKeys[] = {
    { "KEY_ADD", KEY_ADD_GPIO, 1 },   /* 1=按下为低电平(板上按键常态) */
    { "KEY_SUB", KEY_SUB_GPIO, 1 },
};

#define DEMO_KEY_NUM (sizeof(g_demoKeys) / sizeof(g_demoKeys[0]))

static volatile int32_t g_count = 0;    /* 计数值(加减结果,串口打印) */

/* ===== 第 2 步:事件回调(把"按键事件"翻译成业务) =====
 * 扫描线程上下文调用;用表项地址区分按键,增删按键/换引脚这里同步改 */
static void KeyDemoCallback(const KeyDesc *key, KeyEvent event)
{
    if (event != KEY_EVENT_PRESS) {
        return;
    }
    if (key == &g_demoKeys[0]) {
        g_count++;
    } else if (key == &g_demoKeys[1]) {
        g_count--;
    } else {
        return;
    }
    PRINTK("[key] %s, count = %d\n", key->name, (int32_t)g_count);
}

/* ===== 第 3 步:启动/停止(包装成函数,常驻业务一行调用) ===== */
int32_t KeyDemoStart(void)
{
    return KeyDriverInit(g_demoKeys, DEMO_KEY_NUM, KeyDemoCallback);
}

void KeyDemoStop(void)
{
    KeyDriverDeinit();
}

int32_t KeyDemoGetCount(void)
{
    return (int32_t)g_count;
}

void KeyDemoResetCount(void)
{
    g_count = 0;
}

static void KeyDemoUsage(void)
{
    printf("usage: key_demo <cmd> [args...]\n");
    printf("  run [seconds]    counting demo, print on press (default 10s)\n");
    printf("  watch [seconds]  watch all 96 gpio transitions (default 20s)\n");
    printf("  state            one-shot pressed/released of both keys\n");
}

/* 解析秒数(1~60,超 60 截到 60),失败返回 -1 */
static int KeyDemoParseSec(const char *s, uint32_t *sec)
{
    char *end = NULL;
    long v;

    if ((s == NULL) || (*s == '\0')) {
        return -1;
    }
    v = strtol(s, &end, 10);
    if ((end == NULL) || (*end != '\0') || (v <= 0)) {
        return -1;
    }
    *sec = (uint32_t)((v > 60) ? 60 : v);
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t seconds;

    if (argc < 2) {
        KeyDemoUsage();
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        seconds = 10;
        if ((argc >= 3) && (KeyDemoParseSec(argv[2], &seconds) != 0)) {
            printf("key_demo: bad seconds '%s'\n", argv[2]);
            return 1;
        }
        if (KeyDemoStart() != HDF_SUCCESS) {
            printf("key_demo: driver start fail (hdf down? gpio bad?)\n");
            return 1;
        }
        printf("key_demo: counting %us, press KEY_ADD(gpio %u)/KEY_SUB(gpio %u)...\n",
               seconds, KEY_ADD_GPIO, KEY_SUB_GPIO);
        KeyDelayMs((int32_t)(seconds * 1000));
        printf("key_demo: final count = %d\n", KeyDemoGetCount());
        KeyDemoStop();
        return 0;
    }

    if (strcmp(argv[1], "watch") == 0) {
        seconds = 20;
        if ((argc >= 3) && (KeyDemoParseSec(argv[2], &seconds) != 0)) {
            printf("key_demo: bad seconds '%s'\n", argv[2]);
            return 1;
        }
        printf("key_demo: watch %us, press any key to see transitions\n", seconds);
        KeyDriverWatchAll(seconds);
        return 0;
    }

    if (strcmp(argv[1], "state") == 0) {
        uint32_t i;

        if (KeyDemoStart() != HDF_SUCCESS) {
            printf("key_demo: driver start fail\n");
            return 1;
        }
        KeyDelayMs(50);    /* 等扫描线程完成首轮电平基准 */
        printf("key_demo: count = %d\n", KeyDemoGetCount());
        for (i = 0; i < KeyDriverGetKeyNum(); i++) {
            uint8_t pressed = 0;

            if (KeyDriverGetState(i, &pressed) == HDF_SUCCESS) {
                printf("key_demo: %s(gpio %u) %s\n", g_demoKeys[i].name,
                       g_demoKeys[i].gpio, (pressed != 0) ? "PRESSED" : "released");
            }
        }
        KeyDemoStop();
        return 0;
    }

    printf("key_demo: unknown cmd '%s'\n", argv[1]);
    KeyDemoUsage();
    return 1;
}
