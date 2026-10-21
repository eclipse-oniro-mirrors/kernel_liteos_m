# ui_display_sdk — 配套屏 UI（表情 GIF 库 + 触摸/按键交互）用户态应用

HiSpark AI Camera（Hi3516DV300）配套 960x480 横屏的 UI 程序：21 个标准表情
GIF 播放 + 触摸计数/换表情按钮 + 板载按键切表情。纯用户态，基于 OpenHarmony
lite 图形栈（ui_lite + wms_client + freetype），开机自启（init.cfg 已配）。

> 来源：交付包_UIDISPLAY_v1/SDK库包 全量拷贝至本目录，在包版基础上增加
> 板载按键接管表情切换（key1=上一个，key2=下一个，21 个循环回绕），
> 键事件来自 applications/key_sdk（app_test 的按键服务自启已同步停用）。

## 四层结构（逐层解耦，改哪层都不碰别层）

| 层 | 文件 | 职责 |
|---|---|---|
| GIF 解码 | gif_decoder.c/.h | 纯 C 解码器，零 UI 依赖（可单独复用，host_test.c 为 PC 自测程序） |
| 表情库 | emoji_display.cpp/.h | 调函数即可显示/切换/缩放表情，自持视图（移植自 xiaozhi-esp32 表情架构） |
| UI 设计 | ui_design.cpp/.h | 布局/配色/字号/控件摆放（改界面长什么样只动这里的常量区） |
| main 组装 | ui_display.cpp | 启动图形/建窗口/组装 UI/绑事件/板载按键接入/任务循环 |

## 门面 API（表情库，emoji_display.h）

| 函数 | 作用 |
|---|---|
| `EmojiDisplay_Init(gifDir)` | 一键初始化（自建视图，幂等）；gifDir 如 "/storage/data/emoji" |
| `EmojiDisplay_GetView()` | 取表情视图（上层负责挂到视图树） |
| `EmojiDisplay_SetEmotion("happy")` | 按名显示表情（21 个：angry/confident/confused/cool/crying/delicious/embarrassed/funny/happy/kissy/laughing/loving/neutral/relaxed/sad/shocked/silly/sleepy/surprised/thinking/winking） |
| `EmojiDisplay_Next()` | 循环切换下一个 |
| `EmojiDisplay_Prev()` | 循环切换上一个（未加载时从最后一个开始，同样回绕） |
| `EmojiDisplay_SetPosition(x, y)` | 随时移动 |
| `EmojiDisplay_SetScale(percent)` | 缩放 10~400（100=原始尺寸，视图尺寸自动跟随） |
| `EmojiDisplay_CurrentName()` | 当前表情名 |
| `EmojiDisplay_GetCount() / Name(i)` | 注册表查询（新增表情 = EMOJI_TABLE 加一行 + 放 GIF） |

进阶：`GifEmojiView`（LoadGif/StopGif/SetScale，可多实例播 GIF）、
`EmojiLibrary`（自管映射表）。

## UI 设计层（ui_design.h 设计常量区，改这里调界面）

```cpp
SCREEN_W/H = 960x480          /* 屏幕分辨率(HDF 层负责面板旋转) */
EMOJI_X/Y  = 60/100           /* 表情位置: 改这两行即移动 */
EMOJI_SIZE = 240, EMOJI_SCALE = 100   /* 占位框/缩放 */
FONT_TITLE/INFO/COUNT = 32/28/26      /* 字号 */
```
`UiDesign_Build(root)` 建整棵视图树；`UiDesign_GetTouchButton()/GetNextButton()`
给 main 层绑事件；`UiDesign_SetEmotionText()/SetCountText()` 更新文字。

## 当前示例行为（烧录即生效，开机自启）

- 标题栏 "HiSpark AI Camera" + 表情区播放 neutral + 右侧当前表情名
- 点"触摸屏幕试试"按钮 → 计数标签变化（Touch x1、x2...）
- 点"切换表情"按钮 → 表情区下一个 GIF，表情名跟随
- 板载 key1 → 上一个表情，板载 key2 → 下一个表情（21 个循环，表情名跟随）
- 串口 `chat <文字...>`（app_test 智能体）→ LLM 回复 `[EMOJI] <名>` →
  经 `/storage/data/emoji_cmd` 文件通道跨进程切换表情（每 200ms 轮询，读到即删）
- 串口打印：`[ui_display] main: start` → `window created` → `Show+RaiseToTop done`
  → `keys ready: KEY1=prev emoji, KEY2=next emoji`

## 编译（独立脚本，不依赖源码树 GN）

```bash
cd 交付包_UIDISPLAY_v1/SDK库包
bash build_ui_display.sh [OH源码根目录]    # 默认 /code/3516
```

- 旗标：`--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall`
- 头文件 7 个 -I：ui_lite 的 frameworks/interfaces/kits/interfaces/innerkits、
  graphic_utils_lite 的 interfaces/kits/interfaces/innerkits(+frameworks)、
  freetype/include、bounds_checking_function/include（securec.h）
- 链接：`-lui -lwms_client -lfreetype -lgraphic_utils_lite -lgraphic_hals -lsurface -lhilog_shared -lsec_shared -lc++ -lpthread`
- 产物 NEEDED（实测与树内 GN 产物完全一致）：libc++/libc/libfreetype/
  libgraphic_hals/libgraphic_utils_lite/libhilog_shared/libsec_shared/libsurface/libui/libwms_client

## 运行前提（镜像已全部配好，自装环境才需要看）

1. **wms_server 必须先起**（init.cfg 已自启；foundation/samgr 加载 24 个共享库
   需数秒，main 里窗口创建不设重试上限，每 100ms 重试到成功）
2. **字体**：userfs `/storage/data/SourceHanSansSC-Regular.otf`（ui_lite 默认
   VECTOR_FONT_DIR=/user/data/ 本产品不存在，main 已改指 /storage/data/）
3. **表情**：userfs `/storage/data/emoji/*.gif`（21 个）；GIF 缺失时 UI 其余
   部分照常（表情区不显示并打提示）
4. 中文断行规则 `/storage/data/line_cj.brk`（ENABLE_ICU 时用）
5. **板载按键**：GPIO 事件只能被一个进程消费——本程序已接管（key1=prev/key2=next），
   app_test 的 `KeySvcStart()` 自启必须保持停用（见 app_test/main/app_test.c）

## 注意事项（板上实测坑）

1. **整屏蓝 = 视图树没 Invalidate**：UIViewGroup::Add() 不触发重绘，
   Show() 前必须 `RootView::GetInstance()->Invalidate()`（本包已含，勿删——
   这是"整屏蓝"的根因：surface 全零被 wms 以 alpha=0 合成，透出 VO 蓝底）
2. **开机屏幕黑/程序等窗口**：wms_server+foundation 就绪需数秒，属正常
   （main 每 3s 打一条 retry 日志）；如果永远起不来，查 init.cfg 的
   wms_server 行、串口看 `Liteipc ServiceManager not set` 是否一直刷
3. **窗口被盖住**：本进程窗口比 launcher 创建得早，必须
   `window->RaiseToTop()`（本包已含）；改成自己的程序记得保留
4. **表情区空白**：userfs 没烧 → GIF 缺失，程序故意不崩、其余 UI 正常
5. **中文不显示**：字体不在 /storage/data/（换字体同时改 FONT_DIR 常量）
6. 触摸无反应：屏排线/触摸方向（HDF 旋转）问题，与本程序无关；
   串口 `cat /dev/input/event0` 按屏看有无数据可快速判界
7. PC 端调试 GIF 解码：`gcc host_test.c gif_decoder.c -o host_test` 跑
   （纯 C，无 UI 依赖）
8. **按键互斥**：app_test 按键服务恢复自启或手动运行 key_demo 时，会与本程序
   同时扫描同一对 GPIO → 一次按键重复响应。二选一使用

## 文件清单

| 文件 | 用途 |
|---|---|
| ui_display.cpp | main 组装层源码（含板载按键接入：key1=prev/key2=next） |
| ui_design.cpp/.h | UI 设计层源码 |
| emoji_display.cpp/.h | 表情库源码 |
| gif_decoder.c/.h | GIF 解码器源码 |
| host_test.c | PC 端解码自测 |
| docs/*.md | 移植教程与静态资源全链路指南（树内文档原样保留） |
| assets/emoji/*.gif | 21 个表情（userfs data/emoji 用） |
| assets/font/ | 字体 + 断行规则（userfs data/ 用） |
| ui_display | 预编译板端程序 |
| *.o | 预编译目标文件 |
| build_ui_display.sh | 独立交叉编译脚本 |
| BUILD.gn / bundle.json | 源码树集成文件（含 key_sdk 依赖；整机走 `python3 build.py ipcamera_hispark_taurus`） |
