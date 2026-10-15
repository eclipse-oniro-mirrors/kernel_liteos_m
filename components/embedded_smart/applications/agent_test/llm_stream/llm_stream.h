/* llm_stream.h — LLM 流式传输协议层: chunked + SSE 增量解码器 + delta 正文提取
 * 纯协议层: 不依赖网络/TLS/malloc, 可在 PC 主机单测(llm_stream_test_host.c)
 * 消费方: llm_client.c 的 https_stream_once(流式传输)
 *
 * 字节流向:
 *   mbedtls_ssl_read 原始字节(可能任意位置撕裂)
 *     -> llm_sse_feed (chunked 解帧 + SSE 切行)
 *     -> 回调: 每个 "data:" 行的载荷(完整行, 已去前导空格/换行)
 *     -> llm_sse_content_frag (载荷 JSON 里抠 delta.content 片段)
 *
 * 线性安全: 单连接单线程使用(与 llm_client 的 g_lock 串行化配套)
 */
#ifndef LLM_STREAM_H
#define LLM_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 单行(SSE 事件)缓冲上限。真实事件约 600B(含 usage 字段), 2KB 留 3 倍余量;
 * 超长行整行丢弃(s->overflow 置位), 流不中断——丢一个 token 好过断流 */
#define LLS_BUF_SIZE 2048
/* 帧层行(chunk-size/trailer)缓冲上限, 超出部分静默丢弃(仅长度/空行判定有意义) */
#define LLS_META_SIZE 64

/* data 行回调: 每个 "data:" 事件调用一次
 *   line/len: 载荷(已去 "data:" 前缀与前导空格, 不含换行, line 以 NUL 结尾)
 *   返回 0 继续; 非 0 = 请求停止回调(muted 置位, 解码器继续冲到 chunked 终结)
 */
typedef int (*llm_sse_cb)(void *user, const char *line, int len);

typedef struct {
    /* 配置(llm_sse_init 填) */
    llm_sse_cb cb;
    void      *user;
    /* ---- 以下字段由模块内部维护, 调用方只读 ---- */
    /* SSE 行装配缓冲 */
    char buf[LLS_BUF_SIZE];
    int  len;
    int  overflow;    /* 1 = 当前行超长丢弃模式, 等行尾恢复 */
    /* chunked 状态机 */
    int  state;
    long chunk_rem;   /* 当前 chunk 剩余字节 */
    int  chunked;     /* 1 = body 为 chunked 编码; 0 = 裸流(直读) */
    /* 帧层行缓冲(chunk-size/trailer 行)——与 SSE 行缓冲严格独立:
     * chunk 数据边界可落在 SSE 行中间(实测常见), 帧层不得触碰 SSE 行装配状态 */
    char meta[LLS_META_SIZE];
    int  mlen;
    /* 结果 */
    int  muted;       /* 1 = 回调已请求停止: 继续解析(冲到 chunked 终结)但不再回调 */
    int  done;        /* 1 = chunked 正常终结(0块+trailer空行) / 裸流收满 */
} llm_sse_t;

/* 初始化: chunked=1 按 chunked 解帧; chunked=0 裸流(整段都是 SSE 行) */
void llm_sse_init(llm_sse_t *s, int chunked, llm_sse_cb cb, void *user);

/* 喂入 body 原始字节(任意大小, 任意撕裂点)
 * 返回: 0 正常(含 muted 后的继续解析); -1 协议错误(非法 chunk-size, 连接已不可信)
 * 注: done 置位后再 feed 直接忽略; 回调内可安全读 s->muted 等字段 */
int llm_sse_feed(llm_sse_t *s, const char *data, int n);

/* 从 SSE data 载荷(chat.completion.chunk JSON)提取 delta 正文片段
 * 命中: 返回片段长度(>=0, 可为 0 = 首事件空 content), frag 填内容(已反转义, NUL 结尾)
 * 未命中: 返回 -1(reasoning_content 思考事件 / role 首事件无 content / usage / finish 事件)
 * 说明: 模式 "\"content\":\"" 带前导引号——"content":null 与 "reasoning_content":" 均不误匹配 */
int llm_sse_content_frag(const char *line, char *frag, int cap);

#ifdef __cplusplus
}
#endif
#endif /* LLM_STREAM_H */
