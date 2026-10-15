/**
 * @file gif_decoder.c
 * @brief Minimal GIF89a decoder (LZW + interlace + transparency + disposal).
 *
 * Referenced from xiaozhi-esp32 (lvgl_display/gif/gifdec.c) architecture.
 * Self-contained: only stdio/stdlib/string, no third-party dependency.
 */
#include "gif_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIF_HEADER_SIZE 13       /* signature(6) + logical screen descriptor(7) */
#define GIF_MAX_LZW_CODE 4096
#define GIF_MAX_CODE_WIDTH 12
#define GIF_MAX_FRAME_COUNT 1024
#define GIF_EXT_INTRODUCER 0x21
#define GIF_IMAGE_SEPARATOR 0x2C
#define GIF_TRAILER 0x3B
#define GIF_EXT_GRAPHIC_CONTROL 0xF9
#define GIF_EXT_APPLICATION 0xFF
#define GIF_EXT_COMMENT 0xFE
#define GIF_EXT_PLAIN_TEXT 0x01

typedef struct GifColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} GifColor;

struct GifDecoder {
    /* whole file image */
    uint8_t *fileBuf;
    long fileSize;

    /* logical screen */
    int16_t width;
    int16_t height;
    GifColor gct[256];
    int gctSize;
    int bgIndex;

    /* canvas */
    uint8_t *canvas;          /* width*height*4, [B,G,R,A] */
    uint8_t *savedCanvas;     /* snapshot for DISPOSE_PREVIOUS, may be NULL */

    /* frame bookkeeping */
    long firstFrameOffset;    /* offset of first image separator */
    long cursor;              /* current read offset */
    int frameCount;
    int frameIndex;           /* next frame to decode */

    /* graphic control of the frame being decoded */
    int hasGCE;
    int transparentIndex;     /* -1 when none */
    int disposalMethod;       /* 0/1/2/3 */
    int delayCs;              /* delay in 1/100 s */

    /* pending disposal from the previous frame */
    int prevDisposal;
    int16_t prevLeft;
    int16_t prevTop;
    int16_t prevWidth;
    int16_t prevHeight;
};

static int ReadByte(const GifDecoder *d, long *pos)
{
    if (*pos >= d->fileSize) {
        return -1;
    }
    return d->fileBuf[(*pos)++];
}

static int ReadU16(const GifDecoder *d, long *pos)
{
    int lo = ReadByte(d, pos);
    int hi = ReadByte(d, pos);
    if (lo < 0 || hi < 0) {
        return -1;
    }
    return lo | (hi << 8);
}

static void SkipSubBlocks(const GifDecoder *d, long *pos)
{
    for (;;) {
        int len = ReadByte(d, pos);
        if (len <= 0) {
            return; /* terminator or EOF */
        }
        *pos += len;
        if (*pos >= d->fileSize) {
            return;
        }
    }
}

static int ParseHeader(GifDecoder *d)
{
    long pos = 0;
    int packed;

    if (d->fileSize < GIF_HEADER_SIZE) {
        return -1;
    }
    if (memcmp(d->fileBuf, "GIF87a", 6) != 0 && memcmp(d->fileBuf, "GIF89a", 6) != 0) {
        return -1;
    }
    pos = 6;
    d->width = (int16_t)ReadU16(d, &pos);
    d->height = (int16_t)ReadU16(d, &pos);
    packed = ReadByte(d, &pos);
    ReadByte(d, &pos); /* background color index */
    ReadByte(d, &pos); /* pixel aspect ratio */

    if (d->width <= 0 || d->height <= 0) {
        return -1;
    }
    if (packed & 0x80) { /* global color table flag */
        int shift = (packed & 0x07) + 1;
        d->gctSize = 1 << shift;
        for (int i = 0; i < d->gctSize; i++) {
            d->gct[i].r = d->fileBuf[pos++];
            d->gct[i].g = d->fileBuf[pos++];
            d->gct[i].b = d->fileBuf[pos++];
        }
    }
    d->firstFrameOffset = pos;
    d->cursor = pos;
    return 0;
}

/* Count frames and record the first frame offset; skips all blocks once. */
static int IndexFrames(GifDecoder *d)
{
    long pos = d->firstFrameOffset;
    int count = 0;

    while (pos < d->fileSize) {
        int block = ReadByte(d, &pos);
        if (block < 0) {
            break;
        }
        if (block == GIF_TRAILER) {
            break;
        } else if (block == GIF_EXT_INTRODUCER) {
            int label = ReadByte(d, &pos);
            if (label == GIF_EXT_APPLICATION) {
                ReadByte(d, &pos); /* block size (11) */
                pos += 11;         /* app identifier + auth code */
            }
            SkipSubBlocks(d, &pos);
        } else if (block == GIF_IMAGE_SEPARATOR) {
            int packed;
            if (count == 0) {
                d->firstFrameOffset = pos - 1; /* separator byte position */
            }
            pos += 8; /* left(2) top(2) width(2) height(2) */
            packed = ReadByte(d, &pos);
            if (packed & 0x80) { /* local color table */
                int shift = (packed & 0x07) + 1;
                pos += (1 << shift) * 3;
            }
            count++;
            if (count >= GIF_MAX_FRAME_COUNT) {
                break;
            }
            ReadByte(d, &pos); /* LZW min code size */
            SkipSubBlocks(d, &pos);
        } else {
            break; /* unknown block: stop indexing */
        }
    }
    d->frameCount = count;
    return count;
}

/* LSB-first bit reader over GIF sub-blocks.
 * Reads one byte at a time up to the current sub-block boundary so the
 * buffer never loses bits (a whole sub-block can exceed the buffer size). */
typedef struct GifBitReader {
    const GifDecoder *d;
    long pos;
    long subEnd;       /* end of the current sub-block, -1 if none opened */
    uint64_t bitBuf;
    int bitCount;
    int eof;
} GifBitReader;

static int BitRead(GifBitReader *br, int bits)
{
    while (br->bitCount < bits && !br->eof) {
        int b;
        if (br->subEnd >= 0 && br->pos < br->subEnd) {
            b = ReadByte(br->d, &br->pos);
            if (b < 0) {
                br->eof = 1;
                break;
            }
        } else {
            int len = ReadByte(br->d, &br->pos);
            if (len < 0 || len == 0) { /* EOF or sub-block terminator */
                br->eof = 1;
                break;
            }
            br->subEnd = br->pos + len;
            if (br->subEnd > br->d->fileSize) {
                br->subEnd = br->d->fileSize;
            }
            continue;
        }
        br->bitBuf |= (uint64_t)b << br->bitCount;
        br->bitCount += 8;
    }
    if (br->bitCount < bits) {
        return -1;
    }
    int code = (int)(br->bitBuf & ((1u << bits) - 1));
    br->bitBuf >>= bits;
    br->bitCount -= bits;
    return code;
}

/* Decode one LZW image into an index buffer of width*height entries. */
static int LzwDecode(const GifDecoder *d, long *pos, int minCodeSize,
                     uint16_t width, uint16_t height, uint8_t *outIndices)
{
    GifBitReader br = {d, *pos, 0, 0, 0};
    int clearCode = 1 << minCodeSize;
    int endCode = clearCode + 1;
    int nextCode = endCode + 1;
    int codeWidth = minCodeSize + 1;
    int prevCode = -1;
    uint16_t prefix[GIF_MAX_LZW_CODE];
    uint8_t suffix[GIF_MAX_LZW_CODE];
    uint8_t stack[GIF_MAX_LZW_CODE];
    size_t outIdx = 0;
    size_t outTotal = (size_t)width * height;

    if (minCodeSize < 1 || minCodeSize > 8) {
        return -1;
    }
    for (int i = 0; i < clearCode; i++) {
        prefix[i] = 0xFFFF;
        suffix[i] = (uint8_t)i;
    }

    for (;;) {
        int code = BitRead(&br, codeWidth);
        if (code < 0) {
            return -1;
        }
        if (code == clearCode) {
            codeWidth = minCodeSize + 1;
            nextCode = endCode + 1;
            prevCode = -1;
            continue;
        }
        if (code == endCode) {
            break;
        }

        int curCode = code;
        int stackTop = 0;
        if (curCode >= nextCode && prevCode >= 0) {
            /* deferred case: KwKwK */
            stack[stackTop++] = suffix[prevCode];
            curCode = prevCode;
        }
        while (curCode >= clearCode + 2) { /* walk the chain to the root */
            if (curCode >= GIF_MAX_LZW_CODE || stackTop >= GIF_MAX_LZW_CODE) {
                return -1;
            }
            stack[stackTop++] = suffix[curCode];
            curCode = (int)prefix[curCode];
        }
        /* curCode is a root symbol (< clearCode) */
        stack[stackTop++] = suffix[curCode];

        if (prevCode >= 0 && nextCode < GIF_MAX_LZW_CODE) {
            prefix[nextCode] = (uint16_t)prevCode;
            suffix[nextCode] = suffix[curCode];
            nextCode++;
            if (nextCode == (1 << codeWidth) && codeWidth < GIF_MAX_CODE_WIDTH) {
                codeWidth++;
            }
        }
        prevCode = code;

        while (stackTop > 0 && outIdx < outTotal) {
            outIndices[outIdx++] = stack[--stackTop];
        }
        if (outIdx >= outTotal) {
            break;
        }
    }

    *pos = br.pos;
    /* finish this image's sub-block chain: discard unread bytes of the
     * current sub-block, then the remaining sub-blocks + terminator.
     * If eof was set, the terminator was already consumed (or real EOF). */
    if (!br.eof) {
        if (br.subEnd >= 0 && br.pos < br.subEnd) {
            *pos = br.subEnd;
        }
        SkipSubBlocks(d, pos);
    }
    return 0;
}

static void CanvasFillRect(GifDecoder *d, int16_t left, int16_t top,
                           int16_t w, int16_t h, const uint8_t *bgra)
{
    for (int16_t y = top; y < top + h; y++) {
        if (y < 0 || y >= d->height) {
            continue;
        }
        uint8_t *row = d->canvas + (long)y * d->width * 4;
        for (int16_t x = left; x < left + w; x++) {
            if (x < 0 || x >= d->width) {
                continue;
            }
            memcpy(row + (long)x * 4, bgra, 4);
        }
    }
}

static void CanvasSnapshot(GifDecoder *d)
{
    if (d->savedCanvas == NULL) {
        d->savedCanvas = (uint8_t *)malloc((size_t)d->width * d->height * 4);
    }
    if (d->savedCanvas != NULL) {
        memcpy(d->savedCanvas, d->canvas, (size_t)d->width * d->height * 4);
    }
}

/* Apply the disposal method of the previously drawn frame. */
static void ApplyPrevDisposal(GifDecoder *d)
{
    uint8_t bg[4] = {0, 0, 0, 0}; /* transparent black */

    if (d->gctSize > d->bgIndex) {
        bg[0] = d->gct[d->bgIndex].b;
        bg[1] = d->gct[d->bgIndex].g;
        bg[2] = d->gct[d->bgIndex].r;
        bg[3] = 0xFF;
    }
    switch (d->prevDisposal) {
        case 2: /* restore to background */
            CanvasFillRect(d, d->prevLeft, d->prevTop, d->prevWidth, d->prevHeight, bg);
            break;
        case 3: /* restore to previous */
            if (d->savedCanvas != NULL) {
                memcpy(d->canvas, d->savedCanvas, (size_t)d->width * d->height * 4);
            }
            break;
        default: /* 0/1: leave as is */
            break;
    }
    d->prevDisposal = 0;
}

int GifDecodeNextFrame(GifDecoder *d)
{
    if (d == NULL || d->frameCount <= 0) {
        return -1;
    }

    ApplyPrevDisposal(d);

    /* scan forward to the next image separator */
    int block = ReadByte(d, &d->cursor);
    while (block >= 0 && block != GIF_IMAGE_SEPARATOR) {
        if (block == GIF_EXT_INTRODUCER) {
            int label = ReadByte(d, &d->cursor);
            if (label == GIF_EXT_GRAPHIC_CONTROL) {
                int len = ReadByte(d, &d->cursor);
                if (len >= 4) {
                    int packed = ReadByte(d, &d->cursor);
                    d->delayCs = ReadU16(d, &d->cursor);
                    d->transparentIndex = ReadByte(d, &d->cursor);
                    d->hasGCE = 1;
                    d->disposalMethod = (packed >> 2) & 0x07;
                    if ((packed & 0x01) == 0) {
                        d->transparentIndex = -1;
                    }
                } else {
                    d->cursor += (len > 0) ? len : 0;
                }
                SkipSubBlocks(d, &d->cursor);
            } else if (label == GIF_EXT_APPLICATION) {
                ReadByte(d, &d->cursor); /* block size (11) */
                d->cursor += 11;         /* app identifier + auth code */
                SkipSubBlocks(d, &d->cursor);
            } else {
                SkipSubBlocks(d, &d->cursor);
            }
        } else if (block == GIF_TRAILER || block < 0) {
            return -1;
        } else {
            d->cursor++; /* unknown single byte, keep scanning */
        }
        block = ReadByte(d, &d->cursor);
    }
    if (block != GIF_IMAGE_SEPARATOR) {
        return -1;
    }

    /* image descriptor */
    int16_t left = (int16_t)ReadU16(d, &d->cursor);
    int16_t top = (int16_t)ReadU16(d, &d->cursor);
    int16_t iw = (int16_t)ReadU16(d, &d->cursor);
    int16_t ih = (int16_t)ReadU16(d, &d->cursor);
    int packed = ReadByte(d, &d->cursor);
    if (iw <= 0 || ih <= 0) {
        return -1;
    }

    GifColor lct[256];
    GifColor *colorTable = d->gct;
    if (packed & 0x80) {
        int shift = (packed & 0x07) + 1;
        int size = 1 << shift;
        for (int i = 0; i < size; i++) {
            lct[i].r = d->fileBuf[d->cursor++];
            lct[i].g = d->fileBuf[d->cursor++];
            lct[i].b = d->fileBuf[d->cursor++];
        }
        colorTable = lct;
    }
    int interlaced = (packed & 0x40) ? 1 : 0;

    int minCodeSize = ReadByte(d, &d->cursor);
    uint8_t *indices = (uint8_t *)malloc((size_t)iw * ih);
    if (indices == NULL) {
        return -1;
    }
    int ret = LzwDecode(d, &d->cursor, minCodeSize, (uint16_t)iw, (uint16_t)ih, indices);
    if (ret < 0) {
        free(indices);
        return -1;
    }

    if (d->disposalMethod == 3) {
        CanvasSnapshot(d);
    }

    /* de-interlace row order first (pass1: 0,8,8... pass2: 4,8 pass3: 2,4 pass4: 1,2) */
    if (interlaced) {
        uint8_t *ordered = (uint8_t *)malloc((size_t)iw * ih);
        if (ordered != NULL) {
            static const int interlaceStart[4] = {0, 4, 2, 1};
            static const int interlaceStep[4] = {8, 8, 4, 2};
            int dstRow = 0;
            for (int pass = 0; pass < 4; pass++) {
                for (int y = interlaceStart[pass]; y < ih; y += interlaceStep[pass]) {
                    memcpy(ordered + (long)y * iw, indices + (long)dstRow * iw, iw);
                    dstRow++;
                }
            }
            memcpy(indices, ordered, (size_t)iw * ih);
            free(ordered);
        }
    }

    /* compose onto the canvas (BGRA), skipping transparent pixels */
    for (int y = 0; y < ih; y++) {
        int16_t cy = top + y;
        if (cy < 0 || cy >= d->height) {
            continue;
        }
        uint8_t *dstRow = d->canvas + (long)cy * d->width * 4;
        const uint8_t *srcRow = indices + (long)y * iw;
        for (int x = 0; x < iw; x++) {
            int16_t cx = left + x;
            if (cx < 0 || cx >= d->width) {
                continue;
            }
            uint8_t idx = srcRow[x];
            if (d->hasGCE && idx == d->transparentIndex) {
                continue; /* keep previous canvas content */
            }
            if (colorTable == d->gct && idx >= d->gctSize) {
                continue;
            }
            uint8_t *px = dstRow + (long)cx * 4;
            px[0] = colorTable[idx].b;
            px[1] = colorTable[idx].g;
            px[2] = colorTable[idx].r;
            px[3] = 0xFF;
        }
    }

    /* remember disposal for the next call */
    d->prevDisposal = d->disposalMethod;
    d->prevLeft = left;
    d->prevTop = top;
    d->prevWidth = iw;
    d->prevHeight = ih;
    d->hasGCE = 0;
    d->disposalMethod = 0;

    free(indices);
    d->frameIndex++;
    if (d->frameIndex >= d->frameCount) {
        d->frameIndex = 0;
    }
    int delayMs = d->delayCs * 10; /* 1/100 s -> ms */
    d->delayCs = 0;
    return delayMs;
}

void GifRewind(GifDecoder *d)
{
    if (d == NULL) {
        return;
    }
    d->cursor = d->firstFrameOffset;
    d->hasGCE = 0;
    d->disposalMethod = 0;
    d->prevDisposal = 0;
    d->delayCs = 0;
    d->frameIndex = 0;
    memset(d->canvas, 0, (size_t)d->width * d->height * 4);
}

GifDecoder *GifLoadFile(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 8 * 1024 * 1024) { /* 8MB: 全屏背景 GIF 较大(原护栏 4MB 不够) */
        fclose(fp);
        return NULL;
    }

    GifDecoder *d = (GifDecoder *)calloc(1, sizeof(GifDecoder));
    if (d == NULL) {
        fclose(fp);
        return NULL;
    }
    d->fileBuf = (uint8_t *)malloc((size_t)size);
    if (d->fileBuf == NULL) {
        free(d);
        fclose(fp);
        return NULL;
    }
    if (fread(d->fileBuf, 1, (size_t)size, fp) != (size_t)size) {
        free(d->fileBuf);
        free(d);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    d->fileSize = size;

    if (ParseHeader(d) < 0 || IndexFrames(d) <= 0) {
        free(d->fileBuf);
        free(d);
        return NULL;
    }

    d->canvas = (uint8_t *)calloc((size_t)d->width * d->height, 4);
    if (d->canvas == NULL) {
        free(d->fileBuf);
        free(d);
        return NULL;
    }
    d->transparentIndex = -1;
    d->cursor = d->firstFrameOffset;
    return d;
}

int16_t GifGetWidth(const GifDecoder *d)
{
    return (d != NULL) ? d->width : 0;
}

int16_t GifGetHeight(const GifDecoder *d)
{
    return (d != NULL) ? d->height : 0;
}

int GifGetFrameCount(const GifDecoder *d)
{
    return (d != NULL) ? d->frameCount : 0;
}

const uint8_t *GifGetCanvas(const GifDecoder *d)
{
    return (d != NULL) ? d->canvas : NULL;
}

void GifClose(GifDecoder *d)
{
    if (d == NULL) {
        return;
    }
    free(d->fileBuf);
    free(d->canvas);
    free(d->savedCanvas);
    free(d);
}
