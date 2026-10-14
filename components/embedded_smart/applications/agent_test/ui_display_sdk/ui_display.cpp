/*
 * ui_display - HiSpark AI Camera(Hi3516DV300) 配套屏 UI: main 组装层
 *
 * 职责边界(分层解耦):
 *   gif_decoder.*    纯 GIF 解码器(C, 零 UI 依赖)
 *   emoji_display.*  表情库: 调用函数即可显示/切换表情, 自持视图
 *   ui_design.*      UI 设计层: 布局/样式/控件创建(统一改设计的地方)
 *   本文件(main)     启动图形/创建窗口/组装 UI/绑定事件行为/任务循环
 *
 * 基于 OpenHarmony 官方示例 sample_ui.cpp 改造:
 *   - 通过 wms_client 连接 wms_server(开机自启) 创建全屏窗口
 *   - wms_server 负责把窗口合成到 HDF 显示(LCD_ICN9700, MIPI 屏)
 *
 * 运行前提:
 *   - 系统里 wms_server 已随 init 启动(init_liteos_a_3516dv300.cfg 中已配置)
 *   - 字体与 GIF 资产在 userfs 分区 data/ 下(构建时自动打包)
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "common/graphic_startup.h"
#include "common/task_manager.h"
#include "components/root_view.h"
#include "emoji_display.h"
#include "font/ui_font_vector.h"
#include "gfx_utils/graphic_log.h"
#include "graphic_config.h"
#include "key_driver.h"
#include "ui_design.h"
#include "window/window.h"

using namespace OHOS;

namespace {
/* 字体缓存(静态分配, 官方示例 sample_ui 同款做法) */
uint32_t g_fontMemBaseAddr[MIN_FONT_PSRAM_LENGTH / 4];
uint8_t g_icuMemBaseAddr[SHAPING_WORD_DICT_LENGTH];

/*
 * 字体目录: 本产品 userfs 分区(liteos_a los_rootfs.c)挂载在 /storage,
 * 字体文件在镜像 data/ 子目录, 即 /storage/data/SourceHanSansSC-Regular.otf。
 * ui_lite 默认的 VECTOR_FONT_DIR(/user/data/)在本产品不存在。
 */
constexpr const char *FONT_DIR = "/storage/data/";
constexpr const char *FONT_FILE = DEFAULT_VECTOR_FONT_FILENAME;
constexpr const char *BRK_FILE = DEFAULT_LINE_BREAK_RULE_FILENAME;

/* 表情包 GIF 目录: userfs 分区 data/emoji/(xiaozhi 21 个标准表情) */
constexpr const char *EMOJI_DIR = "/storage/data/emoji";

/* 跨进程表情命令通道: app_test(LLM 智能体)写一行表情名到本文件,
 * 本进程主循环每 20 tick(约 200ms)读取一次, 读到即删(协议见
 * app_test/docs/能力清单与LLM协议.md §4)。在主循环(UI 线程)里消费, 无并发问题 */
constexpr const char *EMOJI_CMD_FILE = "/storage/data/emoji_cmd";

/* 跨进程字幕通道: app_test(voice_dialog)把大模型回复文本全量写到本文件
 * (空内容=清空字幕), 本进程轮询显示。与 emoji_cmd 不同: 读后不删,
 * 靠内容比较去重(文件始终反映当前字幕) */
constexpr const char *CAPTION_FILE = "/storage/data/caption_cmd";

/* ---- 行为层: 事件回调(界面长什么样在 ui_design, 点了干什么在这里) ---- */

int g_touchCount = 0;

class TouchClickListener : public UIView::OnClickListener {
public:
    bool OnClick(UIView &view, const ClickEvent &event) override
    {
        (void)view;
        (void)event;
        g_touchCount++;
        char buf[32];
        if (snprintf(buf, sizeof(buf), "Touch x%d", g_touchCount) > 0) {
            UiDesign_SetCountText(buf);
        }
        return true;
    }
};

class EmojiSwitchListener : public UIView::OnClickListener {
public:
    bool OnClick(UIView &view, const ClickEvent &event) override
    {
        (void)view;
        (void)event;
        if (EmojiDisplay_Next()) {
            UiDesign_SetEmotionText(EmojiDisplay_CurrentName());
        }
        return true;
    }
};

TouchClickListener g_clickListener;
EmojiSwitchListener g_emojiClickListener;

/* ---- 板上实体按键: key1(GPIO0_2)=上一个表情, key2(GPIO0_1)=下一个表情 ----
 * 事件来自 key_sdk 扫描线程, 回调只做计数; 主任务循环里消费(与 UI 同线程,
 * 无并发问题)。所有表情循环(SetEmotionByIndex 自带回绕)。
 * 注意: GPIO 事件只能被一个进程消费, app_test 的 KeySvcStart() 已停用自启。 */
constexpr ::KeyDesc g_keys[] = {
    {"KEY1_PREV", 2, 1}, /* key1: GPIO0_2, 实测按下为低 */
    {"KEY2_NEXT", 1, 1}, /* key2: GPIO0_1, 实测按下为低 */
};
constexpr uint32_t KEY_NUM = sizeof(g_keys) / sizeof(g_keys[0]);

volatile int g_keyPrevReq = 0;
volatile int g_keyNextReq = 0;

/* ::KeyDesc/::KeyEvent 用全局限定: ui_lite 里有同名 OHOS::KeyEvent 类, 免歧义 */
void KeyCallback(const ::KeyDesc *key, ::KeyEvent event)
{
    if (event != KEY_EVENT_PRESS) {
        return;
    }
    if (key == &g_keys[0]) {
        g_keyPrevReq++;
    } else if (key == &g_keys[1]) {
        g_keyNextReq++;
    }
}

/* 消费按键请求: 返回是否发生了表情切换(需刷新表情名标签) */
bool DrainKeyRequests()
{
    bool changed = false;
    while (g_keyPrevReq > 0) {
        g_keyPrevReq--;
        if (EmojiDisplay_Prev()) {
            changed = true;
        }
    }
    while (g_keyNextReq > 0) {
        g_keyNextReq--;
        if (EmojiDisplay_Next()) {
            changed = true;
        }
    }
    return changed;
}

/* 轮询跨进程表情命令文件(app_test 的 LLM 智能体写入); 读到即删并切换 */
void PollEmojiCmd()
{
    static int tick = 0;
    FILE *f;
    char name[24];

    if (++tick < 20) { /* 约 200ms 查一次, 足够跟手且省 IO */
        return;
    }
    tick = 0;
    f = fopen(EMOJI_CMD_FILE, "r");
    if (f == nullptr) {
        return;
    }
    if (fgets(name, sizeof(name), f) != nullptr) {
        size_t len = strlen(name);
        while (len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' ||
                           name[len - 1] == ' ')) {
            name[--len] = '\0';
        }
        if (len > 0) {
            if (EmojiDisplay_SetEmotion(name)) {
                UiDesign_SetEmotionText(name);
                printf("[ui_display] emoji cmd: %s\n", name);
            } else {
                printf("[ui_display] emoji cmd unknown: %s\n", name);
            }
        }
    }
    (void)fclose(f);
    (void)remove(EMOJI_CMD_FILE);
}

/* ---- 字幕语音同步(逐字揭示, 节奏跟语音) ----
 * 通道: voice_dialog 单文件追加式写 cap.log(按行: "C"=清屏/新回合,
 * "A<delta>"=追加回复文本, "P已播/已收"=播放进度), 本进程记偏移增量读。
 * 揭示规则(150ms 一拍, 每拍最多 1 字 ≈ 6.7 字/秒, 与语速相当):
 *   - 语音未开播(已收=0): 不出字(最多等约 2s, 超时兜底揭示);
 *   - 语音播到哪揭到哪(T = 已播/已收*文本长): 揭示不超前语音;
 *   - 每拍最多 1 字: 音频突发入队时揭示也不跳字(可读, 不糊屏);
 *   - 语音播完(已播>=已收): 剩余文本立即补完, 字幕与语音同步收尾;
 *   - 全部显示完停留约 5s 自动清空。
 * 窗口由设计层按实测像素收窄且高度=一行行高, 永远单行。 */
#define CAP_BUF         2100 /* 目标全文缓冲上限(超长丢头部留窗口) */
#define CAP_PERIOD      15   /* 主循环 10ms 一拍, 15 拍 = 150ms/字 */
#define CAP_DWELL_TICKS 5   /* 全部显示完后再停留约 3s(20 拍)自动清空 */
#define CAPTION_LOG     "/storage/data/cap.log"

static char g_capTarget[CAP_BUF]; /* 目标全文('A' 行增量累积) */
static int g_capShown = 0;        /* 已揭示字节数(UTF-8 字符边界对齐) */
static int g_capTick = 0;
static int g_capDwell = 0;        /* 显完停留拍数(到 CAP_DWELL_TICKS 自动清空) */
static int g_capWait = 0;         /* 无进展拍数(超 13 拍≈2s 兜底揭示, 防进度通道失效卡死) */
static bool g_capArmed = false;   /* 见到 'C' 才消费: 屏蔽开机残留字幕 */
static bool g_capDone = false;    /* 见到 'D'(回合结束): 立即补完剩余文本 */
static unsigned long g_progPlayed = 0; /* 进度行: 已播音频字节 */
static unsigned long g_progRecv = 0;   /* 进度行: 已收音频字节 */
static bool g_progSeen = false;        /* 是否见过进度行 */
static bool g_capLoggedStart = false;  /* 每轮诊断: 首次揭示已打印 */
static long g_capOff = 0;              /* 通道文件读偏移(增量读) */
static char g_capLine[300];            /* 行重组缓冲(残行跨拍拼接) */
static int g_capLineLen = 0;

/* 从 pos 起推进 nchars 个 UTF-8 字符, 返回新的字节偏移 */
static int CapAdvance(const char *s, int pos, int nchars)
{
    while (nchars-- > 0 && s[pos] != '\0') {
        pos++;
        while ((s[pos] & 0xC0) == 0x80) { /* 跳过续字节 */
            pos++;
        }
    }
    return pos;
}

/* 消费一行通道命令 */
static void CapHandleLine(const char *line)
{
    if (line[0] == 'C') {
        g_capArmed = true;
        g_capShown = 0;
        g_capTarget[0] = '\0';
        g_progPlayed = 0;   /* 新一轮: 进度视图归零, 防上一轮陈旧值
                             * (played>=recv 立即补完 / played=0 永不出字) */
        g_progRecv = 0;
        g_capWait = 0;
        g_capDwell = 0;
        g_capDone = false;
        g_capLoggedStart = false;
        printf("[cap] clear\n");
        UiDesign_SetCaption(""); /* 新一轮: 隐藏, 等语音开播再逐字出 */
    } else if (line[0] == 'A' && g_capArmed) {
        int cur = (int)strlen(g_capTarget);
        int add = (int)strlen(&line[1]);
        int drop;

        if (add <= 0) {
            return;
        }
        if (cur == 0) { /* 每轮第一条: 诊断用 */
            printf("[cap] first text %d bytes\n", add);
        }
        if (cur + add >= CAP_BUF) { /* 溢出: 丢头部保留最近窗口 */
            drop = cur + add - (CAP_BUF - 1);
            (void)memmove(g_capTarget, g_capTarget + drop, (size_t)(cur - drop));
            cur -= drop;
            while ((g_capTarget[0] & 0xC0) == 0x80 && cur > 0) { /* UTF-8 对齐 */
                (void)memmove(g_capTarget, g_capTarget + 1, (size_t)cur);
                cur--;
            }
            g_capTarget[cur] = '\0';
            g_capShown -= drop; /* 揭示偏移随头部丢弃平移 */
            while (g_capShown > 0 && (g_capTarget[g_capShown] & 0xC0) == 0x80) {
                g_capShown--;   /* 窗口尾对齐 UTF-8 字符边界 */
            }
            if (g_capShown < 0) {
                g_capShown = 0;
            }
        }
        (void)memcpy(g_capTarget + cur, &line[1], (size_t)add + 1);
        g_capWait = 0;
    } else if (line[0] == 'D' && g_capArmed) {
        g_capDone = true;   /* 回合结束: PollCaption 立即补完剩余文本 */
    } else if (line[0] == 'P' && g_capArmed) {
        unsigned long p, r;

        if (sscanf(&line[1], "%lu/%lu", &p, &r) == 2) {
            g_progSeen = true;
            g_progPlayed = p;
            g_progRecv = r;
        }
    }
    /* 其它行: 忽略 */
}

void PollCaption()
{
    static char rbuf[512];
    FILE *f;
    int len;

    if (++g_capTick % CAP_PERIOD != 0) { /* 150ms 节拍(含文件 IO) */
        return;
    }

    /* 1) 增量读通道新字节, 按行拼装消费(残行留到下一拍) */
    f = fopen(CAPTION_LOG, "r");
    if (f == nullptr) {
        static int failStreak = 0;

        failStreak++;
        if (failStreak % 33 == 1) { /* 约 5s 打一条, 不刷屏 */
            printf("[cap-r] open fail x%d\n", failStreak);
        }
    } else {
        long size;
        size_t n, i;

        (void)fseek(f, 0, SEEK_END);
        size = ftell(f);
        if (size < g_capOff) { /* 文件被重建(新回合): 偏移与残行归零 */
            g_capOff = 0;
            g_capLineLen = 0;
        }
        if (size > g_capOff) {
            /* 偏移自校验(仅行边界状态): 追加式文件偏移处的前一字节必须是
             * 行尾 '\n'。文件被重建(新回合截断重写 C 行)后, 若新文件长度
             * 已超过旧偏移, "size<offset" 检测失效, 'C' 行被跳过 -> 上一轮
             * 文本残留且新回复开头显示不出来(实测踩坑)。校验失败=偏移失效,
             * 归零重读, 'C' 必被消费 */
            if (g_capOff > 0 && g_capLineLen == 0) {
                (void)fseek(f, g_capOff - 1, SEEK_SET);
                if (fgetc(f) != '\n') {
                    g_capOff = 0;
                    g_capLineLen = 0;
                }
            }
            (void)fseek(f, g_capOff, SEEK_SET);
            n = fread(rbuf, 1, sizeof(rbuf), f);
            g_capOff += (long)n;
            for (i = 0; i < n; i++) {
                if (rbuf[i] == '\n') {
                    g_capLine[g_capLineLen] = '\0';
                    CapHandleLine(g_capLine);
                    g_capLineLen = 0;
                } else if (g_capLineLen < (int)sizeof(g_capLine) - 1) {
                    g_capLine[g_capLineLen++] = rbuf[i];
                } else {
                    g_capLineLen = 0; /* 超长残行: 丢弃(正常行<=260) */
                }
            }
        }
        (void)fclose(f);
    }

    /* 2) 揭示节奏 */
    len = (int)strlen(g_capTarget);
    if (len <= 0) {
        return;
    }
    if (g_capDone && g_capShown < len) {
        /* 回合已结束(response.done): 无论语音/进度什么状态, 剩余文本立即补完,
         * 随后进入 dwell 停留计时 -> 5s 后自动清空(修字幕滞留不消失) */
        g_capShown = len;
        printf("[cap] done -> complete\n");
        UiDesign_SetCaptionWindow(g_capTarget, g_capShown);
    }
    if (g_capShown >= len) {
        /* 全部显示完(逐字追平或语音播完补完): 停留约 5s 后自动清空 */
        g_capDwell++;
        if (g_capDwell >= CAP_DWELL_TICKS) {
            printf("[cap] dwell done, hide\n");
            g_capTarget[0] = '\0';
            g_capShown = 0;
            g_capDwell = 0;
            UiDesign_SetCaption("");
        }
        return;
    }
    g_capDwell = 0;
    if (g_progSeen && g_progRecv > 0 && g_progPlayed >= g_progRecv) {
        if (g_capShown < len) { /* 语音播完: 剩余文本立即补完 */
            g_capShown = len;
            printf("[cap] voice done -> complete\n");
            UiDesign_SetCaptionWindow(g_capTarget, g_capShown);
        }
        return;
    }
    if (g_progSeen && g_progRecv > 0) {
        /* 语音播到哪揭到哪(不超前), 但每拍最多推进 1 字保持可读 */
        unsigned long long t = (unsigned long long)g_progPlayed *
                               (unsigned long long)len /
                               (unsigned long long)g_progRecv;
        int target = (t >= (unsigned long long)len) ? len : (int)t;

        while (target > 0 && (g_capTarget[target] & 0xC0) == 0x80) {
            target--; /* 对齐 UTF-8 字符边界 */
        }
        if (target <= g_capShown) {
            g_capWait++;
            if (g_capWait <= 13) {
                return; /* 语音还没播到: 等待(最多约 2s, 超时兜底揭示) */
            }
        } else {
            g_capWait = 0;
        }
        g_capShown = CapAdvance(g_capTarget, g_capShown, 1);
        if (!g_capLoggedStart) {
            g_capLoggedStart = true;
            printf("[cap] reveal start (shown=%d len=%d pos P%lu/%lu)\n",
                   g_capShown, len, g_progPlayed, g_progRecv);
        }
    } else if (!g_progSeen) {
        g_capShown = CapAdvance(g_capTarget, g_capShown, 1); /* 无进度: 6.7 字/秒 */
        if (!g_capLoggedStart) {
            g_capLoggedStart = true;
            printf("[cap] reveal start (no progress, len=%d)\n", len);
        }
    } else {
        static bool loggedWait = false;

        if (!loggedWait) {
            loggedWait = true;
            printf("[cap] waiting voice (recv=0, len=%d)\n", len);
        }
        g_capWait++;
        if (g_capWait <= 13) {
            return; /* 语音未开播: 最多等约 2s, 超时按无进度兜底揭示 */
        }
        g_capShown = CapAdvance(g_capTarget, g_capShown, 1);
        if (!g_capLoggedStart) {
            g_capLoggedStart = true;
            printf("[cap] reveal start (voice wait timeout, len=%d)\n", len);
        }
    }
    UiDesign_SetCaptionWindow(g_capTarget, g_capShown);
}

/* ---- WiFi 连接监控(独立线程, 不占 UI 线程) ----
 * 在线判定 = wlan0 有 IP 且 TCP 能连通公网(223.5.5.5:53, 1.5s 超时):
 *   - 只查链路标志(SIOCGIFFLAGS): LwIP 的 RUNNING 位常不置位, WiFi 在线图标不亮;
 *   - 只查 IP: 掉关联后 IP 残留在网卡上(LwIP 不自动清), 断网图标不灭;
 * TCP 连通检测对两个场景都正确。连续 WIFI_DOWN_STREAK 次失败才判离线
 * (防探测抖动误灭), 成功立即判在线(亮灯快)。
 * 网络栈操作要与 wpa_supplicant 抢锁, 绝不能放 UI 线程(会卡动画):
 * 独立线程 500ms 轮询, 结果经 volatile 标志交 UI 主循环消费(纯内存读)。 */
#define WIFI_PROBE_IP        "223.5.5.5"
#define WIFI_PROBE_PORT      53
#define WIFI_PROBE_TIMEOUT_MS 800   /* 局域网内 TCP 握手 <100ms, 断网快速判灭 */
#define WIFI_DOWN_STREAK     2
volatile bool g_wifiConnected = false; /* 监控线程写, UI 线程读 */
volatile bool g_wifiDirty = true;      /* true=状态有更新待 UI 消费 */

/* 非阻塞 TCP 连通探测: 握手成功 = 有真实外网路由, 断网/仅残留 IP 时失败 */
static bool TcpProbe()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    struct timeval tv;
    fd_set wset;
    int flags, err = 0;
    socklen_t elen = sizeof(err);
    bool ok = false;

    if (fd < 0) {
        return false;
    }
    (void)memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(WIFI_PROBE_PORT);
    if (inet_pton(AF_INET, WIFI_PROBE_IP, &sa.sin_addr) != 1) {
        close(fd);
        return false;
    }
    flags = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&sa), sizeof(sa)) == 0) {
        ok = true;   /* 立即连上(本机路由缓存热) */
    } else if (errno == EINPROGRESS) {
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        tv.tv_sec = WIFI_PROBE_TIMEOUT_MS / 1000;
        tv.tv_usec = (WIFI_PROBE_TIMEOUT_MS % 1000) * 1000;
        if (select(fd + 1, nullptr, &wset, nullptr, &tv) > 0 &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 &&
            err == 0) {
            ok = true;
        }
    }
    close(fd);
    return ok;
}

void *WifiMonitorThread(void *arg)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int downStreak = 0;
    (void)arg;
    if (fd < 0) {
        printf("[ui_display] wifi monitor: socket failed, monitor off\n");
        return nullptr;
    }
    while (true) {
        struct ifreq ifr;
        struct sockaddr_in *sin;
        char ip[16] = {0};
        bool hasIp = false;
        bool connected;

        (void)memset(&ifr, 0, sizeof(ifr));
        (void)strcpy(ifr.ifr_name, "wlan0"); /* 固定 5+1 字节, 无溢出 */
        if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
            sin = (struct sockaddr_in *)&ifr.ifr_addr;
            if (sin->sin_family == AF_INET && sin->sin_addr.s_addr != 0 &&
                inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)) != nullptr &&
                strcmp(ip, "0.0.0.0") != 0) {
                hasIp = true;
            }
        }

        if (!hasIp) {
            connected = false;
            downStreak = 0;
        } else if (TcpProbe()) {
            connected = true;
            downStreak = 0;
        } else if (++downStreak >= WIFI_DOWN_STREAK) {
            connected = false;   /* 连续失败: 真离线(IP 残留场景) */
        } else {
            connected = g_wifiConnected;   /* 防抖: 未到阈值维持原态 */
        }

        if (connected != g_wifiConnected) {
            g_wifiConnected = connected;
            g_wifiDirty = true;
            printf("[ui_display] wifi %s\n", connected ? ip : "offline");
        }
        usleep(500 * 1000); /* 500ms 轮询: 亮灯延迟 ≈ 半个周期+探测耗时 */
    }
    return nullptr; /* 不可达 */
}

/* UI 主循环消费: 只读内存标志, 零系统调用 */
void PollWifiState()
{
    if (g_wifiDirty) {
        g_wifiDirty = false;
        UiDesign_SetWifiConnected(g_wifiConnected);
    }
}

/* ---- 启动与组装 ---- */

void InitFontEngine()
{
    GraphicStartUp::InitFontEngine(reinterpret_cast<uintptr_t>(g_fontMemBaseAddr), MIN_FONT_PSRAM_LENGTH,
                                   FONT_DIR, FONT_FILE);
#if ENABLE_ICU
    GraphicStartUp::InitLineBreakEngine(reinterpret_cast<uintptr_t>(g_icuMemBaseAddr), SHAPING_WORD_DICT_LENGTH,
                                        FONT_DIR, BRK_FILE);
#endif
}

void BindEvents()
{
    UILabelButton *touchBtn = UiDesign_GetTouchButton();
    if (touchBtn != nullptr) {
        touchBtn->SetOnClickListener(&g_clickListener);
    }
    UILabelButton *nextBtn = UiDesign_GetNextButton();
    if (nextBtn != nullptr) {
        nextBtn->SetOnClickListener(&g_emojiClickListener);
    }
}

void RunApp()
{
    RootView *root = RootView::GetInstance();
    root->SetPosition(0, 0);
    root->Resize(design::SCREEN_W, design::SCREEN_H);
    root->SetStyle(STYLE_BACKGROUND_COLOR, Color::Olive().full);
    root->SetStyle(STYLE_BACKGROUND_OPA, OPA_OPAQUE);
    printf("[ui_display] RunApp: root=%dx%d bg olive ok\n", design::SCREEN_W, design::SCREEN_H);

    /* 表情库先于设计层初始化(设计层需要取它的视图来摆放) */
    if (!EmojiDisplay_Init(EMOJI_DIR)) {
        printf("[ui_display] RunApp: emoji lib init failed\n");
    }

    /* UI 设计层构建整棵视图树(外观与布局) */
    UiDesign_Build(root);

    /* main 层绑定事件行为 */
    BindEvents();

    /* 播放默认表情; GIF 资产缺失(未烧 userfs)时 UI 其余部分照常工作 */
    if (EmojiDisplay_SetEmotion("neutral")) {
        UiDesign_SetEmotionText(EmojiDisplay_CurrentName());
        printf("[ui_display] RunApp: emoji library ready\n");
    } else {
        printf("[ui_display] RunApp: emoji gif not found at %s (userfs flashed?)\n", EMOJI_DIR);
    }
}
} // namespace

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("[ui_display] main: start\n");
    GraphicStartUp::Init();
    InitFontEngine();
    printf("[ui_display] main: GraphicStartUp+Font done\n");

    WindowConfig config = {};
    config.rect.SetRect(0, 0, design::SCREEN_W - 1, design::SCREEN_H - 1);
    /*
     * wms_server 由 init 异步拉起, 且 foundation(samgr/CMS 宿主)加载 24 个共享库
     * 需要数秒, 就绪前所有 IPC 都报 "Liteipc ServiceManager not set!"。
     * 实测启动竞态窗口可达数秒, 因此不设重试上限, 等窗口创建成功为止。
     */
    Window *window = nullptr;
    int retry = 0;
    while (window == nullptr) {
        window = Window::CreateWindow(config);
        if (window != nullptr) {
            break;
        }
        if (retry % 30 == 0) { /* 每 3s 打一条日志, 避免刷屏 */
            GRAPHIC_LOGE("ui_display: wms/samgr not ready, retry %d", retry);
        }
        retry++;
        usleep(100 * 1000); /* 100ms 一次 */
    }
    if (window == nullptr) {
        GRAPHIC_LOGE("ui_display: create window failed");
        return 1;
    }
    printf("[ui_display] main: window created id=%d\n", window->GetWindowId());
    window->BindRootView(RootView::GetInstance());
    RunApp();
    /*
     * 关键: UIViewGroup::Add() 不会触发 Invalidate(OnChildChanged 是空虚函数),
     * 若不主动失效, Show() 时 RootView::Render() 因 invalidateRects_ 为空直接跳过,
     * 窗口 surface 保持全零, wms 以 COPY 模式合成全零(alpha=0)像素,
     * LCD 上透出的是 VO 层的蓝色背景色 —— 这就是"整屏蓝"的根因。
     */
    RootView::GetInstance()->Invalidate();
    printf("[ui_display] main: root invalidated\n");
    window->Show();
    /* 本进程开机即启动, 窗口比 launcher 等应用创建得早, 必须置顶, 否则会被后创建的
     * 全屏窗口(如 com.huawei.launcher)盖住 */
    window->RaiseToTop();
    printf("[ui_display] main: Show+RaiseToTop done, enter task loop\n");

    /* 板上按键接管表情切换(独立扫描线程; 失败只打印, UI 其余功能照常) */
    if (KeyDriverInit(g_keys, KEY_NUM, KeyCallback) == HDF_SUCCESS) {
        printf("[ui_display] keys ready: KEY1=prev emoji, KEY2=next emoji\n");
    } else {
        printf("[ui_display] key driver init failed (keys inactive)\n");
    }

    /* WiFi 连接监控(独立线程; 失败只打印, 月亮保持隐藏) */
    pthread_t wifiTid;
    if (pthread_create(&wifiTid, nullptr, WifiMonitorThread, nullptr) == 0) {
        (void)pthread_detach(wifiTid);
        printf("[ui_display] wifi monitor v2 started (ip+tcp probe %s:%d)\n",
               WIFI_PROBE_IP, WIFI_PROBE_PORT);
    } else {
        printf("[ui_display] wifi monitor thread create failed\n");
    }

    /* 上电清残留字幕: cap.log 落盘持久, 不清会把断电前的旧字幕当新内容显示。
     * 此刻尚无对话(写方每回合开始也整文件重建), 无竞态 */
    {
        FILE *cf = fopen(CAPTION_LOG, "w");
        if (cf != nullptr) {
            (void)fclose(cf);
            printf("[ui_display] caption log cleared on boot\n");
        }
    }
    g_capOff = 0;

    while (true) {
        TaskManager::GetInstance()->TaskHandler();
        if (DrainKeyRequests()) {
            UiDesign_SetEmotionText(EmojiDisplay_CurrentName());
        }
        PollEmojiCmd();
        PollCaption();
        PollWifiState();
        usleep(10 * 1000); /* 10ms 一帧任务调度 */
    }
    return 0;
}
