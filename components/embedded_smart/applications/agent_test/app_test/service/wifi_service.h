#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H
/*
 * wifi_service.h - WiFi 连接/状态/后台监控服务
 * 依赖: llm_sdk 的 libwificlient.a(wifi_connect_ap/wifi_get_ip)
 *
 * 后续接入方法: include 本头文件, 调 WifiSvc* 系列即可;
 * shell 新指令在 wifi_service.c 加 WifiCmdXxx, 声明放这里, 主程序命令表绑定。
 */

/* ============【WiFi 配置区】============ */
#define WIFI_CFG_SSID        "123456"    /* 热点名(空串=启动跳过连接) */
#define WIFI_CFG_PSK         "20010504" /* 热点密码 */
#define WIFI_CFG_WAIT_SEC    60         /* 启动等 IP 总超时(秒, 分 3 轮重试) */
#define WIFI_CFG_BOOT_ROUNDS 3          /* 启动阶段连接尝试轮数 */
#define WIFI_CFG_RETRY_SEC   30         /* 离线后后台重连间隔(秒) */
#define WIFI_CFG_BOUNCE_EVERY 10        /* 连续 N 次重试失败后硬复位网卡(down/up) */

typedef void (*WifiRecoverHook)(void);

/* 启动连网: 已有 IP 则跳过, 否则连接+等 IP; 返回 0=在线 */
int WifiSvcStart(void);

/* 当前 IP; 返回 0=在线并填 ip, 非 0=离线(错误码见 WifiSvcErrstr) */
int WifiSvcGetIp(char *ip, int len);

/* 阻塞等拿 IP(无条件轮询, 修复 lib 假超时); 返回 0=拿到 */
int WifiSvcWaitIp(int sec);

/* 现场重连热点(只发起, 拿 IP 用 WifiSvcWaitIp); 返回 0=守护已启动 */
int WifiSvcConnect(const char *ssid, const char *psk);

const char *WifiSvcErrstr(int e);

/* 启动后台监控线程(每秒巡检: 掉线报警/恢复打印/在线心跳30s) */
void WifiSvcMonitorStart(void);

/* 阻塞操作(问答/动作/等待)期间暂停/恢复监控 */
void WifiSvcMonPause(void);
void WifiSvcMonResume(void);

/* 注册"连接恢复"回调(如掉线恢复后自动重新问 LLM) */
void WifiSvcSetRecoverHook(WifiRecoverHook hook);

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int WifiCmdIp(int argc, char **argv);    /* ip              */
int WifiCmdAp(int argc, char **argv);    /* apx <名> <密码> */
int WifiCmdWait(int argc, char **argv);  /* wait [秒]       */

#endif /* WIFI_SERVICE_H */
