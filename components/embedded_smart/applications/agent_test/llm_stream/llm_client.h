/* llm_client.h — 镜像内模块共用的 LLM 问答客户端库
 * 四种用法:
 *   1) llm_chat():        客户自带网关+key — 板上直连 TLS 1.2, 库内封装 JSON,
 *                         回复自动提取 content(白话进白话出), 一次性返回全文
 *   2) llm_chat_raw():    高阶接口 — 客户自带完整 JSON body(多轮/system/参数),
 *                         响应 body 原样返回不解析
 *   3) llm_chat_stream(): 流式接口(v1.3) — 请求自动加 "stream":true, 服务端
 *                         SSE 增量推送, 正文片段实时回调(打字机效果); 思考过程
 *                         (reasoning_content)不透传, 只吐正文
 *   4) llm_ask():         便捷接口 — 走 PC 中继(默认 192.168.1.1:8443 /say), 免 key
 * 链接: -lllmclient (动态) 或 libllmclient.a (静态, llm_chat/llm_chat_raw/llm_chat_stream 需另 -lmbedtls)
 */
#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* 错误码 */
#define LLM_ERR_CONNECT   (-1)   /* 连不上网关 */
#define LLM_ERR_SEND      (-2)   /* 发送失败 */
#define LLM_ERR_NORESP    (-3)   /* 无响应/超时(内部 90s) */
#define LLM_ERR_STATUS    (-4)   /* 网关返回非 200 */
#define LLM_ERR_TOOSMALL  (-5)   /* 输出缓冲区不够(out 内为截断内容) */
#define LLM_ERR_ARGS      (-6)   /* 参数非法 */
#define LLM_ERR_TLS       (-7)   /* TLS 握手失败 */
#define LLM_ERR_MEM       (-8)   /* 内存分配失败 */

#ifndef LLM_GATEWAY_T
#define LLM_GATEWAY_T
typedef struct {
    const char *ip;       /* 网关 IP(连接用, 必填) — 板上 DNS 解析不可用, 只能传 IP */
    const char *vhost;    /* 网关域名(Host 头/SNI 用, 必填, 可与 ip 相同) */
    int port;             /* 端口, 0 = 默认 443 */
    const char *api_key;  /* 客户的 Bearer key(必填) */
    const char *model;    /* 模型名, NULL = "deepseek-ai/DeepSeek-V4-Flash" */
} llm_gateway_t;
#endif

/* 客户主接口: 自带网关+key, 直连 TLS 问答(便捷版)
 * 链路: 板 -> 网关 IP:443 (TLS1.2, SNI/Host=vhost, Bearer=api_key)
 *       -> POST /v1/chat/completions {"model":..,"messages":[{"role":"user","content":msg}]}
 * message : 用户消息(UTF-8, 上限 1KB, 库内做 JSON 转义)
 * out      : 回复缓冲区(正文已从 JSON 提取, 建议 >= 4096)
 * 返回     : >=0 回复长度; 负数见 LLM_ERR_*
 */
int llm_chat(const llm_gateway_t *gw, const char *message, char *out, int out_len);

/* 高阶接口: 客户自带完整 JSON body(多轮对话/system prompt/参数自定义)
 * 链路与 llm_chat 相同; 区别仅在于 JSON 由客户拼、响应不解析:
 * json_body: 客户构造的请求体, 原样发送(库不做转义不做校验), 上限 3KB
 *            须符合网关 /v1/chat/completions 约定, 例:
 *            {"model":"...","messages":[
 *              {"role":"system","content":"..."},
 *              {"role":"user","content":"..."}],"temperature":0.7}
 *            注意: 消息内容里的双引号/反斜杠/换行需客户自行转义(\",\\,\n)
 * out      : 响应 body 原样返回(完整 JSON, 不解析, 建议 >= 8192)
 * 返回     : >=0 响应 body 长度; 负数见 LLM_ERR_*
 */
int llm_chat_raw(const llm_gateway_t *gw, const char *json_body, char *out, int out_len);

/* ============ 流式接口(v1.3): SSE 增量回调 ============ */

/* 流式回调: 每个正文片段(token 级, UTF-8, 已反转义)调用一次
 * token/len: 片段内容(以 NUL 结尾, len 为长度, 可能含 \n 等转义还原后的字符)
 * user     : llm_chat_stream 传入的透传指针
 * 返回 0 继续收; 返回非 0 = 请求中止(库停止读取, 已收内容保留在 out) */
typedef int (*llm_stream_cb)(const char *token, int len, void *user);

/* 流式问答: 请求体自动加 "stream":true, 网关以 SSE 增量推送
 * 链路: 板 -> 网关 IP:443 (TLS1.2, 同 llm_chat) -> 正文片段实时回调 cb
 * 思考过程(reasoning_content)不透传, 只回调最终正文片段;
 * 模型偶发泄漏进 content 的 </think> 边界标记一并过滤(实测 DeepSeek-V4-Flash)
 * cb  : 每个 token 回调一次; 传 NULL = 不要回调(等价非流式但不解析 JSON 细节)
 * out : 完整回复缓冲; 传 NULL = 纯回调模式(返回值仍为正文总长)
 * 返回: >=0 正文总长度(回调中止时 = 已收长度, 部分结果在 out);
 *       负数见 LLM_ERR_*(out 有内容时为截断的已收部分)
 * 超时: 空闲 60s 无数据判失败(LLM_ERR_NORESP); 总时长不设限(非流式是 90s 总预算)
 * 复用: 与 llm_chat/llm_chat_raw 共用 TLS 连接缓存(keep-alive);
 *       正常收完([DONE]+chunked终结)连接保留复用, 中途取消则拆除重建
 */
int llm_chat_stream(const llm_gateway_t *gw, const char *message,
                    llm_stream_cb cb, void *user, char *out, int out_len);

/* 便捷接口: 走 PC 中继(免 key, 有线网即可)
 * llm_set_server 可换中继地址(默认 192.168.1.1:8443)
 */
int llm_ask(const char *msg, char *out, int out_len);
int llm_set_server(const char *ip, int port);

#ifdef __cplusplus
}
#endif
#endif /* LLM_CLIENT_H */
