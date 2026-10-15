/*
 * app_main.c - agent_test 主程序实现(命令表 + 启动编排)
 * 接口与配置见 app_main.h; app_test.c 只有 main() 入口。
 *
 * 启动流程: 连 WiFi(最长60s) → LLM 打招呼(最长90s) → 控制台 cmd>
 *   后台: WiFi 监控线程 + 语音唤醒循环(喊 "Luna" 对话, 云端指令切表情/做动作)
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "app_main.h"
#include "app_console.h"
#include "wifi_service.h"
#include "llm_service.h"
#include "servo_service.h"
#include "agent_service.h"
#include "voice_service.h"
#include "key_service.h"

/* ---------------- 单实例保护 ----------------
 * 开机由 init 常驻拉起(app_test boot); 手动 exec 是第二个实例,
 * 检测到已有实例在跑时直接退出并提示(麦克风/动作队列不能两份抢)。
 * 用 unix socket 而非 pid 文件: LiteOS-A 开机 pid 分配确定性强,
 * pid 文件在重启后极易撞上被复用的 pid 造成永久误判;
 * socket 方案崩溃残留也可探测(连不上=残留, 清掉重来)。 */
int AppSingleInstanceGuard(int headless)
{
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        return 0;   /* socket 不可用: 放行(行为同旧版) */
    }
    (void)memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    (void)strncpy(addr.sun_path, APP_LOCK, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EADDRINUSE && errno != EACCES) {
            close(fd);
            return 0;   /* 如 /userdata 未挂载: 放行 */
        }
        {
            /* 标记文件还在: 区分活实例 vs 崩溃残留 */
            int probe = socket(AF_UNIX, SOCK_STREAM, 0);
            int alive = (probe >= 0) &&
                        (connect(probe, (struct sockaddr *)&addr,
                                 sizeof(addr)) == 0);

            if (probe >= 0) {
                close(probe);
            }
            if (alive) {
                printf("[%s] 已有实例在运行(开机自启), 本次退出。\r\n"
                       "[%s] 常驻模式下语音/WiFi 均自动工作, 无需手动 exec;\r\n"
                       "[%s] 如需交互控制台, 请先处理常驻实例或重启板子。\r\n",
                       APP_NAME, APP_NAME, APP_NAME);
                close(fd);
                if (headless) {
                    sleep(300);     /* init 会重拉: 慢退出, 减少噪音 */
                }
                return -1;
            }
            /* 没人监听 = 残留(上次掉电/被杀): 清掉重来 */
            (void)unlink(APP_LOCK);
            if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                close(fd);
                return 0;
            }
        }
    }
    (void)listen(fd, 1);    /* 让后续实例的 connect 探测有效 */
    return 0;               /* fd 常开不关: 进程退出即释放标记 */
}

/* ---------------- 命令表 ---------------- */

/* 本模块适配器: 无参语义接口 -> shell 命令原型(int(int, char**)),
 * 避免命令表直接绑定 int(void) 函数产生指针类型不匹配 */
static int CmdStopAll(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return AgentSvcStopAll();
}

#define CMD_N (sizeof(g_cmds) / sizeof(g_cmds[0]))

const ShellCmd g_cmds[] = {
    /* group, name,  usage,                        brief,                     fn,            quit */
    {"WiFi", "ip",    "ip",                         "WiFi 状态(IP/是否在线)",     WifiCmdIp,    0},
    {"WiFi", "apx",   "apx <热点> <密码>",           "连接热点(掉线守护自动重连)", WifiCmdAp,    0},
    {"WiFi", "wait",  "wait [秒]",                  "阻塞等拿 IP(默认 20)",       WifiCmdWait,  0},
    {"LLM",  "ask",   "ask <内容...>",              "问 LLM(ASCII 一行)",         LlmCmdAsk,    0},
    {"LLM",  "uni",   "uni <码点...>",              "问 LLM(中文) 例: uni 4f60 597d", LlmCmdUni, 0},
    {"智能体", "chat",  "chat <文字...>",            "和大模型对话, 自动切表情/做动作", AgentCmdChat, 0},
    {"智能体", "chatu", "chatu <码点...>",           "中文对话(码点), 同 chat",     AgentCmdChatU, 0},
    {"舵机", "scan",  "scan [bus]",                 "扫描 I2C 总线器件",          ServoCmdScan,  0},
    {"舵机", "pinit", "pinit [bus] [addr]",         "初始化 PCA9685",             ServoCmdInit,  0},
    {"舵机", "pstatus", "pstatus",                  "PCA 就绪态(本进程)",         ServoCmdStatus, 0},
    {"舵机", "angle", "angle <ch> <deg>",           "单舵机角度 0~180 (ch 0~15)", ServoCmdAngle, 0},
    {"舵机", "us",    "us <ch> <us>",               "单舵机脉宽 500~2500",        ServoCmdUs,    0},
    {"舵机", "run",   "run <动作> [参数...]",        "阻塞执行动作, 例: run walk 2 800 1 0", ServoCmdRun, 0},
    {"舵机", "post",  "post <动作> [参数...]",       "后台执行动作(stop 中止)",    ServoCmdPost,  0},
    {"舵机", "stop",  "stop",                       "中止当前动作+清空队列并归位", CmdStopAll, 0},
    {"舵机", "off",   "off",                        "中止 + 全通道停脉冲(失力)",  ServoCmdOff,   0},
    {"舵机", "list",  "list",                       "Otto 动作表",                ServoCmdList,  0},
    {"舵机", "map",   "map",                        "肢体通道映射",               ServoCmdMap,   0},
    {"舵机", "pose",  "pose",                       "当前姿态",                   ServoCmdPose,  0},
    {"舵机", "trim",  "trim <ll..rh|0~5> <-50~50>", "肢体微调(度)",               ServoCmdTrim,  0},
    {"按键", "key",   "key",                        "按键状态(自启已停用, 按键移交 ui_display)", KeyCmdStatus, 0},
    {"语音", "voice", "voice start|stop|status",    "语音唤醒+三段式对话(开机自启)", VoiceCmdVoice, 0},
    {"语音", "vol",   "vol [0~100]",                "播放音量(0=静音 100=最大, 不带参数查看)", VoiceCmdVol, 0},
    {"其它", "help",  "help | ?",                   "本帮助",                     CmdHelp,       0},
    {"其它", "ver",   "ver",                        "版本信息",                   CmdVer,        0},
    {"其它", "quit",  "quit | exit",                "退出(程序结束)",             NULL,          1},
    {"其它", "exit",  "exit",                       "退出(同 quit)",              NULL,          1},
};

int CmdHelp(int argc, char **argv)
{
    const char *group = NULL;
    size_t i;

    (void)argc;
    (void)argv;
    for (i = 0; i < CMD_N; i++) {
        if (group == NULL || strcmp(group, g_cmds[i].group) != 0) {
            group = g_cmds[i].group;
            printf("%s:\r\n", group);
        }
        printf("  %-26s %s\r\n", g_cmds[i].usage, g_cmds[i].brief);
    }
    printf("提示: 空闲时自动监控 WiFi(掉线报警/恢复自动重新问答), 无需操作\r\n");
    return 0;
}

int CmdVer(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("%s %s (WiFi+LLM+PCA9685, %s %s)\r\n",
           APP_NAME, APP_VERSION, __DATE__, __TIME__);
    return 0;
}

const ShellCmd *AppCmdFind(const char *name)
{
    size_t i;

    for (i = 0; i < CMD_N; i++) {
        if (strcmp(g_cmds[i].name, name) == 0) {
            return &g_cmds[i];
        }
    }
    return NULL;
}

/* ---------------- 主流程 ---------------- */

/* WiFi 恢复回调适配器: WifiRecoverHook(void) -> LlmSvcGreet(void) 返回值丢弃 */
static void RecoverGreet(void)
{
    (void)LlmSvcGreet();
}

int AppMainRun(int argc, char **argv)
{
    static char line[CONSOLE_LINE_MAX];
    char *v[CONSOLE_ARGV_MAX];
    int nv;
    int online;
    /* boot 参数 = init 开机拉起的常驻模式: 不占串口输入(留给 mksh),
     * 日志照常上串口; 不带参数手动 exec = 交互控制台模式(旧用法) */
    int headless = (argc > 1 && strcmp(argv[1], "boot") == 0);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\r\n=== %s %s: WiFi + LLM + PCA9685 舵机 (hispark_taurus) ===\r\n",
           APP_NAME, APP_VERSION);
    printf("[main] 模式: %s\r\n", headless ? "开机常驻(自动工作)" : "交互控制台");
    printf("[wifi] ssid=\"%s\" psk=%s wait=%ds\r\n", WIFI_CFG_SSID,
           (WIFI_CFG_PSK[0] == '\0') ? "(空)" : "(已填)", WIFI_CFG_WAIT_SEC);
    printf("[llm]  gw=%s:%d vhost=%s model=%s\r\n",
           LLM_CFG_IP, LLM_CFG_PORT, LLM_CFG_VHOST, LLM_CFG_MODEL);
    printf("[pca]  bus=%d addr=0x%02x (首次舵机命令时才初始化)\r\n",
           (int)SERVO_CFG_BUS, (int)SERVO_CFG_ADDR);
    printf("[voice] 唤醒词=\"%s\" 云端=三段式(阿里云ASR -> agent -> 阿里云TTS; agent 原始指令串口打印)\r\n",
           VOICE_CFG_WAKE_WORD);
    if (WIFI_CFG_SSID[0] == '\0' || WIFI_CFG_PSK[0] == '\0') {
        printf("[wifi] !! 先编辑 wifi_service.h 配置区的 SSID/PSK 再编译\r\n");
    }
    if (AppSingleInstanceGuard(headless) != 0) {
        return 0;
    }

    /* 0. 按键服务: 已停用自启 —— 板上按键移交给 ui_display(屏上表情切换:
     *    key1=上一个表情, key2=下一个表情), GPIO 事件只能被一个进程消费,
     *    两处同时扫描会重复响应。需要本服务(key1 随机动作/key2 复位)时,
     *    取消下行注释重编, 并注意与屏端按键功能互斥。 */
    /* (void)KeySvcStart(); */

    /* 1. 语音唤醒最先起(本地检测不依赖网络): 开机几秒即可喊 "Luna";
     *    WiFi 未连时唤醒照常监听, 对话链路等联网后自动可用 */
    if (VoiceSvcStart() == 0) {
        printf("[voice] 唤醒监听启动中, 喊 \"%s\" 开始对话\r\n",
               VOICE_CFG_WAKE_WORD);
    } else {
        printf("[voice] 启动失败(见 [voice] 日志), 稍后 voice start 重试\r\n");
    }

    /* 2. WiFi(失败不退出, 后台每 30s 自动重试, 连上自动恢复) */
    online = (WifiSvcStart() == 0);
    printf("[wifi] ----- 启动段完成 -----\r\n");

    /* 3. 掉线恢复 -> 自动重新问答(监控线程回调, 编排在主程序接线) */
#if LLM_CFG_ASK_ON_RECOVER
    WifiSvcSetRecoverHook(RecoverGreet);
#endif

    /* 4. LLM 打招呼(连上才问) */
    printf("[llm]  ===== 云端问答(连上才问) =====\r\n");
    if (online) {
        (void)LlmSvcGreet();
        /* 4.1 首次连云上报能力清单(动作/表情 + 输出格式约束, 只发一次;
         *     协议见 docs/能力清单与LLM协议.md; 每次对话仍会内嵌能力 prompt) */
        (void)AgentSvcAnnounce();
    } else {
        printf("[llm] 跳过(WiFi 未连接), 连上后自动补打招呼\r\n");
    }
    printf("[llm] ----- 启动段完成 -----\r\n");

    /* 5. WiFi 监控线程(掉线报警/自动重试/恢复回调) */
    WifiSvcMonitorStart();

    if (headless) {
        printf("[main] 常驻模式: WiFi 监控+自动重试 与 语音唤醒 后台运行中, 无需操作\r\n");
        for (;;) {
            sleep(3600);    /* 服务全在后台线程; 退出会被 init 重新拉起 */
        }
    }

    printf("\r\n[main] 控制台就绪: 输入 help 看全部指令; quit 退出\r\n");
    for (;;) {
        const ShellCmd *cmd;
        int len = ConsoleReadline("cmd> ", line, sizeof(line));

        if (len == 0) {
            continue;
        }
        nv = ShellSplit(line, v, CONSOLE_ARGV_MAX);
        if (nv == 0) {
            continue;
        }
        cmd = AppCmdFind(v[0]);
        if (cmd == NULL) {
            printf("unknown cmd: %s (try 'help')\r\n", v[0]);
            continue;
        }
        if (cmd->fn != NULL) {
            (void)cmd->fn(nv, v);
        }
        if (cmd->quit) {
            printf("[%s] bye (WiFi 守护仍在后台)\r\n", APP_NAME);
            (void)unlink(APP_LOCK);     /* 释放单实例标记 */
            break;
        }
    }
    return 0;
}
