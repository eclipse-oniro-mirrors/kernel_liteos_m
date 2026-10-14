/*
 * key_service.c - 板上按键服务实现
 * key1(GPIO0_2) 按下 -> 随机抽动作阻塞执行(独立线程); key2(GPIO0_1) -> 复位。
 * 按键事件来自 key_sdk 扫描线程; 动作执行线程轮询请求, 互不阻塞。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "key_service.h"
#include "key_driver.h"     /* key_sdk: KeyDriverInit/GetState/WatchAll... */
#include "key_port.h"       /* key_sdk: KeyDelayMs(毫秒睡眠垫片) */
#include "servo_service.h"  /* ServoEnsureInit(PCA 懒初始化+接线提示) */
#include "otto_pca.h"       /* OttoPcaRun(阻塞执行)/OttoPcaStop(非阻塞复位) */

/* ============【动作池配置区】============
 * key1 随机动作池(22 个): 全部是执行完自动归位的常规动作;
 * 有意排除: sit/showcase/handsup(不归位,易停在怪姿势)、magic/calisthenics
 * (28s/32s 超长动作串,不适合随机触发)、home(留给 key2 复位用)。
 * 想增删动作: 改这张表(名字须是 otto_pca 动作表里的), 数量不用改宏。 */
static const struct KeyAct {
    const char *name;    /* 动作名(OttoPcaRun 识别) */
    const char *desc;    /* 中文说明(串口打印用) */
} g_actPool[] = {
    { "walk",      "前走" },
    { "turn",      "转弯" },
    { "jump",      "蹲站" },
    { "bend",      "侧弯" },
    { "shakeleg",  "抬腿摇" },
    { "updown",    "一起一落" },
    { "swing",     "左右摇摆" },
    { "tiptoe",    "踮脚摆" },
    { "jitter",    "抖动" },
    { "ascend",    "螺旋升天" },
    { "crusaito",  "十字步" },
    { "flapping",  "拍翅" },
    { "moonwalk",  "太空步" },
    { "whirlwind", "旋风腿" },
    { "handsup",   "举手" },
    { "handsdown", "放手" },
    { "handwave",  "挥手" },
    { "windmill",  "大风车" },
    { "takeoff",   "起飞扑扇" },
    { "fitness",   "健身" },
    { "greeting",  "打招呼" },
    { "shy",       "害羞" },
};
#define KEY_ACT_N (sizeof(g_actPool) / sizeof(g_actPool[0]))

/* ============【按键映射配置区】============ */
#define KEY_CFG_POLL_MS  20     /* 动作执行线程轮询请求周期 */
static const KeyDesc g_keys[] = {
    { "KEY1_ADD", 2, 1 },   /* key1: 板上第 1 个键 GPIO0_2(实测按下为低) */
    { "KEY2_SUB", 1, 1 },   /* key2: 板上第 2 个键 GPIO0_1 */
};
#define KEY_NUM  (sizeof(g_keys) / sizeof(g_keys[0]))
#define KEY_IDX1 0              /* g_keys 下标: 随机动作键 */
#define KEY_IDX2 1              /* g_keys 下标: 复位键 */

/* ---- 状态(回调写/执行线程写/指令读, 标志量 volatile 即可) ---- */
static volatile int g_running;      /* 服务已启动 */
static volatile int g_busy;         /* 动作执行线程正在跑动作 */
static volatile int g_reqRandom;    /* key1 请求(执行线程取走后清 0) */
static volatile int g_addCnt;       /* key1 按下次数 */
static volatile int g_subCnt;       /* key2 按下次数 */
static const char *volatile g_last; /* 最近动作名(未执行过为 NULL) */
static volatile int g_lastRet;      /* 最近动作返回值(0 完成 -1 被中止) */

/* 按键事件回调(按键扫描线程上下文, 只做轻活: 置标志/非阻塞复位) */
static void KeySvcCallback(const KeyDesc *key, KeyEvent event)
{
    if (event != KEY_EVENT_PRESS) {
        return;
    }
    if (key == &g_keys[KEY_IDX1]) {
        g_addCnt++;
        if (g_busy != 0) {
            printf("[key] key1 忽略(动作执行中)\r\n");
            return;
        }
        g_reqRandom = 1;
    } else if (key == &g_keys[KEY_IDX2]) {
        g_subCnt++;
        printf("[key] key2: 动作复位\r\n");
        OttoPcaStop();      /* 只置停止标志: 执行中=中止+归位, 空闲=仅归位 */
    }
}

/* 动作执行线程: 等 key1 请求 -> PCA 懒初始化 -> 随机抽动作 -> 阻塞执行 */
static void *KeyActLoop(void *arg)
{
    unsigned idx;
    int32_t ret;

    (void)arg;
    for (;;) {
        if (g_reqRandom == 0) {
            KeyDelayMs(KEY_CFG_POLL_MS);
            continue;
        }
        g_reqRandom = 0;
        if (ServoEnsureInit() != 0) {
            printf("[key] PCA 未就绪(初始化失败, 先 scan 排查接线), 动作取消\r\n");
            continue;
        }
        idx = (unsigned)(rand() % (int)KEY_ACT_N);
        g_busy = 1;
        printf("[key] key1: 随机动作 -> %s (%s)\r\n",
               g_actPool[idx].name, g_actPool[idx].desc);
        ret = OttoPcaRun(g_actPool[idx].name, NULL, 0);
        g_last = g_actPool[idx].name;
        g_lastRet = ret;
        g_busy = 0;
        if (ret == 0) {
            printf("[key] %s 执行完毕(已归位)\r\n", g_last);
        } else if (ret == -1) {
            printf("[key] %s 被复位中止(已归位)\r\n", g_last);
        } else {
            printf("[key] %s 执行异常 ret=%d\r\n", g_last, (int)ret);
        }
    }
    return NULL;
}

int KeySvcStart(void)
{
    pthread_attr_t attr;
    pthread_t tid;

    if (g_running != 0) {
        return 0;
    }
    srand((unsigned)(time(NULL) ^ (unsigned)getpid()));
    if (KeyDriverInit(g_keys, KEY_NUM, KeySvcCallback) != HDF_SUCCESS) {
        printf("[key] 按键驱动启动失败(GPIO 被占/HDF 异常?)\r\n");
        return -1;
    }
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, KeyActLoop, NULL) != 0) {
        (void)pthread_attr_destroy(&attr);
        printf("[key] 动作执行线程创建失败\r\n");
        KeyDriverDeinit();
        return -1;
    }
    (void)pthread_attr_destroy(&attr);
    g_running = 1;
    printf("[key] 按键服务就绪: key1=随机动作(%d 个池) key2=动作复位\r\n",
           (int)KEY_ACT_N);
    return 0;
}

int KeyCmdStatus(int argc, char **argv)
{
    uint32_t i;

    (void)argc;
    (void)argv;
    printf("[key] 驱动%s, 动作%s, key1 x%d, key2 x%d, 最近: %s(ret=%d)\r\n",
           KeyDriverIsRunning() ? "运行中" : "停止",
           g_busy ? "执行中" : "空闲",
           (int)g_addCnt, (int)g_subCnt,
           (g_last != NULL) ? g_last : "(无)",
           (g_last != NULL) ? (int)g_lastRet : 0);
    for (i = 0; i < KeyDriverGetKeyNum(); i++) {
        uint8_t pressed = 0;

        if (KeyDriverGetState(i, &pressed) == HDF_SUCCESS) {
            printf("[key] %s(gpio %u) %s\r\n", g_keys[i].name, g_keys[i].gpio,
                   (pressed != 0) ? "PRESSED" : "released");
        }
    }
    printf("[key] 动作池(%d):", (int)KEY_ACT_N);
    for (i = 0; i < KEY_ACT_N; i++) {
        printf(" %s", g_actPool[i].name);
    }
    printf("\r\n");
    return 0;
}
