/**
 * @file ui_design.h
 * @brief UI 设计层(独立文件): 统一管理布局常量、样式、控件创建与文字更新。
 *
 * 职责边界:
 *   - 本文件只负责"长什么样": 屏幕布局/配色/字号/控件摆放
 *   - 行为(按钮点了干什么)由 main 层绑定事件回调, 与本文件解耦
 *   - 表情视图来自表情库(emoji_display), 本层只负责其摆放位置
 *
 * 调整界面:
 *   - 改布局/配色/字号 -> 只改 design 常量区
 *   - 移动表情包       -> 只改 EMOJI_X / EMOJI_Y / EMOJI_SIZE
 */
#ifndef UI_DESIGN_H
#define UI_DESIGN_H

#include <stdint.h>

#include "components/ui_label.h"
#include "components/ui_label_button.h"
#include "components/ui_view_group.h"

namespace OHOS {
namespace design {
/* ==================== 设计常量区(改这里即可调整界面) ==================== */

/* 屏幕分辨率: 本产品系统按 960x480 横屏运行(HDF 层负责面板旋转) */
constexpr int16_t SCREEN_W = 960;
constexpr int16_t SCREEN_H = 480;

/* 全屏背景(静态 PNG): 资源源文件在 vendor/hisilicon/hispark_taurus/data/background/,
 * 经 BUILD.gn bg_assets 打包进 userfs data/bg/。图必须是 960x480 原生尺寸
 * (解码一次走 CacheManager 缓存, 静态图层不再逐帧重采样, 动画流畅度由表情区独享)。
 * 原动态 GIF 背景(GifEmojiView)方案已弃用, 见 git 历史 */
constexpr int16_t BG_X = 0;
constexpr int16_t BG_Y = 0;
constexpr int16_t BG_W = SCREEN_W;
constexpr int16_t BG_H = SCREEN_H;
constexpr const char *BG_PATH = "/storage/data/bg/background.png";

/* WiFi 状态图标(右上角): wlan0 链路 UP+RUNNING 且拿到 IP 才显示, 断开/未连隐藏。
 * 源图 vendor/hisilicon/hispark_taurus/data/background/wifi_connect.png(142x141,
 * 原 wifi_connect.jpeg 实为 WebP 已转 PNG), 打包进 userfs data/bg/ */
constexpr const char *WIFI_ICON_PATH = "/storage/data/bg/wifi_connect.png";
constexpr int16_t WIFI_ICON_W = 142;
constexpr int16_t WIFI_ICON_H = 141;
constexpr int16_t WIFI_ICON_MARGIN = 8;
constexpr int16_t WIFI_ICON_X = SCREEN_W - WIFI_ICON_W - WIFI_ICON_MARGIN;
constexpr int16_t WIFI_ICON_Y = WIFI_ICON_MARGIN;

/* 表情包位置: 只需改这几行即可移动/缩放表情(960x480 偏上: 下方留字幕框) */
constexpr int16_t EMOJI_X = 360;
constexpr int16_t EMOJI_Y = 30;
constexpr int16_t EMOJI_SIZE = 240;  /* 占位框(GIF 源尺寸) */
constexpr uint32_t EMOJI_SCALE = 100; /* 缩放百分比: 100 原始, 50 减半, 200 放大一倍 */

/* 字幕气泡区(表情正下方, 居中): 参考 xiaozhi-esp32 的聊天气泡流
 * (lcd_display.cc SetChatMessage: 气泡宽 = min(文本自然宽, 85% 屏宽),
 * 文本按宽度 WRAP 折行, 容器自动滚到底部=始终露出最后几行)。
 * ui_lite 无滚动容器, 等效实现: 宽 85% 屏、按像素宽折行、只显示
 * 最后 CAPTION_MAX_LINES 行(顶行随揭示推进上移滚出=自动滚底)。
 * 文本仍按"已播音频占比"逐字揭示(语音同步跟读)。深色半透明底, 白字 */
constexpr int16_t CAPTION_X = 72;    /* (960-816)/2 水平居中 */
constexpr int16_t CAPTION_Y = 330;   /* emoji 底 270, 留 60px 间隙 */
constexpr int16_t CAPTION_W = 816;   /* 85% 屏宽(xiaozhi 气泡最大宽) */
constexpr int16_t CAPTION_H = 96;    /* 两行行高 */
constexpr uint8_t CAPTION_FONT = 28;
constexpr const char *CAPTION_FONT_NAME = "SourceHanSansSC-Regular";
constexpr int CAPTION_MAX_LINES = 2;      /* 只露最后 2 行(等效自动滚底) */
constexpr int CAPTION_LINE_BYTES = 120;   /* 单行折行模拟的字节上限(防测量异常) */
constexpr int16_t CAPTION_PIXEL_MARGIN = 16; /* 折行判定的安全边距 */

/* ==================== 设计常量区结束 ==================== */
} // namespace design

/* 构建整棵视图树(标题栏/表情视图/标签/按钮), 不绑定任何事件;
 * 成功返回 true。表情视图来自 EmojiDisplay_Init(), 未初始化时跳过表情区。 */
bool UiDesign_Build(UIViewGroup *root);

/* 控件访问: main 层用它绑定事件(与设计解耦) */
UILabel *UiDesign_GetTitle(void);
UILabelButton *UiDesign_GetTouchButton(void);
UILabelButton *UiDesign_GetNextButton(void);

/* 文字更新(标签指针保持私有, 上层不接触具体控件) */
void UiDesign_SetEmotionText(const char *name);
void UiDesign_SetCountText(const char *text);

/* 字幕文本: 空串=清空并隐藏字幕框; 非空=显示并刷新 */
void UiDesign_SetCaption(const char *text);

/* 字幕多行窗口(xiaozhi 气泡流等效): 显示 prefix[0..shownBytes) 的
 * 最后 CAPTION_MAX_LINES 行; 行界用隐藏 ADAPT 标签按像素宽二分模拟
 * 折行得到, 行间以 '\n' 硬换行喂给显示标签(折行引擎支持) */
void UiDesign_SetCaptionWindow(const char *prefix, int shownBytes);

/* WiFi 状态图标显隐: connected=true 显示右上角月亮, false 隐藏。
 * 状态由 ui_display 的 WiFi 监控线程维护, 本函数只做视图显隐 */
void UiDesign_SetWifiConnected(bool connected);
} // namespace OHOS

#endif /* UI_DESIGN_H */
