/*
 * app_test.c - agent_test 程序入口(仅 main 壳, 只函数调用, 无变量定义/宏)
 * 用法: 开机由 init 自动常驻拉起(init.cfg: "app_test boot"), 无需串口操作;
 *       手动 exec /bin/app_test 进交互控制台(单实例保护, 已有常驻实例会提示退出)。
 * 实现: 配置宏/命令表/启动编排见 main/app_main.h 与 main/app_main.c,
 *       各业务模块接口见 service 目录各头文件(配置区在各头文件)。
 */
#include "app_main.h"

int main(int argc, char **argv)
{
    return AppMainRun(argc, argv);
}
