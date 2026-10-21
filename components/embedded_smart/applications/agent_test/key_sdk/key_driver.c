/*
 * key_driver.c - 按键驱动层实现(用户态 SDK 版)
 *
 * 移植自内核版 mytest/key/key_driver.c,扫描/去抖/事件分发逻辑逐行一致,
 * 平台差异三处:
 *  1.GPIO 访问:内核版为 PL061 寄存器直读(基址 0x120D0000/组步进 0x1000),
 *    用户态改走 HDF 用户态 GPIO HDI(GpioSetDir/GpioRead,经 /dev/hdf/dev_mgr
 *    的 HDF_PLATFORM_GPIO_MANAGER 服务)。内核版直读的原因:模块挂载早于
 *    GPIO 控制器(hisi_pl061_driver)注册,开机阶段走 HDF 调用必失败,失败
 *    重试还刷 HDF_LOGE 灌爆 hilog(实测 2026-09-20);用户态程序开机后才
 *    运行,不存在该时序问题,走标准 HDI 即可。
 *  2.任务:LOS_TaskCreate(TSK_INIT_PARAM_S,4KB 栈,优先级 25) ->
 *    pthread_create(detached 属性,musl 默认栈,系统调度)
 *  3.延时:LOS_TaskDelay(1 tick=10ms) -> KeyDelayMs(10)(见 key_port.h)
 * 上层 API 与内核版同名同参(KeyDriverIsRunning 返回 bool,内核版 BOOL)。
 *
 * 复用方法(不改本文件):
 *  - 使用层构造 KeyDesc 数组 + 回调,调 KeyDriverInit 即可
 *  - 改去抖时间/扫描周期只动下方宏
 */
#include "key_driver.h"
#include "key_port.h"      /* PRINTK->printf / KeyDelayMs(用户态垫片) */
#include "gpio_if.h"       /* GpioSetDir/GpioRead(用户态 GPIO HDI) */
#include <pthread.h>
#include <stdlib.h>

/* ===== 可按需调整的配置 ===== */
#define KEY_SCAN_PERIOD_MS  10         /* 扫描间隔 ms(等价内核 1 tick = 10ms) */
#define KEY_DEBOUNCE_TIMES  2          /* 连续 2 次读到同电平才确认,约 20ms */
#define KEY_WATCH_PERIOD_MS 10         /* watch 诊断轮询间隔 ms */
#define KEY_DEINIT_WAIT_MS  200        /* Deinit 等扫描线程退出上限 */

/* ===== GPIO 全局编号规则(组号*8+组内序号,共 12 组 96 个) =====
 * 寄存器基址/步进由 HDF GPIO 控制器管理,用户态不直接碰寄存器,
 * 这里只保留编号规则给 out-of-range 检查/watch 打印用 */
#define GPIO_GROUP_NUM     12
#define GPIO_ALL_NUM       (GPIO_GROUP_NUM * 8)

/* 单个按键的扫描状态 */
typedef struct {
    uint16_t stable;   /* 已确认电平(去抖后) */
    uint8_t bounce;    /* 与确认电平不同的连续计数 */
} KeyState;

static const KeyDesc *g_keys;         /* 使用者的按键表 */
static uint32_t g_num;                /* 按键个数 */
static KeyEventCallback g_cb;         /* 事件回调 */
static KeyState *g_state;             /* 每键去抖状态 */
static volatile uint32_t g_running;   /* 扫描任务运行标志 */
static pthread_t g_tid;               /* 扫描线程句柄(记录/调试用) */
static volatile uint8_t g_tidValid;   /* 1=扫描线程在跑(Init 置 1,线程退出前清 0) */

/* 按键脚配置为输入(HDI 经 GPIO manager 到 GPIO 控制器) */
static int32_t KeyHwSetup(const KeyDesc *key)
{
    if (key->gpio >= GPIO_ALL_NUM) {
        PRINTK("key_scan: gpio %u out of range\n", key->gpio);
        return HDF_ERR_INVALID_PARAM;
    }
    if (GpioSetDir(key->gpio, GPIO_DIR_IN) != HDF_SUCCESS) {
        PRINTK("key_scan: setdir gpio %u fail\n", key->gpio);
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

/* 读按键脚电平:0=低,1=高(Setup 过即可读;读失败按低电平处理并靠去抖过滤) */
static uint16_t KeyHwLevel(const KeyDesc *key)
{
    uint16_t level = 0;

    if (GpioRead(key->gpio, &level) != HDF_SUCCESS) {
        return 0;
    }
    return level;
}

static uint8_t KeyIsPressed(const KeyDesc *key, uint16_t level)
{
    return (uint8_t)((key->activeLow != 0) ? (level == 0) : (level == 1));
}

static void KeyReportEvent(const KeyDesc *key, uint16_t newLevel)
{
    uint8_t pressed = KeyIsPressed(key, newLevel);

    g_cb(key, pressed != 0 ? KEY_EVENT_PRESS : KEY_EVENT_RELEASE);
}

/* 扫描一轮:读电平 -> 去抖 -> 状态变化时回调 */
static void KeyScanOnce(void)
{
    uint32_t i;

    for (i = 0; i < g_num; i++) {
        uint16_t level = KeyHwLevel(&g_keys[i]);

        if (level == g_state[i].stable) {
            g_state[i].bounce = 0;     /* 与确认电平一致,无抖动 */
            continue;
        }
        g_state[i].bounce++;
        if (g_state[i].bounce < KEY_DEBOUNCE_TIMES) {
            continue;
        }
        g_state[i].stable = level;     /* 去抖通过,确认新电平 */
        g_state[i].bounce = 0;
        KeyReportEvent(&g_keys[i], level);
    }
}

/* 扫描线程:配置 GPIO 后循环扫描,g_running 清零自退出 */
static void *KeyScanTask(void *arg)
{
    uint32_t i;

    (void)arg;

    for (i = 0; i < g_num; i++) {
        if (KeyHwSetup(&g_keys[i]) != HDF_SUCCESS) {
            PRINTK("key_scan: setup gpio %u fail, driver exit\n", g_keys[i].gpio);
            g_running = 0;
            g_tidValid = 0;
            return NULL;
        }
        g_state[i].stable = KeyHwLevel(&g_keys[i]);   /* 以开机初始电平为基准 */
        g_state[i].bounce = 0;
    }
    PRINTK("key_scan: %u key(s) ready\n", g_num);

    while (g_running != 0) {
        KeyScanOnce();
        KeyDelayMs(KEY_SCAN_PERIOD_MS);
    }
    g_tidValid = 0;
    return NULL;
}

int32_t KeyDriverInit(const KeyDesc *keys, uint32_t num, KeyEventCallback cb)
{
    pthread_attr_t attr;
    pthread_t tid;

    if ((keys == NULL) || (num == 0) || (cb == NULL)) {
        return HDF_ERR_INVALID_PARAM;
    }
    if ((g_running != 0) || (g_tidValid != 0)) {
        PRINTK("key_scan: already running, call Deinit first\n");
        return HDF_ERR_DEVICE_BUSY;
    }

    g_keys = keys;
    g_num = num;
    g_cb = cb;
    g_state = calloc(num, sizeof(KeyState));
    if (g_state == NULL) {
        return HDF_ERR_MALLOC_FAIL;
    }

    g_running = 1;
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, KeyScanTask, NULL) != 0) {
        PRINTK("key_scan: create thread fail\n");
        (void)pthread_attr_destroy(&attr);
        g_running = 0;
        free(g_state);
        g_state = NULL;
        return HDF_FAILURE;
    }
    (void)pthread_attr_destroy(&attr);
    g_tid = tid;
    g_tidValid = 1;
    return HDF_SUCCESS;
}

void KeyDriverDeinit(void)
{
    uint32_t wait;

    if (g_tidValid == 0) {
        return;
    }
    g_running = 0;                        /* 线程最迟一个扫描周期内退出 */
    for (wait = 0; (wait < KEY_DEINIT_WAIT_MS / KEY_SCAN_PERIOD_MS) &&
                   (g_tidValid != 0); wait++) {
        KeyDelayMs(KEY_SCAN_PERIOD_MS);
    }
    free(g_state);
    g_state = NULL;
    g_keys = NULL;
    g_num = 0;
    g_cb = NULL;
    g_tidValid = 0;
}

bool KeyDriverIsRunning(void)
{
    return (g_running != 0) ? true : false;
}

int32_t KeyDriverGetState(uint32_t index, uint8_t *pressed)
{
    if ((index >= g_num) || (pressed == NULL) || (g_state == NULL)) {
        return HDF_ERR_INVALID_PARAM;
    }
    *pressed = KeyIsPressed(&g_keys[index], g_state[index].stable);
    return HDF_SUCCESS;
}

uint32_t KeyDriverGetKeyNum(void)
{
    return g_num;
}

/*
 * 诊断:监视全部 96 个 GPIO 的电平跳变(不动任何方向/复用,只读电平)
 * 定位"实体按键到底接在哪个脚":按下按键,哪个脚打印跳变就是哪个
 * 注:用户态逐脚 GpioRead 经 dev_mgr 转发,开销比内核版寄存器直读高,
 *     10ms 一轮共 96 次调用,诊断用途足够
 */
void KeyDriverWatchAll(uint32_t seconds)
{
    uint16_t last[GPIO_ALL_NUM];
    uint32_t idx;
    uint32_t tick;

    if (seconds > 60) {
        seconds = 60;
    }
    /* 全部 96 个脚读一遍初值(只读不动方向,不影响已注册按键的扫描) */
    for (idx = 0; idx < GPIO_ALL_NUM; idx++) {
        uint16_t level = 0;

        last[idx] = (GpioRead((uint16_t)idx, &level) == HDF_SUCCESS) ? level : 0;
    }
    PRINTK("watch: %us, press keys now (GPIOg_b = global g*8+b)\n", seconds);

    for (tick = 0; tick < seconds * 100; tick++) {      /* 10ms 一轮 */
        for (idx = 0; idx < GPIO_ALL_NUM; idx++) {
            uint16_t level = 0;
            uint32_t group = idx / 8;
            uint32_t bit = idx % 8;

            if (GpioRead((uint16_t)idx, &level) != HDF_SUCCESS) {
                continue;
            }
            if (level != last[idx]) {
                PRINTK("watch: GPIO%u_%u(%u) %u->%u\n",
                       group, bit, idx, last[idx], level);
                last[idx] = level;
            }
        }
        KeyDelayMs(KEY_WATCH_PERIOD_MS);
    }
    PRINTK("watch: done\n");
}
