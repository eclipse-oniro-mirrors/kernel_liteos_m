/**
 * @file emoji_display.cpp
 * @brief Emoji library implementation for ui_lite.
 *        Referenced from xiaozhi-esp32: EmojiCollection (name -> image) and
 *        the GIF playback loop (GifImageAnimator pattern in ui_image_view.cpp).
 */
#include "emoji_display.h"

#include "gfx_utils/image_info.h"

#include <cstdio>
#include <cstring>

namespace OHOS {
/* ==================== 表情注册表 ==================== */
/* 新增表情: 加一行 { "表情名", "GIF文件名" } 即可, file 传 nullptr 表示
 * 文件名就是 "<表情名>.gif"; 然后把 GIF 放进资产目录(设备端 /storage/data/emoji/)。
 * EMOJI_COUNT 由表自动推导, 无需手改。表情名顺序即 SwitchNext 的切换顺序。 */
const EmojiLibrary::EmojiEntry EmojiLibrary::EMOJI_TABLE[] = {
    {"neutral", nullptr}, {"happy", nullptr},    {"laughing", nullptr},
    {"funny", nullptr},   {"sad", nullptr},      {"angry", nullptr},
    {"crying", nullptr},  {"loving", nullptr},   {"embarrassed", nullptr},
    {"surprised", nullptr}, {"shocked", nullptr}, {"thinking", nullptr},
    {"winking", nullptr}, {"cool", nullptr},     {"relaxed", nullptr},
    {"delicious", nullptr}, {"kissy", nullptr},  {"confident", nullptr},
    {"sleepy", nullptr},  {"silly", nullptr},    {"confused", nullptr},
};
const int EmojiLibrary::EMOJI_COUNT =
    sizeof(EmojiLibrary::EMOJI_TABLE) / sizeof(EmojiLibrary::EMOJI_TABLE[0]);

/* 缩放上下限: 过小看不清, 过大浪费内存(GIF 源 240x240, 400% 即 960x960) */
static const uint32_t MIN_SCALE_PERCENT = 10;
static const uint32_t MAX_SCALE_PERCENT = 400;

/* 零延时会烧 CPU, 钳到一个合理下限 */
static constexpr uint32_t MIN_FRAME_DELAY_MS = 40;
static constexpr uint32_t DEFAULT_FRAME_DELAY_MS = 100;

GifEmojiView::GifEmojiView()
    : decoder_(nullptr),
      anim_(nullptr),
      delayMs_(DEFAULT_FRAME_DELAY_MS),
      lastRunTime_(0),
      deltaTime_(0),
      scalePercent_(100),
      dispBuf_(nullptr),
      dispSize_(0),
      dispW_(0),
      dispH_(0)
{
    (void)memset(&imgInfo_, 0, sizeof(imgInfo_));
}

GifEmojiView::~GifEmojiView()
{
    StopGif();
    delete[] dispBuf_;
    dispBuf_ = nullptr;
}

void GifEmojiView::SetScale(uint32_t percent)
{
    if (percent < MIN_SCALE_PERCENT) {
        percent = MIN_SCALE_PERCENT;
    }
    if (percent > MAX_SCALE_PERCENT) {
        percent = MAX_SCALE_PERCENT;
    }
    if (scalePercent_ == percent) {
        return;
    }
    scalePercent_ = percent;
    if (decoder_ != nullptr) {
        ApplyScale();
        SetSrc(&imgInfo_); /* UIImageView::SetSrc ends with Invalidate() */
    }
}

void GifEmojiView::ApplyScale()
{
    GifDecoder *d = static_cast<GifDecoder *>(decoder_);
    if (d == nullptr) {
        return;
    }
    int srcW = GifGetWidth(d);
    int srcH = GifGetHeight(d);
    if (scalePercent_ == 100) {
        /* 1:1: 直接把解码画布交给 ImageInfo, 零拷贝 */
        delete[] dispBuf_;
        dispBuf_ = nullptr;
        dispSize_ = 0;
        dispW_ = srcW;
        dispH_ = srcH;
        imgInfo_.header.width = srcW;
        imgInfo_.header.height = srcH;
        imgInfo_.dataSize = (uint32_t)srcW * srcH * 4; /* 4: BGRA bytes per pixel */
        imgInfo_.data = const_cast<uint8_t *>(GifGetCanvas(d));
    } else {
        int dstW = srcW * (int)scalePercent_ / 100;
        int dstH = srcH * (int)scalePercent_ / 100;
        if (dstW < 1) {
            dstW = 1;
        }
        if (dstH < 1) {
            dstH = 1;
        }
        uint32_t need = (uint32_t)dstW * dstH * 4; /* 4: BGRA bytes per pixel */
        if (dispBuf_ == nullptr || dispW_ != dstW || dispH_ != dstH) {
            delete[] dispBuf_;
            dispBuf_ = new uint8_t[need];
            dispSize_ = need;
            dispW_ = dstW;
            dispH_ = dstH;
        }
        if (dispBuf_ == nullptr) {
            return; /* 分配失败: 保留上一次的有效画面 */
        }
        /* 最近邻重采样: 解码画布(BGRA) -> 显示画布 */
        const uint8_t *src = GifGetCanvas(d);
        for (int y = 0; y < dstH; y++) {
            const uint8_t *srcRow = src + (long)(y * srcH / dstH) * srcW * 4;
            uint8_t *dstRow = dispBuf_ + (long)y * dstW * 4;
            for (int x = 0; x < dstW; x++) {
                const uint8_t *px = srcRow + (long)(x * srcW / dstW) * 4;
                uint8_t *q = dstRow + (long)x * 4;
                q[0] = px[0];
                q[1] = px[1];
                q[2] = px[2];
                q[3] = px[3];
            }
        }
        imgInfo_.header.width = dstW;
        imgInfo_.header.height = dstH;
        imgInfo_.dataSize = dispSize_;
        imgInfo_.data = dispBuf_;
    }
    /* 视图尺寸与显示尺寸保持一致 */
    Resize(imgInfo_.header.width, imgInfo_.header.height);
}

bool GifEmojiView::LoadGif(const char *path)
{
    StopGif();

    GifDecoder *d = GifLoadFile(path);
    if (d == nullptr) {
        printf("[emoji] load failed: %s\n", path);
        return false;
    }
    printf("[emoji] loaded %s: %dx%d, %d frames\n", path,
           GifGetWidth(d), GifGetHeight(d), GifGetFrameCount(d));

    decoder_ = d;
    (void)memset(&imgInfo_, 0, sizeof(imgInfo_));
    imgInfo_.header.colorMode = ARGB8888;

    /* show the first frame immediately */
    int delay = GifDecodeNextFrame(d);
    delayMs_ = (delay > 0) ? (uint32_t)delay : DEFAULT_FRAME_DELAY_MS;
    if (delayMs_ < MIN_FRAME_DELAY_MS) {
        delayMs_ = MIN_FRAME_DELAY_MS;
    }
    ApplyScale(); /* 填充 imgInfo_(含缩放)并同步视图尺寸 */
    SetSrc(&imgInfo_);

    /* 10 ms tick: the callback honors each frame's own delay (see Callback) */
    lastRunTime_ = 0;
    deltaTime_ = 0;
    anim_ = new Animator(this, this, 10, true);
    anim_->Start();
    return true;
}

void GifEmojiView::StopGif()
{
    if (anim_ != nullptr) {
        anim_->Stop();
        delete anim_;
        anim_ = nullptr;
    }
    if (decoder_ != nullptr) {
        GifClose(static_cast<GifDecoder *>(decoder_));
        decoder_ = nullptr;
    }
}

void GifEmojiView::Callback(UIView *view)
{
    if (view == nullptr || decoder_ == nullptr || anim_ == nullptr) {
        return;
    }
    /* frame pacing, same as GifImageAnimator::Callback */
    uint32_t curTime = anim_->GetRunTime();
    if (curTime != 0) {
        if (curTime + deltaTime_ - lastRunTime_ >= delayMs_) {
            deltaTime_ = curTime + deltaTime_ - lastRunTime_ - delayMs_;
            lastRunTime_ = curTime;
        } else {
            return;
        }
    }

    GifDecoder *d = static_cast<GifDecoder *>(decoder_);
    int delay = GifDecodeNextFrame(d);
    if (delay < 0) {
        GifRewind(d); /* loop playback */
        delay = GifDecodeNextFrame(d);
        if (delay < 0) {
            return;
        }
    }
    delayMs_ = (delay > 0) ? (uint32_t)delay : DEFAULT_FRAME_DELAY_MS;
    if (delayMs_ < MIN_FRAME_DELAY_MS) {
        delayMs_ = MIN_FRAME_DELAY_MS;
    }
    if (scalePercent_ != 100) {
        ApplyScale(); /* 新帧重采样到显示画布 */
    } else {
        imgInfo_.data = const_cast<uint8_t *>(GifGetCanvas(d));
    }
    SetSrc(&imgInfo_); /* UIImageView::SetSrc ends with Invalidate() */
}

bool EmojiLibrary::Init(GifEmojiView *view, const char *dir)
{
    if (view == nullptr || dir == nullptr) {
        return false;
    }
    view_ = view;
    dir_ = dir;
    return true;
}

bool EmojiLibrary::SetEmotion(const char *name)
{
    if (view_ == nullptr || dir_ == nullptr || name == nullptr) {
        return false;
    }
    for (int i = 0; i < EMOJI_COUNT; i++) {
        if (strcmp(EMOJI_TABLE[i].name, name) == 0) {
            return SetEmotionByIndex(i);
        }
    }
    printf("[emoji] unknown emotion: %s\n", name);
    return false;
}

const char *EmojiLibrary::EmotionName(int index) const
{
    if (index < 0 || index >= EMOJI_COUNT) {
        return nullptr;
    }
    return EMOJI_TABLE[index].name;
}

bool EmojiLibrary::SetEmotionByIndex(int index)
{
    if (view_ == nullptr || dir_ == nullptr) {
        return false;
    }
    if (index < 0) {
        index = EMOJI_COUNT - 1;
    }
    if (index >= EMOJI_COUNT) {
        index = 0;
    }
    char path[128];
    const char *file = EMOJI_TABLE[index].file;
    int n;
    if (file != nullptr) {
        n = snprintf(path, sizeof(path), "%s/%s", dir_, file);
    } else {
        n = snprintf(path, sizeof(path), "%s/%s.gif", dir_, EMOJI_TABLE[index].name);
    }
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    if (!view_->LoadGif(path)) {
        return false;
    }
    curIndex_ = index;
    return true;
}

bool EmojiLibrary::SwitchNext()
{
    return SetEmotionByIndex(curIndex_ + 1);
}

bool EmojiLibrary::SwitchPrev()
{
    /* curIndex_=-1(未加载)时 -2 也会被 SetEmotionByIndex 归位到最后一个 */
    return SetEmotionByIndex(curIndex_ - 1);
}

/* ==================== 门面 API ==================== */

namespace {
GifEmojiView *g_facadeView = nullptr;
EmojiLibrary *g_facadeLib = nullptr;
}

bool EmojiDisplay_Init(const char *gifDir)
{
    if (gifDir == nullptr) {
        return false;
    }
    if (g_facadeLib != nullptr) {
        return true; /* 已初始化, 幂等 */
    }
    g_facadeView = new GifEmojiView();
    g_facadeLib = new EmojiLibrary();
    if (!g_facadeLib->Init(g_facadeView, gifDir)) {
        delete g_facadeLib;
        delete g_facadeView;
        g_facadeLib = nullptr;
        g_facadeView = nullptr;
        return false;
    }
    return true;
}

UIView *EmojiDisplay_GetView(void)
{
    return g_facadeView;
}

void EmojiDisplay_SetPosition(int16_t x, int16_t y)
{
    if (g_facadeView != nullptr) {
        g_facadeView->SetPosition(x, y);
    }
}

bool EmojiDisplay_SetScale(uint32_t percent)
{
    if (g_facadeView == nullptr) {
        return false;
    }
    g_facadeView->SetScale(percent);
    return true;
}

bool EmojiDisplay_SetEmotion(const char *name)
{
    return (g_facadeLib != nullptr) ? g_facadeLib->SetEmotion(name) : false;
}

bool EmojiDisplay_SetEmotionByIndex(int index)
{
    return (g_facadeLib != nullptr) ? g_facadeLib->SetEmotionByIndex(index) : false;
}

bool EmojiDisplay_Next(void)
{
    return (g_facadeLib != nullptr) ? g_facadeLib->SwitchNext() : false;
}

bool EmojiDisplay_Prev(void)
{
    return (g_facadeLib != nullptr) ? g_facadeLib->SwitchPrev() : false;
}

const char *EmojiDisplay_CurrentName(void)
{
    return (g_facadeLib != nullptr) ? g_facadeLib->CurrentEmotion() : nullptr;
}

int EmojiDisplay_GetCount(void)
{
    return EmojiLibrary::EMOJI_COUNT;
}

const char *EmojiDisplay_Name(int index)
{
    if (index < 0 || index >= EmojiLibrary::EMOJI_COUNT) {
        return nullptr;
    }
    return EmojiLibrary::EMOJI_TABLE[index].name;
}
} // namespace OHOS
