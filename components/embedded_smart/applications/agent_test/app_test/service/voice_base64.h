/*
 * voice_base64.h - base64 编解码(WebSocket 握手 + 阿里云音频传输用)
 *
 * 复制自 wake_base64(纯算法, 0 依赖), 仅改前缀 wake→voice。
 * 用途: ws 握手 Sec-WebSocket-Key 编码; ASR/TTS 二进制帧虽不走 base64(裸 PCM),
 *       但保留以备 JSON 内嵌场景。
 */
#ifndef VOICE_BASE64_H
#define VOICE_BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * base64 编码:data(len 字节)→ out(以 '\0' 结尾)。
 * 返回 out 长度(不含 '\0')。out 至少能装 4*ceil(len/3)+1 字节。
 */
int voice_base64_encode(const unsigned char *data, int len, char *out, int out_cap);

/*
 * base64 解码:b64(以 '\0' 结尾)→ out(裸字节)。
 * 返回解码字节数。out 至少能装 3*strlen(b64)/4 字节。
 * 非法字符返回 -1。
 */
int voice_base64_decode(const char *b64, unsigned char *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_BASE64_H */
