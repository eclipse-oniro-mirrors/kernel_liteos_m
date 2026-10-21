#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H
/*
 * app_console.h - 串口控制台与 shell 小工具(行读取/分词/数字解析)
 * 供主程序与各服务模块共用; 不含任何业务逻辑。
 */

/* ============【控制台配置区】============ */
#define CONSOLE_LINE_MAX 512    /* 单行输入缓冲 */
#define CONSOLE_ARGV_MAX 16     /* 单行最多参数个数 */

/* 阻塞读一行(回显/退格由内核 ICANON 负责); 返回行长, buf 以 '\0' 结尾 */
int ConsoleReadline(const char *prompt, char *buf, int size);

/* 按空白切分(原地修改 line); 返回参数个数 argc */
int ShellSplit(char *line, char **argv, int maxv);

/* 严格十进制解析("12"可, "12x"不可); 0=成功 */
int ShellParseInt(const char *s, int32_t *val);

#endif /* APP_CONSOLE_H */
