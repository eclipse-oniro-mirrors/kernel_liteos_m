#ifndef LLM_SERVICE_H
#define LLM_SERVICE_H
/*
 * llm_service.h - LLM 云端问答服务
 * 依赖: llmclient_stream v1.3(llm_chat_stream 流式 / llm_chat_raw 长消息)
 *       + wifi_service(在线检查)
 *
 * 后续接入方法: include 本头文件, 调 LlmSvcAsk 即可(<=1KB 流式打字机,
 * 超限自动转 raw 非流式通道);
 * shell 新指令在 llm_service.c 加 LlmCmdXxx, 声明放这里, 主程序命令表绑定。
 */

/* ============【LLM 配置区】============ */
#define LLM_CFG_IP        "47.102.215.139"              /* 网关 IP */
#define LLM_CFG_VHOST     "api.siliconflow.cn"          /* TLS SNI/Host */
#define LLM_CFG_PORT      443
#define LLM_CFG_KEY       "sk-jcojwurdcgxlgtrgaiuibttmopjzzxxrmnrfoieawqpkqwrj"
#define LLM_CFG_MODEL     "deepseek-ai/DeepSeek-V4-Flash"
#define LLM_CFG_GREETING  "您好, 请简单自我介绍并确认收到消息"
#define LLM_CFG_ASK_ON_RECOVER 1                        /* WiFi 恢复后自动重新问答 */
#define LLM_CFG_REPLY_MAX 4096                           /* 回复缓冲 */

/* 内置问候(启动/掉线恢复时由监控线程回调); 问答互斥, 忙时返回 -99 */
int LlmSvcGreet(void);

/* 问答(ASCII); 问答互斥, 忙时返回 -99 */
int LlmSvcAsk(const char *msg);

/* 带输出缓冲问答(供 agent 等需要解析回复的模块; 复用同一互斥/监控暂停);
 * 返回 >=0 回复长度, 负数见 LLM_ERR_* */
int LlmSvcChat(const char *msg, char *out, int out_len);

/* 参数列表(hex 码点词) -> UTF-8 文本(uni/chatu 共用); 0=成功(至少 1 字节) */
int LlmSvcUtf8FromArgs(int argc, char **argv, char *out, int out_size);

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int LlmCmdAsk(int argc, char **argv);   /* ask <内容...>          */
int LlmCmdUni(int argc, char **argv);   /* uni <4位hex码点...> 中文 */

#endif /* LLM_SERVICE_H */
