/**
 * @file ui_design.cpp
 * @brief UI 设计层实现: 控件创建与样式, 布局常量见 ui_design.h。
 */
#include "ui_design.h"

#include <stdio.h>
#include <unistd.h>

#include "components/ui_image_view.h"
#include "emoji_display.h"
#include "gfx_utils/color.h"

namespace OHOS {
namespace {
/* 控件指针(保持私有; 界面 = 静态背景+表情+字幕+WiFi月亮图标) */
UILabel *g_title = nullptr;
UILabel *g_emotionLabel = nullptr;
UILabel *g_countLabel = nullptr;
UILabel *g_captionLabel = nullptr;
UILabel *g_captionMeasure = nullptr; /* 隐藏测量标签: ADAPT 模式测自然单行宽 */
UILabelButton *g_touchBtn = nullptr;
UILabelButton *g_nextBtn = nullptr;
UIImageView *g_bgView = nullptr;
UIImageView *g_wifiIcon = nullptr;
}

bool UiDesign_Build(UIViewGroup *root)
{
    if (root == nullptr) {
        return false;
    }

    /* 底层: 全屏静态背景 PNG(先 Add 的视图在最底层; 解码一次走缓存)。
     * 文件缺失时打印告警并退回 root 底色, 其余功能不受影响 */
    if (access(design::BG_PATH, F_OK) != 0) {
        printf("[ui_design] background not found at %s (keep solid bg)\n", design::BG_PATH);
    } else {
        g_bgView = new UIImageView();
        g_bgView->SetSrc(design::BG_PATH);
        g_bgView->SetPosition(design::BG_X, design::BG_Y, design::BG_W, design::BG_H);
        root->Add(g_bgView);
    }

    /* 中层: 动态表情(透明底, 透出背景图; 位置在 design::EMOJI_X/Y) */
    UIView *emoji = EmojiDisplay_GetView();
    if (emoji != nullptr) {
        EmojiDisplay_SetScale(design::EMOJI_SCALE);
        emoji->SetPosition(design::EMOJI_X, design::EMOJI_Y, design::EMOJI_SIZE, design::EMOJI_SIZE);
        emoji->SetStyle(STYLE_BACKGROUND_OPA, OPA_TRANSPARENT);
        root->Add(emoji);
    }

    /* 中下: 字幕气泡区(xiaozhi 气泡流等效, 默认隐藏)。宽 85% 屏、
     * 按像素宽折行、只显示最后两行; 文本由 ui_display 按"已播音频
     * 占比"逐字揭示, 每 tick 传入当前应显示的揭示尾部 */
    g_captionLabel = new UILabel();
    g_captionLabel->SetPosition(design::CAPTION_X, design::CAPTION_Y,
                                design::CAPTION_W, design::CAPTION_H);
    g_captionLabel->SetLineBreakMode(UILabel::LINE_BREAK_CLIP);
    g_captionLabel->SetAlign(TEXT_ALIGNMENT_LEFT, TEXT_ALIGNMENT_CENTER);
    g_captionLabel->SetStyle(STYLE_BACKGROUND_COLOR, Color::Black().full);
    g_captionLabel->SetStyle(STYLE_BACKGROUND_OPA, OPA_OPAQUE * 40 / 100);
    g_captionLabel->SetStyle(STYLE_TEXT_COLOR, Color::White().full);
    g_captionLabel->SetFont(design::CAPTION_FONT_NAME, design::CAPTION_FONT);
    g_captionLabel->SetText("");
    g_captionLabel->SetVisible(false);
    root->Add(g_captionLabel);

    /* 隐藏测量标签(同字体, LINE_BREAK_ADAPT 不换行): CLIP 模式下
     * GetTextWidth 测的是换行后的宽度(永远不超框宽), 无法判定溢出;
     * ADAPT 模式 expandWidth=true, 返回自然单行宽度, 用于折行模拟。
     * 注: 曾尝试把条高压成一行行高来物理禁二行, 实测导致字幕整体
     * 不显示, 已回退(高度保持设计常量) */
    g_captionMeasure = new UILabel();
    g_captionMeasure->SetLineBreakMode(UILabel::LINE_BREAK_ADAPT);
    g_captionMeasure->SetFont(design::CAPTION_FONT_NAME, design::CAPTION_FONT);
    g_captionMeasure->SetText("");
    g_captionMeasure->SetVisible(false);
    root->Add(g_captionMeasure);

    /* 顶层: WiFi 状态月亮(右上角, 默认隐藏; 监控线程查到连接才显示) */
    if (access(design::WIFI_ICON_PATH, F_OK) == 0) {
        g_wifiIcon = new UIImageView();
        g_wifiIcon->SetSrc(design::WIFI_ICON_PATH);
        g_wifiIcon->SetPosition(design::WIFI_ICON_X, design::WIFI_ICON_Y,
                                design::WIFI_ICON_W, design::WIFI_ICON_H);
        g_wifiIcon->SetVisible(false);
        root->Add(g_wifiIcon);
    } else {
        printf("[ui_design] wifi icon not found at %s (feature off)\n", design::WIFI_ICON_PATH);
    }

    return true;
}

UILabel *UiDesign_GetTitle(void)
{
    return g_title;
}

UILabelButton *UiDesign_GetTouchButton(void)
{
    return g_touchBtn;
}

UILabelButton *UiDesign_GetNextButton(void)
{
    return g_nextBtn;
}

void UiDesign_SetEmotionText(const char *name)
{
    if (g_emotionLabel != nullptr && name != nullptr) {
        g_emotionLabel->SetText(name);
    }
}

void UiDesign_SetCountText(const char *text)
{
    if (g_countLabel != nullptr && text != nullptr) {
        g_countLabel->SetText(text);
    }
}

void UiDesign_SetCaption(const char *text)
{
    if (g_captionLabel == nullptr || text == nullptr) {
        return;
    }
    if (text[0] == '\0') {
        /* 空文本: 清空并隐藏(不占屏) */
        if (g_captionLabel->IsVisible()) {
            g_captionLabel->SetVisible(false);
            g_captionLabel->Invalidate();
        }
        return;
    }
    g_captionLabel->SetText(text);
    if (!g_captionLabel->IsVisible()) {
        g_captionLabel->SetVisible(true);
    }
    g_captionLabel->Invalidate();
}

void UiDesign_SetCaptionWindow(const char *prefix, int shownBytes)
{
    static char mbuf[design::CAPTION_LINE_BYTES + 4]; /* 折行测量用单行串 */
    static char win[2 * (design::CAPTION_LINE_BYTES + 2)]; /* 最后两行 + '\n' */
    int textLen, shown, maxW, ls, cnt, pos, lineA_s, lineA_e, lineB_s, lineB_e;

    if (g_captionLabel == nullptr || prefix == nullptr) {
        return;
    }
    textLen = (int)strlen(prefix);
    if (shownBytes <= 0 || textLen <= 0) {
        UiDesign_SetCaption(""); /* 空窗口: 隐藏 */
        return;
    }
    shown = (shownBytes < textLen) ? shownBytes : textLen;
    maxW = design::CAPTION_W - design::CAPTION_PIXEL_MARGIN;

    /* 按像素宽折行模拟(参照 xiaozhi LV_LABEL_LONG_WRAP: 文本超气泡宽
     * 即换行), 滚动保留最后 CAPTION_MAX_LINES 行。行界用隐藏 ADAPT 标签
     * 二分: 行内宽度单调递增, 找 <=maxW 的最大前缀。
     * 关键: 只消费 prefix[0..shown) 已揭示文本, 未揭示部分绝不显示 */
    lineA_s = lineA_e = lineB_s = lineB_e = 0;
    cnt = 0;
    ls = 0;
    while (ls < shown) {
        int lo = ls + 1;
        int hi = (shown < ls + design::CAPTION_LINE_BYTES)
                     ? shown
                     : ls + design::CAPTION_LINE_BYTES;
        int best = -1;

        if (g_captionMeasure != nullptr) {
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                int l = mid - ls;

                (void)memcpy(mbuf, prefix + ls, (size_t)l);
                mbuf[l] = '\0';
                g_captionMeasure->SetText(mbuf);
                if (g_captionMeasure->GetTextWidth() <= maxW) {
                    best = mid; /* 行还能再长 */
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
        }
        if (best < 0) {
            best = ls + 1; /* 测量缺失/单字超宽: 硬切一字符防死循环 */
        }
        lineA_s = lineB_s; /* 滚动: A=倒数第二行, B=最后一行 */
        lineA_e = lineB_e;
        lineB_s = ls;
        lineB_e = best;
        cnt++;
        ls = best;
    }

    /* 拼 '\n' 显示最后两行(单行时只有 B); '\n' 是折行引擎的硬换行点 */
    pos = 0;
    if (cnt >= 2 && lineA_e > lineA_s) {
        int l = lineA_e - lineA_s;

        (void)memcpy(win + pos, prefix + lineA_s, (size_t)l);
        pos += l;
        win[pos++] = '\n';
    }
    {
        int l = lineB_e - lineB_s;

        (void)memcpy(win + pos, prefix + lineB_s, (size_t)l);
        pos += l;
    }
    win[pos] = '\0';
    if (win[0] == '\0') {
        UiDesign_SetCaption("");
        return;
    }
    g_captionLabel->SetText(win);
    if (!g_captionLabel->IsVisible()) {
        g_captionLabel->SetVisible(true);
    }
    g_captionLabel->Invalidate();
}

void UiDesign_SetWifiConnected(bool connected)
{
    if (g_wifiIcon != nullptr && g_wifiIcon->IsVisible() != connected) {
        g_wifiIcon->SetVisible(connected);
        g_wifiIcon->Invalidate();
    }
}
} // namespace OHOS
