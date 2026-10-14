/*
 * voice_ws.h - WebSocket 客户端(实例化句柄 + 二进制帧)
 *
 * 链路: socket(TCP) → mbedTLS(TLS1.2) → WebSocket 帧层
 *   - 板上 DNS 不可用, mbedtls_net_connect 用预解析 IP
 *   - MBEDTLS_SSL_VERIFY_NONE 跳过证书验证(不用打包 cacert.pem)
 *   - WebSocket: 客户端发送 mask(payload XOR 4 字节 maskkey), 服务端发送不 mask
 *
 * 区别于 wake_ws(单连接全局态 + 仅 TEXT 帧): voice_ws 用堆分配句柄 voice_ws_t,
 * 支持多条 WS 同时在线(ASR 长连接 + TTS 每轮); 支持 BINARY 帧收发(阿里云
 * DashScope inference 用二进制 PCM 传音频)。复制自 wake_ws.c, 重构全局→句柄。
 *
 * 参考: wake_ws.c(mbedTLS 握手 + RFC6455 帧层); applications/llm_sdk/llm_client.c(TLS 模式)
 */
#ifndef VOICE_WS_H
#define VOICE_WS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 阿里云 DashScope inference 连接参数(板上 DNS 不可用, IP 预解析; 复用 wake 的 workspace) */
#define VOICE_WS_IP   "47.94.20.201"
#define VOICE_WS_HOST "ws-qq7tg8jxbjmnqaty.cn-beijing.maas.aliyuncs.com"
#define VOICE_WS_PORT 443
#define VOICE_WS_PATH "/api-ws/v1/inference"

/* WebSocket 帧类型(RFC 6455 opcode) */
#define VOICE_WS_TEXT    0x1
#define VOICE_WS_BINARY  0x2
#define VOICE_WS_CLOSE   0x8
#define VOICE_WS_PING    0x9
#define VOICE_WS_PONG    0xA

/* 连接句柄(不透明, 内部含 mbedTLS 上下文 + 接收缓冲 + 发送互斥锁) */
typedef struct voice_ws_s voice_ws_t;

/*
 * 建立 wss:// 连接: TCP → TLS 握手 → WebSocket 升级握手。
 * api_key: DashScope API key(Bearer 认证)。
 * 成功返回句柄(需用 voice_ws_close 释放), 失败返回 NULL。
 */
voice_ws_t *voice_ws_connect(const char *ip, const char *host, int port,
                             const char *path, const char *api_key);

/*
 * 发送文本帧(UTF-8 JSON 事件)。成功 0, 失败 -1。
 */
int voice_ws_send_text(voice_ws_t *ws, const char *data, int len);

/*
 * 发送二进制帧(裸 PCM 音频)。成功 0, 失败 -1。
 */
int voice_ws_send_binary(voice_ws_t *ws, const void *data, int len);

/*
 * 接收一帧。out 收 payload, *is_binary 输出是否二进制帧(非 NULL 时)。
 * 返回 payload 长度; 0 = 超时无完整帧; <0 = 失败/连接关闭。
 * timeout_ms=0 非阻塞(无数据返回 0); >0 阻塞等待最多 timeout_ms。
 * 仅 TEXT 帧对 payload 空终止; BINARY 帧保留原字节, 按返回长度读取。
 */
int voice_ws_recv(voice_ws_t *ws, void *out, int cap, int *is_binary, int timeout_ms);

/*
 * 关闭 WebSocket + TLS + socket, 释放句柄(ws 置 NULL)。
 */
void voice_ws_close(voice_ws_t *ws);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_WS_H */
