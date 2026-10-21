#ifndef AGENT_SERVICE_H
#define AGENT_SERVICE_H
/*
 * agent_service.h - LLM 智能体编排服务(能力上报 + 格式约束对话 + 功能分发)
 * 依赖: llm_service(LlmSvcChat 带输出缓冲问答) + servo_service(ServoEnsureInit/
 *       OttoPcaPost 动作) + ui_display_sdk(表情: 独立进程, 文件通道转发)
 *
 * 功能: 串口文字 -> 大模型 -> 按行标记协议回复 -> 解析分发:
 *       [SAY] 打印 / [EMOJI] 写表情命令文件给 ui_display / [ACT] 后台执行舵机动作
 * 协议与能力清单文档: docs/能力清单与LLM协议.md
 */

/* ============【智能体配置区】============ */
#define AGENT_CFG_EMOJI_CMD_FILE "/storage/data/emoji_cmd" /* -> ui_display 表情命令通道 */
#define AGENT_CFG_PROMPT_MAX 4096   /* 组包缓冲(raw 通道 body 上限 3072B, 超出截尾部用户文本) */
#define AGENT_CFG_REPLY_MAX  4096   /* 回复缓冲(与 llm_service 一致) */
#define AGENT_CFG_ACT_ARG_MAX 4     /* [ACT] 行最多附加整数参数 */
#define AGENT_CFG_ACTQ_SIZE  8      /* 动作排队上限(多个 [ACT] 顺序执行) */
#define AGENT_CFG_NAME_MAX   24     /* 表情/动作名上限 */

/* 首次连云上报能力(内部只发一次, 成功后置标志; 返回 0=已上报) */
int AgentSvcAnnounce(void);

/* 格式约束对话: 能力prompt+文字 -> LLM -> 解析[EMOJI]/[ACT]/[SAY] -> 分发 */
int AgentSvcChat(const char *text);

/*
 * 语音链路专用对话(三段式 voice_dialog 的 agent 段): 行为同 AgentSvcChat,
 * 额外输出(均可传 NULL 忽略):
 *   raw_out/raw_cap: LLM 原始回复全文(未解析, 供串口打印/人工检查)
 *   say_out/say_cap: [SAY] 行内容(无则空串, 供 TTS 播报)
 * [EMOJI]/[ACT] 仍在函数内按能力表校验分发, 与串口 chat 同一规则。
 * 返回 >=0 回复长度, <0 失败。
 */
int AgentSvcChatVoice(const char *text, char *raw_out, int raw_cap,
                      char *say_out, int say_cap);

/* ---- 语音/云端指令分发接口(voice_dialog 的 FC 路径用) ----
 * 与串口 chat 的 [EMOJI]/[ACT] 行同一套处理: 能力表校验 -> 表情文件通道 /
 * 动作队列(含 NormalizeAct ±1 方向桥接), 保证"云端指令解析"与文字链路一致 */
int AgentSvcDispatchEmoji(const char *name);                          /* 0=执行 -1=忽略 */
int AgentSvcDispatchAct(const char *name, const int *par, int npar);  /* 0=入队 -1=忽略 */
/* 原始入队: par 直接按 otto_pca.h Ex 形参序传, 不做 NormalizeAct ±1 桥接;
 * 供 agent 工具桥按底层签名完整构造参数(walk/turn steps=1 时不受 ±1 启发式影响)。
 * 名字不在能力表也放行(能力表外的 SDK 动作如 home/showcase 也能走) */
int AgentSvcEnqueueRaw(const char *name, const int *par, int npar);   /* 0=入队 -1=忽略 */
/* 完整急停: 清空动作队列 + 中止当前动作并归位(agent 工具桥 robot.stop 用) */
int AgentSvcStopAll(void);                                            /* 0=已处理 */
int AgentSvcEmojiNames(char *dst, int size);   /* 表情名"a,b,c"(拼云端工具枚举) */
int AgentSvcActNames(char *dst, int size);     /* 动作名"a,b,c" */

/* ---- shell 指令入口(主程序命令表绑定) ---- */
int AgentCmdChat(int argc, char **argv);    /* chat <文字...>            */
int AgentCmdChatU(int argc, char **argv);   /* chatu <码点...> 中文对话   */

#endif /* AGENT_SERVICE_H */
