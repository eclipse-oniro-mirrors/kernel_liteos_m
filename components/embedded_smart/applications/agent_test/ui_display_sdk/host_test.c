#include <stdio.h>
#include <stdlib.h>
#include "gif_decoder.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <gif>\n", argv[0]);
        return 1;
    }
    GifDecoder *d = GifLoadFile(argv[1]);
    if (d == NULL) {
        printf("LOAD FAIL\n");
        return 1;
    }
    int w = GifGetWidth(d), h = GifGetHeight(d);
    int total = w * h;
    int expect = GifGetFrameCount(d);
    printf("size=%dx%d frames=%d\n", w, h, expect);

    int decoded = 0, fails = 0;
    for (int f = 0; f < expect; f++) {
        int delay = GifDecodeNextFrame(d);
        if (delay < 0) {
            fails++;
            break;
        }
        decoded++;
        if (f < 2 || f == expect - 1) {
            const uint8_t *c = GifGetCanvas(d);
            int opaque = 0;
            for (int i = 0; i < total; i++) {
                if (c[i * 4 + 3] == 0xFF) opaque++;
            }
            printf("frame %d: ret=%dms opaque=%d/%d px[center]=%02x%02x%02x a=%02x\n",
                   f, delay, opaque, total,
                   c[(total / 2) * 4], c[(total / 2) * 4 + 1], c[(total / 2) * 4 + 2],
                   c[(total / 2) * 4 + 3]);
        }
    }
    printf("RESULT: decoded %d/%d, fails=%d -> %s\n", decoded, expect, fails,
           (decoded == expect && fails == 0) ? "PASS" : "FAIL");
    GifClose(d);
    return (decoded == expect && fails == 0) ? 0 : 1;
}
