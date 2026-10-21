/**
 * @file gif_decoder.h
 * @brief Minimal GIF89a decoder for the emoji library.
 *
 * Ported for the Hi3516DV300 ui_display demo, referencing the
 * xiaozhi-esp32 emoji architecture (otto-emoji-gif-component + lvgl gifdec).
 *
 * Usage:
 *   GifDecoder* d = GifLoadFile("/storage/data/emoji/neutral.gif");
 *   int delayMs = GifDecodeNextFrame(d);   // compose frame onto canvas
 *   const uint8_t* px = GifGetCanvas(d);   // BGRA bytes (ui_lite ARGB8888)
 *   GifRewind(d);                          // loop playback
 *   GifClose(d);
 *
 * Canvas pixel memory layout: [B, G, R, A] per pixel, matching ui_lite
 * ImageInfo colorMode ARGB8888 (see GifImageAnimator in ui_image_view.cpp).
 */
#ifndef GIF_DECODER_H
#define GIF_DECODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GifDecoder GifDecoder;

/* Open and parse a GIF file (header, global color table, frame index). */
GifDecoder *GifLoadFile(const char *path);

/* Close and free everything. */
void GifClose(GifDecoder *d);

/* Canvas dimensions. */
int16_t GifGetWidth(const GifDecoder *d);
int16_t GifGetHeight(const GifDecoder *d);

/* Total number of image frames in the file. */
int GifGetFrameCount(const GifDecoder *d);

/* Decode the next frame onto the canvas.
 * Returns the frame delay in milliseconds (>= 0), or -1 when no more frames. */
int GifDecodeNextFrame(GifDecoder *d);

/* Rewind playback to the first frame and reset the canvas. */
void GifRewind(GifDecoder *d);

/* Canvas buffer: width * height * 4 bytes, layout [B, G, R, A]. */
const uint8_t *GifGetCanvas(const GifDecoder *d);

#ifdef __cplusplus
}
#endif

#endif /* GIF_DECODER_H */
