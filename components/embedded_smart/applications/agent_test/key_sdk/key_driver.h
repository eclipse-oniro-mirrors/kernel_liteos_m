/*
 * key_driver.h - 按键驱动层(纯 GPIO 按键,不含任何业务逻辑)——用户态 SDK 版
 *
 * 移植自内核版(device/soc/.../mytest/key):上层 API 同名同参,平台差异由
 * key_port.h 垫片承担(LOS_TaskDelay->KeyDelayMs / PRINTK->printf /
 * LOS_TaskCreate->pthread),GPIO 访问改走 HDF 用户态 GPIO HDI
 * (GpioSetDir/GpioRead,经 /dev/hdf/dev_mgr 的 HDF_PLATFORM_GPIO_MANAGER;
 * 用户态程序开机后才运行,无内核版'模块早于 GPIO 控制器注册'的时序问题)。
 *
 * 职责:把"某个 GPIO 按键被按下/松开"变成事件,回调给使用者
 *  - 支持任意多个按键(使用者传 KeyDesc 数组)
 *  - 软件去抖(连续 N 次读到同一电平才确认,默认约 20ms)
 *  - 独立扫描任务轮询 GPIO,回调在任务上下文执行(回调里可 PRINTK/延时)
 *
 * 与使用层的边界:
 *  - 本层只发 KEY_EVENT_PRESS/KEY_EVENT_RELEASE,不知道"计数/音量"等业务
 *  - 使用者(key/key_app.c 等)只写回调函数,换业务不动本层
 *
 * 硬件说明:
 *  - 板上按键接 GPIO 与 GND 之间,按下为低电平(activeLow=1)
 *  - GPIO 全局编号 = 组号*8 + 组内序号(见 sdk_liteos/hdf_config/gpio/gpio_config.hcs),
 *    例如 GPIO3_7 = 31、GPIO4_0 = 32
 *  - 按键脚默认复用为 GPIO,无需 PinSetFunc;若换到非默认 GPIO 脚,
 *    初始化前先用 PinGet/PinSetFunc 切复用(参考 bridge/uart1_bus.c)
 */
#ifndef MYTEST_KEY_DRIVER_H
#define MYTEST_KEY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "hdf_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 按键事件 */
typedef enum {
    KEY_EVENT_PRESS = 0,   /* 按下(确认去抖后触发一次) */
    KEY_EVENT_RELEASE,     /* 松开(确认去抖后触发一次) */
} KeyEvent;

/* 按键描述:使用者填好数组传给 KeyDriverInit */
typedef struct {
    const char *name;      /* 按键名(回调/打印用,自定义) */
    uint16_t gpio;         /* GPIO 全局编号(组号*8+序号) */
    uint8_t activeLow;     /* 1=按下为低电平(板上按键常用);0=按下为高电平 */
} KeyDesc;

/* 按键事件回调:扫描任务上下文调用,可安全 PRINTK/延时 */
typedef void (*KeyEventCallback)(const KeyDesc *key, KeyEvent event);

/*
 * 启动按键驱动:配置 GPIO 为输入,创建扫描任务并开始上报事件
 * @keys    按键描述数组(生命周期需覆盖整个运行期,建议 static)
 * @num     按键个数
 * @cb      事件回调,不可为 NULL
 * 返回 HDF_SUCCESS 或失败码;重复调用返回失败(先 Deinit 再 Init)
 */
int32_t KeyDriverInit(const KeyDesc *keys, uint32_t num, KeyEventCallback cb);

/* 停止扫描任务并释放资源(幂等;Deinit 后可重新 Init) */
void KeyDriverDeinit(void);

/* 驱动是否正在运行(Init 成功后为 true,Deinit 后为 false) */
bool KeyDriverIsRunning(void);

/* 查询第 index 个按键当前是否处于按下态(1=按下,0=松开) */
int32_t KeyDriverGetState(uint32_t index, uint8_t *pressed);

/* 当前注册的按键个数(未初始化时为 0) */
uint32_t KeyDriverGetKeyNum(void);

/*
 * 诊断:监视全部 12 组 96 个 GPIO 电平,任何跳变打印一行
 * "watch: GPIO<组>_<位> <旧>-><新>",持续 seconds 秒(上限 60)
 * 用途:实体的按键引脚不确定时,按下按键看哪个脚跳变
 * 在调用者任务上下文阻塞运行(期间 shell 无响应,到时自返回)
 */
void KeyDriverWatchAll(uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif /* MYTEST_KEY_DRIVER_H */
