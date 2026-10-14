#ifndef KEY_SERVICE_H
#define KEY_SERVICE_H
/*
 * key_service.h - 板上按键服务(key_sdk 驱动层 + Otto 动作)
 * 依赖: applications/key_sdk(libkeydriver.a: KeyXxx) + servo_service(PCA 懒初始化)
 *
 * 行为(本服务启动后生效, 不依赖网络):
 *   key1(板上第 1 个键, GPIO0_2): 从 22 个动作里随机抽一个执行
 *     - 串口先打印抽中的动作名, 执行完再打印执行完毕消息
 *     - 动作在独立执行线程里阻塞跑, 不占按键扫描; 执行期间再按 key1 忽略
 *   key2(板上第 2 个键, GPIO0_1): 动作复位
 *     - 动作执行中 = 中止当前动作 + 归位; 空闲 = 仅归位
 *
 * 动作池(22 个)在 key_service.c 顶部配置区, 想增删动作改那张表即可。
 *
 * 并发说明: 控制台 run/post 与按键动作走同一个 Otto SDK, 无互斥,
 * 不要同时用; key2 复位优先级最高(任何来源的动作都会被中止归位)。
 */

/* ---- 语义接口 ---- */
int KeySvcStart(void);                        /* 启动按键扫描+动作执行线程 */

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int KeyCmdStatus(int argc, char **argv);      /* key: 按键/动作执行状态 */

#endif /* KEY_SERVICE_H */
