/*
 * key_demo.h - 按键使用层示例(两键加减计数)对外接口——用户态 SDK 版
 *
 * 与内核版差异:没有 LOS_MODULE_INIT 开机自启,也没有 shell 命令 key1,
 * 计数状态只存在于本进程;板端验收用一次性命令行程序 key_demo(见下)
 * 或常驻业务进程直接调这 4 个 API。
 *
 * 本文件演示"如何用 key_driver 实现自己的按键功能":
 *  - 按键表 + 事件回调 + 一键启动/停止,全部收在 KeyDemoStart/Stop 里
 *  - 其他代码只需 KeyDemoStart()/KeyDemoStop()/KeyDemoGetCount()
 *  - 想实现别的功能(如控制舵机/切屏):仿照 key_demo.c 新建自己的
 *    xx_user.c,换一套按键表和回调即可,驱动层零改动
 */
#ifndef MYTEST_KEY_DEMO_H
#define MYTEST_KEY_DEMO_H

#include <stdint.h>
#include "hdf_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一键启动按键示例:注册按键表+回调,创建扫描任务
 * 幂等保护:已启动时返回 HDF_ERR_DEVICE_BUSY(先 Stop 再 Start)
 * 返回 HDF_SUCCESS 或失败码
 */
int32_t KeyDemoStart(void);

/* 停止示例并释放驱动(幂等;停止后可重新 Start) */
void KeyDemoStop(void);

/* 当前计数值(加键 +1 / 减键 -1 的结果) */
int32_t KeyDemoGetCount(void);

/* 计数值清零 */
void KeyDemoResetCount(void);

#ifdef __cplusplus
}
#endif
#endif /* MYTEST_KEY_DEMO_H */
