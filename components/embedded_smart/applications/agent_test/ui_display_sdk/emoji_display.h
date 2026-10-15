/**
 * @file emoji_display.h
 * @brief 表情包库(独立可移植模块): 调用函数即可显示/切换/缩放 GIF 表情动画。
 *
 * 移植自 xiaozhi-esp32 表情架构, 每个表情一个 GIF 文件, 通过 EMOJI_TABLE
 * 映射表注册(见 emoji_display.cpp, 新增表情只需加一行)。
 *
 * 依赖: ui_lite(UIView/UIImageView/Animator) + gif_decoder(纯 C 解码器)。
 * 不依赖任何布局/业务逻辑 —— 视图由本模块自建, 位置/缩放由本模块 API 控制。
 *
 * 简单用法(门面 API, 推荐):
 *   EmojiDisplay_Init("/storage/data/emoji");   // 初始化(自建视图)
 *   EmojiDisplay_SetEmotion("happy");           // 按名显示表情
 *   EmojiDisplay_Next();                        // 循环切换
 *   EmojiDisplay_SetPosition(60, 100);          // 随时调整位置
 *   EmojiDisplay_SetScale(200);                 // 缩放: 100=原始尺寸
 *   root->Add(EmojiDisplay_GetView());          // 挂到视图树
 *
 * 高级用法: 直接使用 GifEmojiView / EmojiLibrary 类(可多实例)。
 */
#ifndef EMOJI_DISPLAY_H
#define EMOJI_DISPLAY_H

#include <stdint.h>

#include "animator/animator.h"
#include "components/ui_image_view.h"

extern "C" {
#include "gif_decoder.h"
}

namespace OHOS {
/**
 * @brief 动态 GIF 视图: gif_decoder 解码帧, ui_lite Animator 驱动播放
 *        (与 ui_lite GifImageAnimator 同款节奏算法), 内置最近邻缩放。
 */
class GifEmojiView : public UIImageView, public AnimatorCallback {
public:
    GifEmojiView();
    ~GifEmojiView() override;

    /* 加载 GIF 并开始播放; 视图尺寸自动同步为缩放后的尺寸; 失败返回 false */
    bool LoadGif(const char *path);

    /* 停止播放并释放解码器 */
    void StopGif();

    /* 缩放(最近邻采样): percent=100 原始尺寸, 50 减半, 200 放大一倍;
     * 限制 10~400; 播放中调用立即生效, 后续帧按新比例渲染 */
    void SetScale(uint32_t percent);

    /* AnimatorCallback: 每个 tick 渲染下一帧 */
    void Callback(UIView *view) override;

private:
    /* 按当前 scalePercent_ 重采样画布并同步 imgInfo_ 与视图尺寸 */
    void ApplyScale();

    void *decoder_;
    ImageInfo imgInfo_;
    Animator *anim_;
    uint32_t delayMs_;
    uint32_t lastRunTime_;
    uint32_t deltaTime_;
    uint32_t scalePercent_; /* 缩放百分比, 100 = 原始尺寸 */
    uint8_t *dispBuf_;      /* 缩放后的显示画布; nullptr 表示 1:1 直用解码画布 */
    uint32_t dispSize_;     /* dispBuf_ 字节数 */
    int dispW_;
    int dispH_;
};

/**
 * @brief 表情库: 表情名到 GIF 文件的映射与播放调度。
 *
 * 新增表情: 在 EMOJI_TABLE(emoji_display.cpp)加一行, 并把 GIF 放进资产目录。
 */
class EmojiLibrary {
public:
    /* 映射条目: name 对外 API 使用的表情名; file 为 GIF 文件名, nullptr 表示 <name>.gif */
    struct EmojiEntry {
        const char *name;
        const char *file;
    };

    /* 表情注册表与数量(数量由表自动推导, 见 emoji_display.cpp) */
    static const EmojiEntry EMOJI_TABLE[];
    static const int EMOJI_COUNT;

    EmojiLibrary() : view_(nullptr), dir_(nullptr), curIndex_(-1) {}

    /* 绑定显示视图与 GIF 目录(如 "/storage/data/emoji", 字符串须长期有效) */
    bool Init(GifEmojiView *view, const char *dir);

    /* xiaozhi 风格 API: 按名切换表情 */
    bool SetEmotion(const char *name);

    /* 按序号切换(自动回绕) */
    bool SetEmotionByIndex(int index);

    /* 切换到注册表中的下一个表情 */
    bool SwitchNext();

    /* 切换到注册表中的上一个表情(与 SwitchNext 一样自动回绕) */
    bool SwitchPrev();

    /* 当前表情名(未加载时为 nullptr); index 对应的表情名(越界返回 nullptr) */
    const char *CurrentEmotion() const
    {
        return (curIndex_ >= 0 && curIndex_ < EMOJI_COUNT) ? EMOJI_TABLE[curIndex_].name : nullptr;
    }
    const char *EmotionName(int index) const;

private:
    GifEmojiView *view_;
    const char *dir_;
    int curIndex_;
};

/* ==================== 门面 API(调用函数即可用) ==================== */

/* 初始化表情库(内部自建视图); 重复调用幂等; gifDir 须长期有效 */
bool EmojiDisplay_Init(const char *gifDir);

/* 获取表情视图, 由上层(设计层)负责挂到视图树; 未初始化时返回 nullptr */
UIView *EmojiDisplay_GetView(void);

/* 位置控制(独立封装, 便于调整): x/y 为屏幕坐标 */
void EmojiDisplay_SetPosition(int16_t x, int16_t y);

/* 缩放控制: percent=100 原始尺寸, 限制 10~400; 视图尺寸随之自动调整 */
bool EmojiDisplay_SetScale(uint32_t percent);

/* 显示指定表情(按名/按序号), 成功返回 true */
bool EmojiDisplay_SetEmotion(const char *name);
bool EmojiDisplay_SetEmotionByIndex(int index);

/* 循环切换下一个表情; 当前表情名(未加载时为 nullptr) */
bool EmojiDisplay_Next(void);

/* 循环切换上一个表情(未加载时从最后一个开始, 同样回绕) */
bool EmojiDisplay_Prev(void);
const char *EmojiDisplay_CurrentName(void);

/* 注册表查询: 表情总数 / 第 index 个表情名 */
int EmojiDisplay_GetCount(void);
const char *EmojiDisplay_Name(int index);
} // namespace OHOS

#endif /* EMOJI_DISPLAY_H */
