#ifndef APP_MAIN_H
#define APP_MAIN_H
/*
 * app_main.h - agent_test 主程序(命令表 + 启动编排)
 *
 * 分层: app_test.c 只有 main() 入口(仅函数调用, 无变量定义/宏);
 *       本头文件集中放置 主程序配置宏 + 命令表类型与声明 + 各函数声明;
 *       全部实现见 app_main.c, 各服务接口见 service 目录各头文件(配置区在各头文件)。
 *
 * 架构(接口隔离): 本模块只是"命令表 + 启动编排", 不含业务逻辑
 *   app_console.h/.c    控制台行读取 + shell 小工具(无业务)
 *   wifi_service.h/.c   WiFi 连接/状态/后台监控(配置在 wifi_service.h)
 *   llm_service.h/.c    LLM 问答(配置在 llm_service.h, 串口调试后门)
 *   servo_service.h/.c  PCA9685 舵机/Otto 动作(配置在 servo_service.h)
 *   agent_service.h/.c  LLM 智能体编排(能力上报/格式约束对话/表情动作分发;
 *                       能力表唯一来源, 语音链路云端指令也走这里分发)
 *   voice_service.h/.c  语音唤醒+三段式对话服务(ASR→agent→TTS, 配置在 voice_service.h;
 *                       开机自启, 执行体在 voice_dialog.c; agent 原始指令串口打印)
 *   key_service.h/.c    板上按键(自启已停用: 按键移交 ui_display 做表情切换)
 *
 * 【新增一条指令只需三步】
 *   1. 在对应服务 .c 实现 int XxxCmdName(int argc, char **argv)
 *   2. 在对应服务 .h 加声明
 *   3. 在 app_main.c 的 g_cmds 命令表加一行 {名字, 用法, 说明, 分组, 函数}
 *   help 输出自动跟随命令表, 无需手改。
 *
 * 【新增一个功能模块】仿照 servo_service 建 xxx_service.h/.c:
 *   语义接口 + Cmd 入口, 配置宏放自己的 .h, 命令表加行即接入。
 */

/* ============【主程序配置区】============ */
#define APP_NAME    "agent_test"
#define APP_VERSION "v5.0-3stage"
#define APP_LOCK    "/userdata/.agent_test.sock"  /* 单实例标记(unix socket) */

/* shell 命令处理函数原型(命令表 fn 字段绑定) */
typedef int (*ShellCmdFn)(int argc, char **argv);

/* 命令表条目 */
typedef struct {
    const char *group;    /* help 分组标题 */
    const char *name;     /* 指令名 */
    const char *usage;    /* 用法(help 展示) */
    const char *brief;    /* 一句说明 */
    ShellCmdFn   fn;      /* 处理函数(NULL=无动作, 如 quit/exit) */
    int          quit;    /* 1=执行后退出程序 */
} ShellCmd;

/* 命令表(定义在 app_main.c; help/查找按序遍历, 同分组需连续排列) */
extern const ShellCmd g_cmds[];

/* 主流程: 单实例保护 → 起服务 → 控制台命令循环; 返回进程退出码 */
int AppMainRun(int argc, char **argv);

/* 单实例保护(开机常驻 vs 手动 exec 二选一): 已有活实例返回 -1(调用方退出),
 * 否则占坑返回 0; 实现见 app_main.c(unix socket 标记, 崩溃残留可探测清理) */
int AppSingleInstanceGuard(int headless);

/* 按名查命令; 未找到返回 NULL */
const ShellCmd *AppCmdFind(const char *name);

/* ---- 主模块 shell 指令入口(命令表绑定) ---- */
int CmdHelp(int argc, char **argv);   /* help | ?  本帮助   */
int CmdVer(int argc, char **argv);    /* ver       版本信息 */

#endif /* APP_MAIN_H */
