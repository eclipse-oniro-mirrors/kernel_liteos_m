/*
 * agent_service.c - LLM 智能体编排服务实现
 *
 * 结构(与其它 service 一致): 配置/能力表在本文件顶部 -> 语义接口 -> Cmd 入口。
 * 能力表是唯一来源: 既拼上报 prompt, 又做 [EMOJI]/[ACT] 回复校验,
 * 文档(docs/能力清单与LLM协议.md)与本文同步维护。
 *
 * 协议强化(v4.2):
 *   - [SAY]/[EMOJI]/[ACT]/[END] 四行缺一不可, 没有操作的那行写 none
 *   - 复合指令: 多个动作写多行 [ACT], 按顺序排队执行(后台线程逐个阻塞跑)
 *   - 解析容错: 大小写/全角【】/中英冒号/引号/markdown 装饰均可, 缺行有审计日志
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>

#include "agent_service.h"
#include "app_console.h"    /* ShellParseInt(严格十进制词解析) */
#include "llm_service.h"    /* LlmSvcChat/LlmSvcUtf8FromArgs */
#include "servo_service.h"  /* ServoEnsureInit */
#include "otto_pca.h"       /* OttoPcaRun(阻塞执行, 在动作线程里用) */

/* 【实验开关】1 = 不加能力表模板, 用户文本裸发 LLM(看 agent 裸回复长什么样);
 * 0 = 正常四行模板链路。实验时 ParseReply 仍会照常跑(大概率解析失败),
 * 语音链路 TTS 无 [SAY] 可播 => 机器人不说话, 回复只看串口原始回复横幅。 */
#define AGENT_CFG_RAW_CHAT 0

/* ============【能力表: 加动作/加表情改这里(文档同步改)】============ */

/* 表情带中文含义: prompt 里展示给模型按语义选, 防止无脑默认 happy */
static const struct AgentEmoji {
    const char *name;   /* 表情名(GIF 文件名, ui_display 校验) */
    const char *desc;   /* 中文含义(prompt 内帮助 LLM 按语义选) */
} g_emojiTable[] = {
    { "neutral",      "平静" },
    { "happy",        "开心" },
    { "laughing",     "大笑" },
    { "funny",        "滑稽" },
    { "sad",          "难过" },
    { "angry",        "生气" },
    { "crying",       "哭泣" },
    { "loving",       "喜爱" },
    { "embarrassed",  "尴尬" },
    { "surprised",    "惊讶" },
    { "shocked",      "震惊" },
    { "thinking",     "思考" },
    { "winking",      "调皮" },
    { "cool",         "得意" },
    { "relaxed",      "放松" },
    { "delicious",    "美味" },
    { "kissy",        "飞吻" },
    { "confident",    "自信" },
    { "sleepy",       "困倦" },
    { "silly",        "搞怪" },
    { "confused",     "困惑" },
};
#define EMOJI_N (sizeof(g_emojiTable) / sizeof(g_emojiTable[0]))

static const struct AgentAct {
    const char *name;   /* 动作名(OttoPcaRun 识别) */
    const char *desc;   /* 中文(prompt 内帮助 LLM 选动作) */
} g_actTable[] = {
    { "walk",      "向前走步" },
    { "turn",      "原地转身踏步" },
    { "jump",      "原地蹲站跳" },
    { "bend",      "左右侧弯身" },
    { "shakeleg",  "抬起单腿摇" },
    { "updown",    "身体一起一落" },
    { "swing",     "左右摇摆身体" },
    { "tiptoe",    "踮脚小幅摆动" },
    { "jitter",    "原地抖动" },
    { "ascend",    "螺旋升天转圈" },
    { "crusaito",  "十字舞步(会位移)" },
    { "flapping",  "拍翅膀舞步(会位移)" },
    { "moonwalk",  "太空步滑行" },
    { "whirlwind", "旋风踢腿" },
    { "handsup",   "举起手不归位" },
    { "handsdown", "把手放下" },
    { "handwave",  "原地挥手(脚不动)" },
    { "windmill",  "手臂大风车" },
    { "takeoff",   "起飞扑扇舞步" },
    { "fitness",   "原地健身操" },
    { "greeting",  "转身挥手打招呼(会转身体)" },
    { "shy",       "侧身害羞扭捏" },
    { "sit",       "坐下(保持坐姿不归位)" },
};
#define ACT_N (sizeof(g_actTable) / sizeof(g_actTable[0]))

/* ---- 状态 ---- */
static char g_prompt[AGENT_CFG_PROMPT_MAX]; /* 组包缓冲(网关单条上限 4KB) */
static char g_reply[AGENT_CFG_REPLY_MAX];   /* 回复缓冲 */
static volatile int g_announced;            /* 能力已上报标志 */
static char g_say_text[512];                /* 最近一次 [SAY] 行内容(语音 TTS 播报用) */

/* ============【能力 prompt 组包】============ */

/* 通用: {name,desc} 表 -> "a(含义1),b(含义2),..." */
typedef const char *(*EntryNameFn)(const void *entry);
typedef const char *(*EntryDescFn)(const void *entry);

static void BuildNameDescList(const void *table, size_t n, size_t stride,
                              EntryNameFn nameFn, EntryDescFn descFn,
                              char *dst, size_t size)
{
    size_t i, used = 0;

    dst[0] = '\0';
    for (i = 0; i < n; i++) {
        const char *item0 = nameFn((const char *)table + i * stride);
        const char *item1 = descFn((const char *)table + i * stride);
        char item[64];

        (void)snprintf(item, sizeof(item), "%s%s(%s)", (i > 0) ? "," : "",
                       item0, item1);
        if (used + strlen(item) + 1 >= size) {
            break;
        }
        (void)memcpy(dst + used, item, strlen(item) + 1);
        used += strlen(item);
    }
}

static const char *EmojiName(const void *e)
{
    return ((const struct AgentEmoji *)e)->name;
}

static const char *EmojiDesc(const void *e)
{
    return ((const struct AgentEmoji *)e)->desc;
}

static const char *ActName(const void *e)
{
    return ((const struct AgentAct *)e)->name;
}

static const char *ActDesc(const void *e)
{
    return ((const struct AgentAct *)e)->desc;
}

/* 动作表 -> "walk(前走),turn(转弯),..." */
static void BuildActList(char *dst, size_t size)
{
    BuildNameDescList(g_actTable, ACT_N, sizeof(g_actTable[0]),
                      ActName, ActDesc, dst, size);
}

/* 表情表 -> "neutral(平静),happy(开心),..." */
static void BuildEmojiList(char *dst, size_t size)
{
    BuildNameDescList(g_emojiTable, EMOJI_N, sizeof(g_emojiTable[0]),
                      EmojiName, EmojiDesc, dst, size);
}

/*
 * 组一条完整消息(弱模型格式服从优化, 见 docs/能力清单与LLM协议.md §2):
 *   - 模板前置: 弱模型对 prompt 开头的格式最敏感, 四行模板放最前
 *   - 能力表作"填空数据"跟在模板后
 *   - 核心规则一条: 语义严格对上, 没要求的操作写 none, 禁止补操作/默认兜底
 *   - 示例三条: 第一条就是"只要表情"用例(动作必须 none)
 */
static const char *BuildPrompt(const char *text)
{
    char actList[640];
    char emojiList[768];

    BuildActList(actList, sizeof(actList));
    BuildEmojiList(emojiList, sizeof(emojiList));
    (void)snprintf(g_prompt, sizeof(g_prompt),
        "你是Luna,是Bayes实验室推出的桌面机器人。必须严格按[模板]四行回复, "
        "除这四行禁止输出任何其它内容(包括思考过程、解释、其它字段)。\n"
        "[模板]\n"
        "[SAY] 给用户的中文回复(一行)\n"
        "[EMOJI] 表情名或none\n"
        "[ACT] 动作名或none(要多个动作就写多行[ACT], 按顺序执行)\n"
        "[END]\n"
        "[能力-表情] %s\n"
        "[能力-动作] %s\n"
        "核心规则: 只执行用户明确要求的操作并与语义严格对上: "
        "只要表情就不写动作, 只要动作就不写表情, 都没要就都写none; "
        "禁止补动作/补表情, 禁止默认兜底(没提情绪就none); "
        "名字必须原样用表中英文小写名, 禁止自造或写中文; "
        "反向动作加-1(turn -1=右转, walk -1=后退); "
        "表情按中文含义选最贴合的一个, 不确定就不选。\n"
        "示例-用户: 换个开心的表情\n"
        "[SAY] 好的, 给你一个开心的表情\n"
        "[EMOJI] happy\n"
        "[ACT] none\n"
        "[END]\n"
        "示例-用户: 做个思考的表情, 然后向左转\n"
        "[SAY] 让我想想, 这就向左转\n"
        "[EMOJI] thinking\n"
        "[ACT] turn\n"
        "[END]\n"
        "示例-用户: 你会什么\n"
        "[SAY] 我会21种表情和23个动作, 试试让我开心地走两步!\n"
        "[EMOJI] none\n"
        "[ACT] none\n"
        "[END]\n"
        "用户说: %s",
        emojiList, actList, (text != NULL) ? text : "");
    return g_prompt;
}

/* ============【解析与分发】============ */

/* 行首装饰清理: 跳过空白、星号、减号、反引号(防 markdown 修饰) */
static const char *LineTrim(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '*' || *s == '-' || *s == '`') {
        s++;
    }
    return s;
}

/* 内容前缀清理: 跳过空白/中英冒号/引号 */
static const char *ContentTrim(const char *s)
{
    for (;;) {
        if (*s == ' ' || *s == '\t' || *s == ':' || *s == '"') {
            s++;
            continue;
        }
        if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBC &&
            (unsigned char)s[2] == 0x9A) { /* 全角冒号 */
            s += 3;
            continue;
        }
        break;
    }
    return s;
}

/*
 * 行首是否为指定标记(大小写不敏感, 兼容半角[]与全角【】)。
 * 是则返回标记后的内容指针, 否则返回 NULL。
 * 带误匹配保护: 标记名后必须是括号收尾或分隔符, 如 "[SAYX]" 不会匹配 SAY。
 */
static const char *TagContent(const char *s, const char *tag)
{
    size_t n = strlen(tag);

    if (s[0] == '[') {
        if (strncasecmp(s + 1, tag, n) != 0) {
            return NULL;
        }
        s += 1 + n;
    } else if ((unsigned char)s[0] == 0xE3 && (unsigned char)s[1] == 0x80 &&
               (unsigned char)s[2] == 0x90) { /* 全角【 */
        if (strncasecmp(s + 3, tag, n) != 0) {
            return NULL;
        }
        s += 3 + n;
    } else {
        return NULL;
    }
    /* 标记名后只允许: 收括号/空白/冒号/行尾, 否则视为误匹配 */
    if (*s == ']' || *s == '\0' || *s == ' ' || *s == '\t' || *s == ':') {
        if (*s == ']') {
            s++;
        }
    } else if ((unsigned char)s[0] == 0xE3 && (unsigned char)s[1] == 0x80 &&
               (unsigned char)s[2] == 0x91) { /* 全角】 */
        s += 3;
    } else {
        return NULL;
    }
    return ContentTrim(s);
}

/* 从 s 取首个空白分隔的词(表情/动作名); 引号也算分隔 */
static void FirstToken(const char *s, char *out, size_t size)
{
    size_t used = 0;

    while (*s == ' ' || *s == '\t' || *s == '"') {
        s++;
    }
    while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '"' &&
           used + 1 < size) {
        out[used++] = *s++;
    }
    out[used] = '\0';
}

/*
 * turn/walk 语义参数适配:
 * prompt 教模型用 "turn -1"=右转 / "walk -1"=后退, ±1 是方向;
 * 而底层签名是 <steps> <periodMs> <dir> <arm>, 参数从左填,
 * 裸 ±1 会落到 steps 上(=-1 步 -> 空转直接返回"完成")。
 * 这里把首参 ±1 识别为方向, 按底层签名展开成 步数+周期+方向。
 */
static void NormalizeAct(const char *name, int *par, int *npar)
{
    int dir;

    if (strcmp(name, "turn") != 0 && strcmp(name, "walk") != 0) {
        return;
    }
    if (*npar < 1 || (par[0] != 1 && par[0] != -1)) {
        return;
    }
    dir = par[0];
    if (*npar >= 2) {
        /* "turn -1 N": N>0 视为步数, 周期用默认 */
        par[0] = (par[1] > 0) ? par[1] : 4;
        par[1] = 1000;
        par[2] = dir;
        *npar = 3;
    } else {
        /* "turn -1": 全默认 步数4 周期1000 方向±1 */
        par[0] = 4;
        par[1] = 1000;
        par[2] = dir;
        *npar = 3;
    }
}

static int EmojiValid(const char *name)
{
    size_t i;

    for (i = 0; i < EMOJI_N; i++) {
        if (strcmp(g_emojiTable[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const struct AgentAct *ActFind(const char *name)
{
    size_t i;

    for (i = 0; i < ACT_N; i++) {
        if (strcmp(g_actTable[i].name, name) == 0) {
            return &g_actTable[i];
        }
    }
    return NULL;
}

/* 表情命令写文件 -> ui_display 主循环轮询(协议见 docs/能力清单与LLM协议.md §4) */
static void SendEmojiCmd(const char *name)
{
    FILE *f = fopen(AGENT_CFG_EMOJI_CMD_FILE, "w");

    if (f == NULL) {
        printf("[agent] 表情命令写失败(%s)\r\n", AGENT_CFG_EMOJI_CMD_FILE);
        return;
    }
    (void)fprintf(f, "%s\n", name);
    (void)fclose(f);
    printf("[agent] 表情 -> %s\r\n", name);
}

/* ---- 动作执行队列: 后台线程逐个阻塞执行(模式同 key_service 的动作线程),
 * 多个 [ACT] 严格按回复顺序执行; 串口 stop 可中止当前动作 ---- */

static struct ActReq {
    char name[AGENT_CFG_NAME_MAX];
    int par[AGENT_CFG_ACT_ARG_MAX];
    int npar;
    int epoch;     /* 入队时的停止世代号; 与当前不符 = 已被急停作废 */
} g_actQ[AGENT_CFG_ACTQ_SIZE];
static volatile int g_qHead;   /* 消费位 */
static volatile int g_qTail;   /* 生产位(与 head 相等 = 队列空) */
static volatile int g_workerOn;
static volatile int g_stopEpoch;   /* 急停世代号: StopAll 时 +1, 作废在途动作 */
/* 生产者互斥: 串口 chat 线程与语音 FC 线程都会入队(两个生产者),
 * 消费者只有动作线程一个, 头指针无需加锁 */
static pthread_mutex_t g_actQMtx = PTHREAD_MUTEX_INITIALIZER;

static void *ActWorkerLoop(void *arg)
{
    (void)arg;
    for (;;) {
        struct ActReq *req;
        int ret;

        /* 出队加锁: 与 StopAll 的清队列互斥, 避免清空瞬间半出队状态 */
        (void)pthread_mutex_lock(&g_actQMtx);
        if (g_qHead == g_qTail) {
            (void)pthread_mutex_unlock(&g_actQMtx);
            usleep(50 * 1000);
            continue;
        }
        req = &g_actQ[g_qHead];
        g_qHead = (g_qHead + 1) % AGENT_CFG_ACTQ_SIZE;
        (void)pthread_mutex_unlock(&g_actQMtx);
        /* 世代号不符 = 急停后刚出队/清空边缘漏网的在途动作。
         * 必须在 OttoPcaRun 之前丢弃 —— OttoPcaRun 启动会清 g_stopReq,
         * 仅靠停止标志挡不住它(SDK 原始行为, 不可改) */
        if (req->epoch != g_stopEpoch) {
            printf("[agent] 动作 %s 已丢弃(急停作废)\r\n", req->name);
            continue;
        }
        if (ServoEnsureInit() != 0) {
            printf("[agent] PCA 未就绪(先 scan 排查接线), %s 取消\r\n", req->name);
            continue;
        }
        ret = OttoPcaRun(req->name, (req->npar > 0) ? req->par : NULL, req->npar);
        if (ret == 0) {
            printf("[agent] 动作 %s 完成(已归位)\r\n", req->name);
        } else if (ret == -1) {
            printf("[agent] 动作 %s 被中止(已归位)\r\n", req->name);
        } else {
            printf("[agent] 动作 %s 异常 ret=%d\r\n", req->name, (int)ret);
        }
    }
    return NULL;
}

/* 动作入队(非阻塞); 首次使用时懒创建动作线程 */
static void EnqueueAct(const char *name, const int *par, int npar)
{
    pthread_attr_t attr;
    pthread_t tid;
    struct ActReq *req;
    int next, i;

    (void)pthread_mutex_lock(&g_actQMtx);
    if (g_workerOn == 0) {
        (void)pthread_attr_init(&attr);
        (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, ActWorkerLoop, NULL) != 0) {
            (void)pthread_attr_destroy(&attr);
            (void)pthread_mutex_unlock(&g_actQMtx);
            printf("[agent] 动作线程创建失败\r\n");
            return;
        }
        (void)pthread_attr_destroy(&attr);
        g_workerOn = 1;
    }
    next = (g_qTail + 1) % AGENT_CFG_ACTQ_SIZE;
    if (next == g_qHead) {
        (void)pthread_mutex_unlock(&g_actQMtx);
        printf("[agent] 动作队列已满(%d), 丢弃: %s\r\n", AGENT_CFG_ACTQ_SIZE, name);
        return;
    }
    req = &g_actQ[g_qTail];
    (void)memset(req, 0, sizeof(*req));
    req->epoch = g_stopEpoch;
    for (i = 0; name[i] != '\0' && i < AGENT_CFG_NAME_MAX - 1; i++) {
        req->name[i] = name[i];
    }
    req->name[i] = '\0';
    for (i = 0; i < npar && i < AGENT_CFG_ACT_ARG_MAX; i++) {
        req->par[i] = par[i];
    }
    req->npar = (npar > AGENT_CFG_ACT_ARG_MAX) ? AGENT_CFG_ACT_ARG_MAX : npar;
    g_qTail = next;
    if (req->npar > 0) {
        printf("[agent] 动作 -> %s", name);
        for (i = 0; i < req->npar; i++) {
            printf(" %d", req->par[i]);
        }
        printf(" (已排队)\r\n");
    } else {
        printf("[agent] 动作 -> %s (已排队)\r\n", name);
    }
    (void)pthread_mutex_unlock(&g_actQMtx);
}

/* 解析 [ACT] 内容: 动作名 + 最多 ACT_ARG_MAX 个整数参数(非法词跳过) */
static void ParseActLine(const char *rest, char *name, size_t size,
                         int *par, int *npar)
{
    char word[32];
    size_t used = 0;
    const char *s = rest;

    *npar = 0;
    s = LineTrim(s);
    while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '"' &&
           used + 1 < size) {
        name[used++] = *s++;
    }
    name[used] = '\0';
    for (;;) {
        int32_t val;

        while (*s == ' ' || *s == '\t' || *s == '"') {
            s++;
        }
        if (*s == '\0' || *npar >= AGENT_CFG_ACT_ARG_MAX) {
            break;
        }
        used = 0;
        while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '"' &&
               used + 1 < sizeof(word)) {
            word[used++] = *s++;
        }
        word[used] = '\0';
        if (ShellParseInt(word, &val) == 0) {
            par[(*npar)++] = (int)val;
        }
    }
}

/*
 * 扫回复逐行解析分发。
 * 返回 0=完整格式(见到[END]) 1=有标记但缺[END](已审计提示) -1=无任何标记。
 * 协议要求四行齐全: 缺的行逐个打审计日志(无操作的行应写 none)。
 */
static int ParseReply(const char *reply)
{
    const char *p = reply;
    const char *c;
    char line[256];
    char name[AGENT_CFG_NAME_MAX];
    int sawEnd = 0, sawTag = 0, sawSay = 0, sawEmoji = 0, sawAct = 0;

    while (*p != '\0' && !sawEnd) {
        size_t used = 0;

        while (*p != '\0' && *p != '\n' && used + 1 < sizeof(line)) {
            line[used++] = *p++;
        }
        line[used] = '\0';
        if (*p == '\n') {
            p++;
        }
        c = LineTrim(line);
        if (c[0] == '\0') {
            continue;
        }
        if (TagContent(c, "END") != NULL) {
            sawEnd = 1;
            sawTag = 1;
            continue;
        }
        c = TagContent(c, "SAY");
        if (c != NULL) {
            if (c[0] != '\0') {
                printf("[agent] 回复: %s\r\n", c);
                (void)snprintf(g_say_text, sizeof(g_say_text), "%s", c);
            }
            sawSay = 1;
            sawTag = 1;
            continue;
        }
        c = TagContent(LineTrim(line), "EMOJI");
        if (c != NULL) {
            FirstToken(c, name, sizeof(name));
            sawEmoji = 1;
            sawTag = 1;
            if (name[0] == '\0' || strcmp(name, "none") == 0) {
                continue;
            }
            if (!EmojiValid(name)) {
                printf("[agent] 未知表情已忽略: %s\r\n", name);
                continue;
            }
            SendEmojiCmd(name);
            continue;
        }
        c = TagContent(LineTrim(line), "ACT");
        if (c != NULL) {
            int par[AGENT_CFG_ACT_ARG_MAX];
            int npar = 0;

            ParseActLine(c, name, sizeof(name), par, &npar);
            sawAct = 1;
            sawTag = 1;
            if (name[0] == '\0' || strcmp(name, "none") == 0) {
                continue;
            }
            if (ActFind(name) == NULL) {
                printf("[agent] 未知动作已忽略: %s\r\n", name);
                continue;
            }
            NormalizeAct(name, par, &npar);
            EnqueueAct(name, par, npar);
            continue;
        }
        /* 其余行忽略(LLM 偶发的多余说明文字/代码围栏) */
    }
    if (!sawTag) {
        if (reply[0] != '\0') {
            printf("[agent] !! 回复未含任何标记(格式违规), 原文见 [llm] 日志\r\n");
        }
        return -1;
    }
    /* 缺行审计(四行缺一不可) */
    if (!sawSay) {
        printf("[agent] !! 缺 [SAY] 行\r\n");
    }
    if (!sawEmoji) {
        printf("[agent] !! 缺 [EMOJI] 行(无操作应写 none)\r\n");
    }
    if (!sawAct) {
        printf("[agent] !! 缺 [ACT] 行(无操作应写 none)\r\n");
    }
    if (!sawEnd) {
        printf("[agent] !! 缺 [END] 行, 已按已解析内容执行\r\n");
    }
    return sawEnd ? 0 : 1;
}

/* "expression:"/"表情:" 式 前缀词 -> 值(大小写不敏感, 冒号/引号容错) */
static const char *WordValue(const char *s, const char *word)
{
    size_t n = strlen(word);

    if (strncasecmp(s, word, n) != 0) {
        return NULL;
    }
    s = ContentTrim(s + n);
    return (*s != '\0') ? s : NULL;
}

/* 兜底行首清理: 在 LineTrim 基础上再跳过序号(如 "1. 表情: happy") */
static const char *SalvTrim(const char *s)
{
    s = LineTrim(s);
    while ((*s >= '0' && *s <= '9') || *s == '.' || *s == ')') {
        s++;
    }
    /* 全角括号）与。也当序号分隔清掉 */
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBC &&
        ((unsigned char)s[2] == 0x88 || (unsigned char)s[2] == 0x89)) {
        s += 3;
    } else if ((unsigned char)s[0] == 0xE3 && (unsigned char)s[1] == 0x80 &&
               (unsigned char)s[2] == 0x82) {
        s += 3;
    }
    return LineTrim(s);
}

/* 表情字段前缀: expression/emotion/emoji/表情/表情 -> 值或 NULL */
static const char *SalvageEmojiVal(const char *c)
{
    const char *v = WordValue(c, "expression");

    if (v == NULL) {
        v = WordValue(c, "emotion");
    }
    if (v == NULL) {
        v = WordValue(c, "emoji");
    }
    if (v == NULL) {
        v = WordValue(c, "\xE8\xA1\xA8\xE6\x83\x85");        /* 表情 */
    }
    if (v == NULL) {
        v = WordValue(c, "\xE8\xA1\xA8\xE6\x83\x85"); /* 表情 */
    }
    return v;
}

/* 动作字段前缀: action/act/动作/动作 -> 值或 NULL(先长词后短词防误配) */
static const char *SalvageActVal(const char *c)
{
    const char *v = WordValue(c, "action");

    if (v == NULL) {
        v = WordValue(c, "act");
    }
    if (v == NULL) {
        v = WordValue(c, "\xE5\x8A\xA8\xE4\xBD\x9C");        /* 动作 */
    }
    if (v == NULL) {
        v = WordValue(c, "\xE5\x8B\x95\xE4\xBD\x9C"); /* 动作 */
    }
    return v;
}

/* 文本字段前缀: say/回复/想法 -> 值或 NULL(兜底时把回复也带出来) */
static const char *SalvageSayVal(const char *c)
{
    const char *v = WordValue(c, "say");

    if (v == NULL) {
        v = WordValue(c, "\xE5\x9B\x9E\xE5\xA4\x8D");        /* 回复 */
    }
    if (v == NULL) {
        v = WordValue(c, "\xE6\x83\xB3\xE6\xB3\x95"); /* 想法 */
    }
    return v;
}

/*
 * 格式漂移兜底: 模型没按四行标记输出时, 二次扫描抢救。
 * 认得 "expression:/emotion:/emoji:/表情:/表情:" -> 表情,
 *      "action:/act:/动作:/动作:" -> 动作(可多行, 按顺序排队),
 *      "say:/回复:/想法:" -> 文本; 行首序号/装饰均可。
 * 多条同类型按出现顺序执行(表情以最后一条为准)。
 * 返回 1=救回至少一条 0=完全无法解析。
 */
static int SalvageReply(const char *reply)
{
    const char *p = reply;
    char line[256];
    char name[AGENT_CFG_NAME_MAX];
    int hit = 0;

    while (*p != '\0') {
        const char *c;
        const char *v;
        size_t used = 0;

        while (*p != '\0' && *p != '\n' && used + 1 < sizeof(line)) {
            line[used++] = *p++;
        }
        line[used] = '\0';
        if (*p == '\n') {
            p++;
        }
        c = SalvTrim(line);
        if (c[0] == '\0') {
            continue;
        }
        v = SalvageEmojiVal(c);
        if (v != NULL) {
            FirstToken(v, name, sizeof(name));
            if (name[0] != '\0' && EmojiValid(name)) {
                SendEmojiCmd(name);
                hit = 1;
            }
            continue;
        }
        v = SalvageActVal(c);
        if (v != NULL) {
            int par[AGENT_CFG_ACT_ARG_MAX];
            int npar = 0;

            ParseActLine(v, name, sizeof(name), par, &npar);
            if (name[0] != '\0' && ActFind(name) != NULL) {
                NormalizeAct(name, par, &npar);
                EnqueueAct(name, par, npar);
                hit = 1;
            }
            continue;
        }
        v = SalvageSayVal(c);
        if (v != NULL && v[0] != '\0') {
            printf("[agent] 回复: %s\r\n", v);
            (void)snprintf(g_say_text, sizeof(g_say_text), "%s", v);
            hit = 1;
            continue;
        }
    }
    return hit;
}

/* ============【语义接口】============ */

/*
 * 对话核心(串口 chat 与语音 agent 段共用):
 *   BuildPrompt -> LlmSvcChat -> ParseReply(缺行走 SalvageReply 兜底)。
 *   raw_out/raw_cap 非 NULL 时拷出 LLM 原始回复(未解析);
 *   say_out/say_cap 非 NULL 时拷出 [SAY] 行内容(无则空串)。
 * 返回 >=0 回复长度, <0 失败。
 */
static int AgentSvcChatCore(const char *text, char *raw_out, int raw_cap,
                            char *say_out, int say_cap)
{
    int n;

    if (raw_out != NULL && raw_cap > 0) {
        raw_out[0] = '\0';
    }
    if (say_out != NULL && say_cap > 0) {
        say_out[0] = '\0';
    }
    g_say_text[0] = '\0';
    if (text == NULL || text[0] == '\0') {
        printf("[agent] 用法: chat <文字...> / chatu <码点...>\r\n");
        return -1;
    }
    printf("[agent] >> %s\r\n", text);
#if AGENT_CFG_RAW_CHAT
    /* 实验: 能力表模板(下方 BuildPrompt)不启用, 裸文本直发 LLM。
     * 原代码: n = LlmSvcChat(BuildPrompt(text), g_reply, sizeof(g_reply)); */
    n = LlmSvcChat(text, g_reply, sizeof(g_reply));
#else
    n = LlmSvcChat(BuildPrompt(text), g_reply, sizeof(g_reply));
#endif
    if (n < 0) {
        printf("[agent] LLM 失败(%d), 详见 [llm] 日志\r\n", n);
        return n;
    }
    if (raw_out != NULL && raw_cap > 0) {
        (void)snprintf(raw_out, (size_t)raw_cap, "%s", g_reply);
    }
    if (ParseReply(g_reply) < 0) {
        if (SalvageReply(g_reply) > 0) {
            printf("[agent] 已按兜底格式救回指令\r\n");
        } else {
            printf("[agent] 无可用指令, 可重试\r\n");
        }
    }
    if (say_out != NULL && say_cap > 0) {
        (void)snprintf(say_out, (size_t)say_cap, "%s", g_say_text);
    }
    return n;
}

int AgentSvcChat(const char *text)
{
    return AgentSvcChatCore(text, NULL, 0, NULL, 0);
}

int AgentSvcChatVoice(const char *text, char *raw_out, int raw_cap,
                      char *say_out, int say_cap)
{
    return AgentSvcChatCore(text, raw_out, raw_cap, say_out, say_cap);
}

int AgentSvcAnnounce(void)
{
    int n;

    if (g_announced) {
        return 0;
    }
    printf("[agent] 首次连云: 上报能力清单(动作 %d 个 + 表情 %d 个)...\r\n",
           (int)ACT_N, (int)EMOJI_N);
    n = LlmSvcChat(BuildPrompt(
        "以上是本机器人的全部能力和回复格式, 请记住。"
        "现在按格式回复: [SAY]一行简短打招呼, [EMOJI]none, [ACT]none, [END]。"),
        g_reply, sizeof(g_reply));
    if (n < 0) {
        printf("[agent] 上报失败(对话时能力清单仍会随每次请求内嵌, 不影响功能)\r\n");
        return n;
    }
    (void)ParseReply(g_reply);
    g_announced = 1;
    printf("[agent] 能力上报完成\r\n");
    return 0;
}

/* ============【语音分发接口(voice_dialog 的云端指令路径)】============ */

/*
 * 云端(阿里云 FC)回来的表情指令: 与串口 [EMOJI] 行同一处理 ——
 * 能力表校验 + 写 /storage/data/emoji_cmd 文件通道(ui_display 轮询消费)。
 * 返回 0=已执行, -1=名字非法已忽略。
 */
int AgentSvcDispatchEmoji(const char *name)
{
    if (name == NULL || name[0] == '\0' || !EmojiValid(name)) {
        printf("[agent] 未知表情已忽略: %s\r\n", (name != NULL) ? name : "(null)");
        return -1;
    }
    SendEmojiCmd(name);
    return 0;
}

/*
 * 云端(阿里云 FC)回来的动作指令: 与串口 [ACT] 行同一处理 ——
 * 能力表校验 + NormalizeAct 参数桥接(±1=方向) + 动作队列(后台线程阻塞执行)。
 * 返回 0=已入队, -1=名字非法已忽略。
 */
int AgentSvcDispatchAct(const char *name, const int *par, int npar)
{
    int p[AGENT_CFG_ACT_ARG_MAX];
    int np = 0;
    int i;

    if (name == NULL || name[0] == '\0' || ActFind(name) == NULL) {
        printf("[agent] 未知动作已忽略: %s\r\n", (name != NULL) ? name : "(null)");
        return -1;
    }
    for (i = 0; i < npar && i < AGENT_CFG_ACT_ARG_MAX; i++) {
        p[np++] = par[i];
    }
    NormalizeAct(name, p, &np);
    EnqueueAct(name, p, np);
    return 0;
}

/*
 * agent 工具桥专用原始入队: 参数已由调用方按 otto_pca.h Ex 形参序构造完整,
 * 不再过 NormalizeAct(±1 启发式会把 steps=1 误判成方向); 名字不在能力表也
 * 放行(OttoPcaRun 底层还有校验), 这样 home/showcase/calisthenics 等表外
 * SDK 动作同样可达。返回 0=已入队, -1=名字非法已忽略。
 */
int AgentSvcEnqueueRaw(const char *name, const int *par, int npar)
{
    int p[AGENT_CFG_ACT_ARG_MAX];
    int np = 0;
    int i;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    if (par != NULL) {
        for (i = 0; i < npar && i < AGENT_CFG_ACT_ARG_MAX; i++) {
            p[np++] = par[i];
        }
    }
    EnqueueAct(name, p, np);
    return 0;
}

/*
 * 完整急停: 先清空动作队列(还没开始的动作全部丢弃), 再 ServoCmdStop 中止
 * 当前正在执行的动作并归位。单用 ServoCmdStop 只能停"正在跑"的那一个,
 * 队列里排队的动作会接着跑 —— LLM 连发多个动作时"停下"必须全清。
 * 返回 0=已处理。
 */
int AgentSvcStopAll(void)
{
    (void)pthread_mutex_lock(&g_actQMtx);
    if (g_qTail != g_qHead) {
        g_qTail = g_qHead;
        printf("[agent] 动作队列已清空\r\n");
    }
    g_stopEpoch++;   /* 世代 +1: 已出队未执行的动作一并作废 */
    (void)pthread_mutex_unlock(&g_actQMtx);
    return ServoCmdStop(0, NULL);
}

/* 能力名清单(逗号分隔): 语音层拼阿里云工具枚举用, 能力表仍是唯一来源 */
int AgentSvcEmojiNames(char *dst, int size)
{
    size_t i;
    int used = 0;

    if (dst == NULL || size <= 0) {
        return -1;
    }
    dst[0] = '\0';
    for (i = 0; i < EMOJI_N; i++) {
        int n = snprintf(dst + used, (size_t)(size - used), "%s%s",
                         (i > 0) ? "," : "", g_emojiTable[i].name);
        if (n < 0 || used + n >= size) {
            break;
        }
        used += n;
    }
    return used;
}

int AgentSvcActNames(char *dst, int size)
{
    size_t i;
    int used = 0;

    if (dst == NULL || size <= 0) {
        return -1;
    }
    dst[0] = '\0';
    for (i = 0; i < ACT_N; i++) {
        int n = snprintf(dst + used, (size_t)(size - used), "%s%s",
                         (i > 0) ? "," : "", g_actTable[i].name);
        if (n < 0 || used + n >= size) {
            break;
        }
        used += n;
    }
    return used;
}

/* ============【shell 指令】============ */

/* 拼参数为一句文字(同 LlmCmdAsk 做法) */
int AgentCmdChat(int argc, char **argv)
{
    char text[1024];
    size_t used = 0;
    int i;

    if (argc < 2) {
        printf("[agent] 用法: chat <文字...>  中文可用: chatu <码点...>\r\n");
        return 0;
    }
    text[0] = '\0';
    for (i = 1; i < argc; i++) {
        size_t len = strlen(argv[i]);

        if (used + len + 2 >= sizeof(text)) {
            break;
        }
        if (i > 1) {
            text[used++] = ' ';
        }
        (void)memcpy(text + used, argv[i], len);
        used += len;
        text[used] = '\0';
    }
    (void)AgentSvcChat(text);
    return 0;
}

/* 中文对话: 码点转 UTF-8 后走同一条管线 */
int AgentCmdChatU(int argc, char **argv)
{
    char text[1024];

    if (argc < 2) {
        printf("[agent] 用法: chatu <码点...>  例: chatu 4f60 597d 5f00 5fc3\r\n");
        return 0;
    }
    if (LlmSvcUtf8FromArgs(argc, argv, text, sizeof(text)) != 0) {
        printf("[agent] 码点解析失败\r\n");
        return 0;
    }
    (void)AgentSvcChat(text);
    return 0;
}
