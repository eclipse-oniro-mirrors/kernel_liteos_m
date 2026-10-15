# emoji_display 表情包库 — 移植教程与 API 手册

> HiSpark AI Camera (Hi3516DV300) · OpenHarmony 小型系统 (LiteOS-A) · ui_lite 图形栈
> 表情库移植自 xiaozhi-esp32 表情架构(21 个标准 GIF 表情), 解码器为自研纯 C 实现, 无第三方依赖。
>
> **本文档是什么**: 本目录(emoji_display 表情包模块)的移植教程、API 参考手册与二次开发指南。

---

## 1. 文件总览(每个文件是什么)

| 文件 | 语言 | 职责 | 可移植性 |
|---|---|---|---|
| `gif_decoder.h/.c` | C | **纯 GIF89a 解码器**: LZW 解码/隔行重排/透明色/帧间 disposal/循环播放。输出 BGRA 画布(对齐 ui_lite `ARGB8888`)。 | 任意平台(只依赖 stdio/stdlib/string) |
| `emoji_display.h/.cpp` | C++ | **表情包库**(本模块核心): 解码器封装成"调用函数即显示表情"的 API, 自带视图与动画调度、最近邻缩放、表情映射表。 | 任意 ui_lite 平台 |
| `ui_design.h/.cpp` | C++ | **UI 设计层**: 布局常量/配色/字号/控件创建全部集中于此。改界面只动这两个文件。 | 按产品自定义 |
| `ui_display.cpp` | C++ | **main 组装层**: 图形引擎启动、窗口创建(带重试)、把设计层 UI 挂到 RootView、绑定按钮事件与板载按键(行为)、任务循环。 | 按产品自定义 |
| `host_test.c` | C | **宿主机回归测试**(非必须文件, 见第 4.3 节): 在 PC 上 `gcc` 直接编译运行, 验证解码器正确性。不参与设备构建, 删除与否不影响功能。 | 任意平台 |
| `BUILD.gn` | - | 构建脚本: 源文件列表 + GIF 资产打包(userfs data 分区)。 | 按产品自定义 |

**依赖关系(严格单向, 低耦合):**

```
main(ui_display.cpp) ──> ui_design ──> emoji_display ──> gif_decoder
        │                    │
        └──> emoji_display(行为: 切换表情)   └─> ui_lite (UIView/Animator/ImageInfo)
```

**GIF 资产**(本目录 assets/emoji/, 打包链路):
```
applications/ui_display_sdk/assets/emoji/*.gif   (21 个 240x240 GIF89a, 源自 xiaozhi)
    └─ BUILD.gn copy("emoji_assets") ─> out/.../data/emoji/ ─> userfs 分区 ─> 设备 /storage/data/emoji/
```

---

## 2. 快速上手(3 行代码显示表情)

```cpp
#include "emoji_display.h"

EmojiDisplay_Init("/storage/data/emoji");  // 1. 初始化(自建视图, 幂等)
EmojiDisplay_SetEmotion("happy");          // 2. 显示指定表情
root->Add(EmojiDisplay_GetView());         // 3. 把视图挂到视图树
```

位置/缩放(可随时调用, 立即生效):

```cpp
EmojiDisplay_SetPosition(60, 100);  // 屏幕坐标
EmojiDisplay_SetScale(200);         // 缩放百分比: 100 原始, 50 减半, 200 放大一倍
```

---

## 3. API 手册

### 3.1 gif_decoder(纯 C, 无 UI 依赖)

画布像素内存布局: 每像素 4 字节 `[B, G, R, A]`, 与 ui_lite `ImageInfo::ARGB8888` 一致。

```c
GifDecoder *GifLoadFile(const char *path);        /* 打开+解析(头/全局色表/帧索引); 失败返回 NULL */
int   GifDecodeNextFrame(GifDecoder *d);          /* 解码下一帧并合成到画布; 返回该帧延时(ms), 无帧返回 -1 */
void  GifRewind(GifDecoder *d);                   /* 倒回第一帧并清空画布(循环播放用) */
const uint8_t *GifGetCanvas(const GifDecoder *d); /* 画布指针: width*height*4 字节 [B,G,R,A] */
int16_t GifGetWidth(const GifDecoder *d);         /* 画布宽 */
int16_t GifGetHeight(const GifDecoder *d);        /* 画布高 */
int   GifGetFrameCount(const GifDecoder *d);      /* 总帧数 */
void  GifClose(GifDecoder *d);                    /* 关闭并释放全部内存 */
```

解码器约束: 文件 ≤ 4MB; 最多 1024 帧; LZW min code size 1~8; 支持 GIF87a/89a、局部色表、隔行、透明色、disposal 0~3、NETSCAPE 循环扩展。

### 3.2 emoji_display 门面 API(推荐, 单实例)

```cpp
/* ---- 生命周期 ---- */
bool    EmojiDisplay_Init(const char *gifDir);  /* 初始化(自建视图); gifDir 须长期有效; 重复调用幂等 */
UIView *EmojiDisplay_GetView(void);             /* 取视图挂到视图树; 未初始化返回 nullptr */

/* ---- 布局与缩放 ---- */
void EmojiDisplay_SetPosition(int16_t x, int16_t y);   /* 屏幕坐标 */
bool EmojiDisplay_SetScale(uint32_t percent);           /* 100=原始; 钳制 10~400; 视图尺寸自动同步 */

/* ---- 播放控制 ---- */
bool EmojiDisplay_SetEmotion(const char *name);         /* 按名显示, 如 "happy"; 成功返回 true */
bool EmojiDisplay_SetEmotionByIndex(int index);         /* 按序号显示(自动回绕) */
bool EmojiDisplay_Next(void);                           /* 循环切换下一个 */
bool EmojiDisplay_Prev(void);                           /* 循环切换上一个(未加载时从最后一个开始, 同样回绕) */
const char *EmojiDisplay_CurrentName(void);             /* 当前表情名; 未加载为 nullptr */

/* ---- 注册表查询 ---- */
int  EmojiDisplay_GetCount(void);                       /* 表情总数(由映射表自动推导) */
const char *EmojiDisplay_Name(int index);               /* 第 index 个表情名(越界返回 nullptr) */
```

行为说明:
- GIF 加载成功后自动开始循环播放(Animator 10ms tick + 每帧自身延时), 帧延时钳制 ≥ 40ms 防烧 CPU。
- `SetScale` 播放中调用立即生效; 100% 时零拷贝直用解码画布, 非 100% 内部维护缩放缓存(最近邻采样)。
- GIF 文件缺失时 `SetEmotion` 返回 false 并打印 `[emoji] load failed`, 其余 UI 不受影响(容错)。

### 3.3 emoji_display 类(高级, 可多实例)

```cpp
class GifEmojiView : public UIImageView, public AnimatorCallback {
    bool LoadGif(const char *path);      /* 加载+播放; 视图尺寸自动同步(含缩放) */
    void StopGif();                      /* 停止并释放解码器 */
    void SetScale(uint32_t percent);     /* 缩放(最近邻), 播放中生效 */
    void Callback(UIView *view) override;/* Animator 回调: 渲染下一帧 */
};

class EmojiLibrary {
    struct EmojiEntry { const char *name; const char *file; };  /* name=API 用名; file=nullptr 表示 <name>.gif */
    static const EmojiEntry EMOJI_TABLE[];   /* 表情映射表(见 emoji_display.cpp 顶部) */
    static const int EMOJI_COUNT;            /* 由表 sizeof 自动推导 */
    bool Init(GifEmojiView *view, const char *dir);
    bool SetEmotion(const char *name);
    bool SetEmotionByIndex(int index);
    bool SwitchNext();
    bool SwitchPrev();                                   /* 与 SwitchNext 一样自动回绕 */
    const char *CurrentEmotion() const;
    const char *EmotionName(int index) const;
};
```

### 3.4 ui_design API(界面统一设计)

布局常量(`ui_design.h` 的 `namespace design`, **改界面只改这里**):

| 常量 | 含义 | 默认值 |
|---|---|---|
| `SCREEN_W / SCREEN_H` | 屏幕分辨率(横屏) | 960 / 480 |
| `TITLE_X/Y/W/H` | 标题栏 | 0,0,960,70 |
| `EMOJI_X / EMOJI_Y` | **表情位置** | 60, 100 |
| `EMOJI_SIZE` | 表情占位框(GIF 源尺寸) | 240 |
| `EMOJI_SCALE` | **表情缩放百分比** | 100 |
| `RIGHT_X / RIGHT_W` | 右侧信息区 | 340 / 560 |
| `BTN_Y / BTN_W / BTN_H`、`TOUCH_BTN_X / NEXT_BTN_X` | 按钮 | 200, 220, 70, 340, 590 |
| `FONT_TITLE / FONT_INFO / FONT_COUNT` | 字号 | 32 / 28 / 26 |

```cpp
bool UiDesign_Build(UIViewGroup *root);          /* 构建整棵视图树(不绑事件) */
UILabel       *UiDesign_GetTitle(void);          /* 控件访问: main 绑事件用 */
UILabelButton *UiDesign_GetTouchButton(void);
UILabelButton *UiDesign_GetNextButton(void);
void UiDesign_SetEmotionText(const char *name);  /* 文字更新(标签指针私有) */
void UiDesign_SetCountText(const char *text);
```

---

## 4. 移植教程

### 4.1 移植到其他 OpenHarmony 轻量系统(ui_lite)设备

表情库本体只依赖 ui_lite, 与 Hi3516 板级代码零耦合。最小移植 = 4 个文件 + 资产:

1. **拷贝核心库**(4 个文件): `gif_decoder.h` `gif_decoder.c` `emoji_display.h` `emoji_display.cpp`
2. **加入构建**(BUILD.gn):
   ```gn
   executable("your_app") {
     sources = [ "gif_decoder.c", "emoji_display.cpp", "your_app.cpp" ]
     include_dirs = [ "//foundation/arkui/ui_lite/frameworks" ]
     deps = [ "//foundation/arkui/ui_lite:ui_lite" ]   # wms_client 等按你的窗口方案添加
   }
   ```
3. **打包 GIF 资产**: 把 `*.gif` 放进你的 userfs data 源目录(参考本目录 BUILD.gn 的 `copy("emoji_assets")` 写法, 逐文件列出 → `$root_out_dir/data/emoji/`), 设备上路径 = `/<userfs挂载点>/data/emoji/`。
4. **代码集成**(参考 `ui_display.cpp` 的 `RunApp()`):
   ```cpp
   EmojiDisplay_Init("/storage/data/emoji");        // 路径按你的 userfs 挂载点
   UiDesign_Build(root);                            // 或 root->Add(EmojiDisplay_GetView())
   EmojiDisplay_SetEmotion("neutral");
   ```
5. **必须注意的两个坑**(本产品踩过的):
   - **`UIViewGroup::Add()` 不触发重绘**(OnChildChanged 是空虚函数)。首次显示前必须 `RootView::GetInstance()->Invalidate()`, 否则窗口 surface 全零、LCD 透出 VO 背景色(整屏蓝/黑)。
   - **窗口竞态**: wms_server 由 init 异步拉起, 就绪前 `Window::CreateWindow` 失败, 需循环重试(见 `ui_display.cpp` main); 开机早于 launcher 的进程还需 `window->RaiseToTop()` 防被盖。

### 4.2 移植到非 ui_lite 平台(如 LVGL)

- `gif_decoder.*` 原样可用(纯 C)。
- 参照 `GifEmojiView` 的模式写一层适配(约 100 行): **解码帧 → BGRA 画布 →(可选缩放)→ 目标平台图像控件刷新**, 帧节奏算法直接抄 `Callback()`(`GetRunTime` 差值驱动 + 每帧延时钳制)。
- 映射表 `EMOJI_TABLE` 原样保留, 只替换"画"的那一层。

### 4.3 宿主机回归测试(可选, 不参与设备构建)

`host_test.c` 是**开发/调试辅助文件, 不是必须文件**: `BUILD.gn` 未引用它, 删除后编译、烧录、运行完全不受影响。保留它的价值是在 PC 上快速验证解码器, 不用上板:

```bash
gcc -O2 -o giftest gif_decoder.c host_test.c   # host_test.c 自带 main(), 与解码器一起编译
./giftest /path/to/xxx.gif                      # 逐帧解码, 输出画布统计与 PASS/FAIL
```

适用场景:
- **改了 `gif_decoder.c` 之后**: 先在宿主机跑一遍全部 GIF 资产, 10 秒内发现解码回归(历史上位缓冲溢出 bug 就是靠它定位的, 避免了上板盲调);
- **换了新的 GIF 素材后**: 上板前先确认素材可被解码、帧数/尺寸符合预期;
- **跨平台移植时**: 目标平台编译链未就绪, 先用宿主机验证解码层。

测试程序会校验: 每帧解码返回值 ≥ 0、不透明像素数符合预期、BGRA/alpha 字节正确。全部通过输出 `PASS`。

---

## 5. 如何构建自己的显示函数

### 5.1 场景 A: 业务代码里切换表情(最常用)

直接调用门面, 不需要碰任何视图细节:

```cpp
void OnVoiceCommand(const char *cmd)
{
    if (strcmp(cmd, "hello") == 0) {
        EmojiDisplay_SetEmotion("happy");
    } else if (strcmp(cmd, "error") == 0) {
        EmojiDisplay_SetEmotion("sad");
    }
    UiDesign_SetEmotionText(EmojiDisplay_CurrentName());  // 同步右栏文字
}
```

> **注意**: 上面这种直接调用的写法, 仅当调用者与 `TaskManager::TaskHandler()` 在**同一个任务/线程**时才安全。业务代码跑在**其他任务**时, 禁止直接调任何 UI 函数(含本模块全部 API), 请改用第 6 节的消息队列方式。

**新增表情**(两步, 不改任何代码逻辑):
1. `emoji_display.cpp` 顶部 `EMOJI_TABLE` 加一行: `{"新表情名", nullptr}`(或 `{"别名", "已有文件.gif"}` 复用素材);
2. 把对应 GIF 放进资产目录并重编(资产打包 + userfs)。

### 5.2 场景 B: 加自己的 UI 控件(设计层扩展)

`ui_design.cpp` 已内置工厂函数模式, 照抄即可:

```cpp
/* ui_design.h 常量区加坐标 */
constexpr int16_t MY_BTN_X = 100;
constexpr int16_t MY_BTN_Y = 400;

/* ui_design.cpp: 用工厂创建并加入视图树 */
g_myBtn = MakeButton(MY_BTN_X, MY_BTN_Y, design::BTN_W, design::BTN_H,
                     "My Button", design::FONT_INFO, Color::Blue().full,
                     DEFAULT_VECTOR_FONT_FILENAME);
root->Add(g_myBtn);

/* 提供 getter 供 main 绑事件 */
UILabelButton *UiDesign_GetMyButton(void) { return g_myBtn; }
```

```cpp
/* main(ui_display.cpp): 行为与外观分离, 事件绑定都在这里 */
class MyListener : public UIView::OnClickListener {
    bool OnClick(UIView &view, const ClickEvent &event) override
    {
        /* 你自己的行为 */
        return true;
    }
};
static MyListener g_myListener;
UiDesign_GetMyButton()->SetOnClickListener(&g_myListener);
```

### 5.3 场景 C: 新的动态显示类型(仿 GifEmojiView 模式)

任何"逐帧刷新的自绘内容"(如 PNG 序列帧、摄像头画面)都按同一模式:

```cpp
class MyAnimatedView : public UIImageView, public AnimatorCallback {
    ImageInfo imgInfo_{};
    uint8_t *frameBuf_ = nullptr;   /* 你的帧缓冲, BGRA 4 字节/像素 */
    Animator *anim_ = nullptr;

    /* 1. 准备 ImageInfo */
    void SetupImage(int w, int h)
    {
        imgInfo_.header.width  = w;
        imgInfo_.header.height = h;
        imgInfo_.header.colorMode = ARGB8888;
        imgInfo_.dataSize = (uint32_t)w * h * 4;
        imgInfo_.data = frameBuf_;
        Resize(w, h);          /* 视图尺寸同步 */
        SetSrc(&imgInfo_);     /* 末尾自带 Invalidate, 触发重绘 */
    }

    /* 2. Animator 每 tick 填充新帧 */
    void Callback(UIView *view) override
    {
        /* 把新一帧内容写进 frameBuf_, 然后 SetSrc 触发重绘 */
        SetSrc(&imgInfo_);
    }
};
/* 3. 启动: anim_ = new Animator(this, this, 10, true); anim_->Start(); */
```

要点: 像素布局必须是 `[B, G, R, A]`(小端 ARGB8888); 不透明像素 A=0xFF; 每次 `SetSrc(&imgInfo_)` 结尾自动失效重绘, 无需手动 Invalidate。

---

## 6. 跨任务更新 UI(其他任务切换表情)

### 6.1 铁律: UI 函数只能在 UI 线程调用

ui_lite 的视图树/TaskManager/Animator 是**单线程模型**, 所有 UI 调用(含本模块全部 API 与 `UiDesign_Set*`)必须收敛在跑 `TaskManager::TaskHandler()` 的那个任务 —— 即 `ui_display.cpp` main 末尾的 10ms 循环:

```cpp
while (true) {
    TaskManager::GetInstance()->TaskHandler();  /* 触摸分发+动画帧+脏区重绘都在这里 */
    usleep(10 * 1000);
}
```

其他任务(音频/网络/业务逻辑)**直接调用** `EmojiDisplay_SetEmotion()` 属于跨线程访问视图树, 会产生竞态, 典型现象: 偶发花屏、渲染错乱、崩溃 —— 且是概率性问题, 极难复现调试。

同时澄清两个"阻塞"问题:

- 动画播放只占 UI 任务自己的时间片(TaskHandler 干完活就 `usleep` 让出 CPU), **不会阻塞其他任务**;
- 其他任务也不阻塞 UI —— 前提是业务逻辑不写进 UI 主循环任务里(长阻塞会卡动画)。

### 6.2 推荐架构: 消息队列中转

```
业务任务 A ─┐
业务任务 B ─┼─ EmojiMsg_Send("happy") ─> [环形队列 + 互斥锁] ─> UI 线程每帧顺路 Recv
(IPC 线程) ─┘       永不阻塞, 满队丢旧                            │
                                                   EmojiDisplay_SetEmotion(name)
                                                   UiDesign_SetEmotionText(...)
```

设计要点:

- **业务侧只投递不等待**: `Send` 永不阻塞, 队列满丢最旧(表情切换只关心最新状态);
- **UI 侧零新增阻塞原语**: 主循环本来就 10ms 轮询, 每帧顺路取件即可, 不需要信号量/条件变量;
- **执行永远在 UI 线程**: 队列里只存"表情名"字符串, 真正调 UI 函数的动作由主循环完成。

### 6.3 参考实现(两个新文件, 可直接抄)

`emoji_msg.h`:

```c
/**
 * @file emoji_msg.h
 * @brief 跨任务表情请求队列(默认单例): 业务线程 Send -> UI 线程 Recv 后执行。
 *
 * 纯 C, 不依赖任何 UI 头文件。业务线程只投递表情名, 永不阻塞;
 * 真正调 EmojiDisplay_* 的动作由 UI 线程(main 的 TaskHandler 循环)完成。
 * 需要多个队列时, 以本文件为模板自建(队列结构见 emoji_msg.c)。
 */
#ifndef EMOJI_MSG_H
#define EMOJI_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMOJI_MSG_NAME_MAX 24  /* 表情名缓冲上限(含 '\0') */

/* 必须在创建任何业务线程之前调用一次(初始化内部互斥锁) */
void EmojiMsg_InitDefault(void);

/* 业务线程调用: 投递表情切换请求。永不阻塞; 队列满丢最旧; 名字过长截断。 */
void EmojiMsg_Send(const char *emotionName);

/* UI 线程调用: 弹出一条请求到 outName(容量 >= EMOJI_MSG_NAME_MAX)。
 * 取到返回 1, 队列空返回 0。 */
int EmojiMsg_Recv(char *outName);

#ifdef __cplusplus
}
#endif

#endif /* EMOJI_MSG_H */
```

`emoji_msg.c`:

```c
#include "emoji_msg.h"

#include <pthread.h>
#include <string.h>

#define EMOJI_MSG_DEPTH 8 /* 队列深度: 满时丢最旧(表情切换只关心最新状态) */

typedef struct {
    pthread_mutex_t lock;
    char names[EMOJI_MSG_DEPTH][EMOJI_MSG_NAME_MAX];
    int head;  /* 队头读位置 */
    int count; /* 现存条数 */
} EmojiMsgQueue;

static EmojiMsgQueue g_defaultQueue;

void EmojiMsg_InitDefault(void)
{
    g_defaultQueue.head = 0;
    g_defaultQueue.count = 0;
    pthread_mutex_init(&g_defaultQueue.lock, NULL);
}

void EmojiMsg_Send(const char *emotionName)
{
    char buf[EMOJI_MSG_NAME_MAX];
    if (emotionName == NULL) {
        return;
    }
    strncpy(buf, emotionName, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    pthread_mutex_lock(&g_defaultQueue.lock);
    if (g_defaultQueue.count == EMOJI_MSG_DEPTH) { /* 满: 丢最旧 */
        g_defaultQueue.head = (g_defaultQueue.head + 1) % EMOJI_MSG_DEPTH;
        g_defaultQueue.count--;
    }
    (void)strcpy(g_defaultQueue.names[(g_defaultQueue.head + g_defaultQueue.count) % EMOJI_MSG_DEPTH], buf);
    g_defaultQueue.count++;
    pthread_mutex_unlock(&g_defaultQueue.lock);
}

int EmojiMsg_Recv(char *outName)
{
    int has = 0;
    if (outName == NULL) {
        return 0;
    }
    pthread_mutex_lock(&g_defaultQueue.lock);
    if (g_defaultQueue.count > 0) {
        (void)strcpy(outName, g_defaultQueue.names[g_defaultQueue.head]);
        g_defaultQueue.head = (g_defaultQueue.head + 1) % EMOJI_MSG_DEPTH;
        g_defaultQueue.count--;
        has = 1;
    }
    pthread_mutex_unlock(&g_defaultQueue.lock);
    return has;
}
```

接入三步:

1. `BUILD.gn` 的 `executable("ui_display")` sources 里加一行 `"emoji_msg.c"`(pthread 已在 ldflags 链接, 无需改);
2. `ui_display.cpp` 头部 `#include "emoji_msg.h"`, 并在 `main()` **开头(创建任何业务线程之前)** 调用 `EmojiMsg_InitDefault();`
3. 主循环改造(去重集中在这里做, 业务侧可无脑 Send):

```cpp
while (true) {
    TaskManager::GetInstance()->TaskHandler();
    /* 消费跨任务表情请求: 只有这里(UI 线程)调 EmojiDisplay_* 才安全 */
    char name[EMOJI_MSG_NAME_MAX];
    while (EmojiMsg_Recv(name)) {
        const char *cur = EmojiDisplay_CurrentName();
        if (cur != nullptr && strcmp(cur, name) == 0) {
            continue; /* 同名跳过: SetEmotion 对同名也会整个重新 LoadGif(文件 IO+解码), 会卡顿 */
        }
        if (EmojiDisplay_SetEmotion(name)) {
            UiDesign_SetEmotionText(EmojiDisplay_CurrentName());
        }
    }
    usleep(10 * 1000);
}
```

业务任务里(任意线程, 一行):

```cpp
#include "emoji_msg.h"

void OnVoiceCommand(const char *cmd)
{
    if (strcmp(cmd, "hello") == 0) {
        EmojiMsg_Send("happy");
    } else if (strcmp(cmd, "error") == 0) {
        EmojiMsg_Send("sad");
    }
}
```

注意事项:

- **启动顺序**: `EmojiMsg_InitDefault()` 必须先于所有业务线程创建, 否则 Send/Recv 会撞上未初始化的锁;
- **去重集中在 UI 线程**: `SetEmotion` 对同名表情也会重新加载 GIF(StopGif + 文件 IO + 解码), 业务侧无需判断, 统一在 Recv 循环里去重;
- **表情名须已注册**: 未在 `EMOJI_TABLE` 注册的名字会在 UI 线程执行时打印 `[emoji] unknown emotion` 并丢弃(容错, 不影响 UI);
- **仅限线程上下文**: 不要在中断上下文调用(用户态应用一般无此场景);
- **扩展其他命令**(SetScale/SetPosition/改文字): 把队列元素从"表情名"换成带 `type` 字段的小结构体, 在同一处 Recv 循环里分发即可。

### 6.4 跨进程怎么办

若业务逻辑在**另一个进程**(不同地址空间), 上面的进程内队列不可见。模式不变, 只把"入队"换成 IPC:

| 方案 | 说明 |
|---|---|
| AF_UNIX 本地 socket | 最简单: UI 进程开一个监听线程, recv 后调 `EmojiMsg_Send`, 之后路径与 6.3 完全复用 |
| POSIX 消息队列(mq_open) | LiteOS-A 支持, 无需额外线程 |
| LiteIPC / samgr 服务 | OHOS 正统服务间通信, 适合已有服务框架的项目 |

**关键不变量**: 无论哪种通道, IPC 接收线程也只做 `Send`, 调 `EmojiDisplay_*` 的永远只有跑 `TaskHandler()` 的 UI 线程。

> **现成参考**: 本工程 `ui_display.cpp` 的板载按键处理就是这套模式的轻量实现——
> key_sdk 扫描线程回调只做 `g_keyPrevReq++/g_keyNextReq++`(计数标志),
> 主循环 `TaskHandler()` 之后统一消费(Prev/Next + 刷新表情名标签), 零锁零队列。

---

## 7. 已知限制与调试

| 事项 | 说明 |
|---|---|
| GIF 上限 | 文件 ≤ 4MB, ≤ 1024 帧 |
| 缩放范围 | 10%~400%(最近邻, 放大有锯齿属预期) |
| 帧延时 | GIF 0 延时帧钳制为 100ms, 其余下限 40ms |
| 内存 | 解码画布 230KB/GIF(240x240x4) + 缩放缓存(非 100% 时) |
| 串口日志 | `[emoji] loaded <path>: WxH, N frames` / `[emoji] load failed` / `[ui_display] RunApp: ...` breadcrumbs |
| 触摸报错 | `ERROR: MMAP ADDR: 0x...` 为良性(TDE 硬件 blit 校验失败后软件兜底), 可忽略 |
| 烧录 | 表情资源在 **userfs** 分区: 只刷 rootfs 不刷 userfs 会 `[emoji] load failed`; 内核与资源无关 |

## 8. 版本信息

- 表情素材: xiaozhi-esp32 `otto-emoji-gif-component`(21 个 240x240 GIF89a)
- 解码器: 自研 `gif_decoder`(参考 xiaozhi lvgl gifdec 架构重写), 2026-09 修复位缓冲溢出(>32 位丢失导致解码失败)后全量回归通过(21 GIF × 全帧)
- 帧节奏: 与 ui_lite `GifImageAnimator` 同款算法; 缩放: 最近邻采样
