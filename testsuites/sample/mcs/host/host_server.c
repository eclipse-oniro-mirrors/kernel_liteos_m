/*
 * host_server.c — mcs 测试 host 主服务（2026-11-09 新架构，替代 v4 mcs_host）
 *
 * 三线程模型：
 *   主服务（本文件）：AF_UNIX socket 监听（板内），收客户端命令行，
 *                    起测试线程（互斥：一次一个）
 *   被动服务线程：np/PF/echo 等从核用例配合服务（host_passive.c，常驻）
 *   测试线程：五步生命周期 create->start->回调->stop->delete；
 *            回调两类：E2e（host_e2e.c）/ 从核用例调度（host_slave.c）
 *
 * 客户端：板上小工具 mcs_cmd（传参一次 -> socket -> 退）。
 * 命令格式（一行文本）：
 *   run <module> <index>    跑从核单用例（1-based）
 *   run <module> all        跑从核模块全部
 *   run all                 从核全部模块
 *   e2e <McsE2eNNN>         跑主核 E2e 用例（阶段 C2 接入）
 *   stop                    停当前执行（先协议 STOP，当前用例后停）
 *   status                  状态
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include "host_slave.h"

/* 段错误现场打印（v4 同款间歇崩溃的定位手段） */
#include <ucontext.h>
static void on_host_fatal(int sig, siginfo_t *si, void *uc)
{
    ucontext_t *u = uc;
    (void)sig;
    fprintf(stderr, "[host][FATAL] sig=%d addr=%p pc=0x%llx sp=0x%llx\n",
            sig, si->si_addr,
            (unsigned long long)u->uc_mcontext.pc,
            (unsigned long long)u->uc_mcontext.sp);
    _exit(99);
}

#define HOST_SOCK_PATH "/data/mcs_test.sock"
#define DEFAULT_FW     "/data/liteos-m.elf"

/* ── 测试线程（单实例互斥）────────────────────────────── */
static pthread_t g_test_tid;
static volatile int g_test_running;
static volatile int g_test_want_stop;

/* 被杀兜底：cleanup 例程保证 stop+delete（用户要求：执行完和被杀死都要走） */
static void test_thread_cleanup(void *arg)
{
    struct slave_ctx *s = arg;
    printf("[host] test thread cleanup: stop+delete slave\n");
    slave_stop_delete(s);
    g_test_running = 0;
}

struct test_arg {
    int type;                 /* 0=从核用例调度 1=E2e（C2） */
    char module[12];
    uint32_t index;           /* 0=all */
    int run_all;
    char fw[256];
};

static void *test_thread_fn(void *arg)
{
    struct test_arg *a = arg;
    struct slave_ctx *s;
    struct mcs_test_msg msg, rsp;
    int ret;

    s = slave_create_start(a->fw, 30);
    if (!s) {
        printf("[host] slave create/start failed\n");
        g_test_running = 0;
        free(a);
        return NULL;
    }
    pthread_cleanup_push(test_thread_cleanup, s);

    memset(&msg, 0, sizeof(msg));
    msg.cmd = a->run_all ? MCS_TEST_CMD_RUN_ALL :
              (a->index ? MCS_TEST_CMD_RUN_CASE : MCS_TEST_CMD_RUN_MODULE);
    snprintf(msg.module, sizeof(msg.module), "%s", a->module);
    msg.index = a->index;

    printf("[host] RUN cmd=%d module=%s index=%u -> slave\n", msg.cmd, msg.module, msg.index);
    ret = slave_cmd_wait_ack(s, &msg, 5000);
    if (ret != 0) {
        printf("[host] ACK fail/timeout: %d\n", ret);
        goto out;
    }
    printf("[host] ACK: status=%d\n", msg.status);

    /* 等 DONE：无限等（用例时长不定）；外部 stop 走协议 STOP 帧 */
    ret = slave_wait_done(s, &rsp, -1);
    if (ret == 0) {
        printf("[host] DONE: total=%u failed=%u\n", rsp.total, rsp.failed);
    }
out:
    pthread_cleanup_pop(1);
    free(a);
    return NULL;
}

/* boot-only 线程：拉核即返回（从核固件自跑批；测试结束由串口 Test End 检测） */
static void *boot_thread_fn(void *arg)
{
    struct test_arg *a = arg;
    struct slave_ctx *s;

    s = slave_create_start(a->fw, 30);
    if (!s) {
        printf("[host] slave create/start failed\n");
        g_test_running = 0;
        free(a);
        return NULL;
    }
    printf("[host] slave core up; firmware batch self-running (form A)\n");
    /* 挂住 ctx 不 teardown：测试期从核在跑；板 reboot 时一并回收 */
    sleep(86400);
    return NULL;
}

/* ── 被动服务线程（阶段 C2 从 mcs_test_server 移植；先占位）────── */
extern int host_passive_start(void); /* host_passive.c；无则弱桩 */

__attribute__((weak)) int host_passive_start(void)
{
    printf("[host] passive service: stub (stage C2)\n");
    return 0;
}

/* ── 命令解析与调度 ───────────────────────────────────── */
static void reply(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (fd >= 0) {
        (void)write(fd, buf, strlen(buf));
    }
    printf("[host] rsp: %s", buf);
}

static void handle_conn(int fd, const char *fw)
{
    char line[256];
    ssize_t n;
    char mod[16] = "";
    char arg2[32] = "";

    n = read(fd, line, sizeof(line) - 1);
    if (n <= 0) {
        return;
    }
    line[n] = '\0';
    line[strcspn(line, "\r\n")] = '\0';
    printf("[host] cmd: %s\n", line);

    if (strncmp(line, "status", 6) == 0) {
        reply(fd, "state=%s\n", g_test_running ? "RUNNING" : "IDLE");
        return;
    }
    if (strncmp(line, "stop", 4) == 0) {
        if (g_test_running) {
            g_test_want_stop = 1;
            reply(fd, "OK stop requested\n");
        } else {
            reply(fd, "OK idle\n");
        }
        return;
    }
    if (strncmp(line, "run ", 4) == 0) {
        struct test_arg *a;
        int cnt = sscanf(line + 4, "%15s %31s", mod, arg2);
        if (cnt < 1) {
            reply(fd, "ERR usage: run <module> <index|all> | run all\n");
            return;
        }
        if (g_test_running) {
            reply(fd, "ERR busy\n");
            return;
        }
        a = calloc(1, sizeof(*a));
        if (!a) {
            reply(fd, "ERR nomem\n");
            return;
        }
        snprintf(a->fw, sizeof(a->fw), "%s", fw);
        if (strcmp(mod, "all") == 0 && (cnt < 2 || arg2[0] == '\0')) {
            a->run_all = 1;
        } else {
            snprintf(a->module, sizeof(a->module), "%s", mod);
            if (cnt >= 2 && strcmp(arg2, "all") == 0) {
                a->index = 0;
            } else if (cnt >= 2) {
                a->index = (uint32_t)atoi(arg2);
            } else {
                a->index = 0;
            }
        }
        g_test_running = 1;
        g_test_want_stop = 0;
        if (pthread_create(&g_test_tid, NULL, test_thread_fn, a) != 0) {
            g_test_running = 0;
            free(a);
            reply(fd, "ERR thread\n");
            return;
        }
        pthread_detach(g_test_tid);
        reply(fd, "OK started\n");
        return;
    }
    reply(fd, "ERR unknown cmd\n");
}

int main(int argc, char **argv)
{
    const char *fw = DEFAULT_FW;
    const char *sock = HOST_SOCK_PATH;
    int srv, cli;
    struct sockaddr_un addr;
    int i;
    int boot_only = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            fw = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sock = argv[++i];
        } else if (strcmp(argv[i], "-boot") == 0) {
            /* 形态 A：裸拉核——从核固件自带批跑测试（ItSuiteDrivers+TestKernel），
             * 拉起后自动跑至 Test End，无需 host 发调度命令 */
            boot_only = 1;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0); /* nohup 重定向下行缓冲，日志实时 */
    printf("[host] mcs host_server start: fw=%s sock=%s\n", fw, sock);

    host_passive_start();

    unlink(sock);
    srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 4) != 0) {
        perror("listen");
        return 1;
    }
    printf("[host] listening on %s\n", sock);

    if (boot_only) {
        struct test_arg *a = calloc(1, sizeof(*a));

        if (!a) {
            return 1;
        }
        snprintf(a->fw, sizeof(a->fw), "%s", fw);
        a->type = 2; /* boot-only：测试线程只 create_start（拉核），不发调度命令；
                      * 从核固件（形态 A）自跑全量批至 Test End */
        printf("[host] boot-only: pulling slave core, firmware self-tests...\n");
        g_test_running = 1;
        if (pthread_create(&g_test_tid, NULL, boot_thread_fn, a) != 0) {
            g_test_running = 0;
            return 1;
        }
        pthread_detach(g_test_tid);
    }

    signal(SIGPIPE, SIG_IGN);
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_host_fatal;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL); /* umt mmap 写崩溃为 SIGBUS（signal 7）——
                                         2026-11-13 实测无尸检，补钩 */
    }
    for (;;) {
        cli = accept(srv, NULL, NULL);
        if (cli < 0) {
            perror("accept (ignored)");
            sleep(1); /* mica 库内部信号可能打断 accept：不退出，服务常驻 */
            continue;
        }
        handle_conn(cli, fw);
        close(cli);
    }
    /* not reached */
}
