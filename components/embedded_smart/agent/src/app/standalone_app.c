#include "litecrab/standalone_app.h"

#include "litecrab/approval.h"
#include "litecrab/autoconf.h"
#include "litecrab/capability.h"
#include "litecrab/config.h"
#include "litecrab/gateway.h"
#include "litecrab/observability.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LITECRAB_BUILD_REVISION
#define LITECRAB_BUILD_REVISION "unknown"
#endif
#ifndef LITECRAB_BUILD_TIME_UTC
#define LITECRAB_BUILD_TIME_UTC "unknown"
#endif

static volatile sig_atomic_t standaloneStopRequested;

static void stop_server(int signalNumber) {
    (void) signalNumber;
    standaloneStopRequested = 1;
#ifdef CONFIG_LITECRAB_CAP_TCP_GATEWAY
    RequestServerRequestStop();
#endif
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--config FILE] [--llm-config FILE] [--llm-provider NAME] "
            "[--workspace DIR] [--port PORT] [--alarm-mode polling|subscription] "
            "[--alarm-base-url URL] [--alarm-user USER] [--alarm-password PWD] "
            "[--alarm-ca-file FILE] [--alarm-insecure-tls] [--alarm|--no-alarm]\n",
            program);
}

static int parse_port(const char* text, int* output) {
    char* end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !end || *end || value < 1 || value > 65535)
        return -1;
    *output = (int) value;
    return 0;
}

static int parse_arguments(int argc,
                           char** argv,
                           const char** baseConfig,
                           const char** llmConfig,
                           const char** llmProvider,
                           const char** workspace,
                           const char** portText,
                           AlarmAppOptions* alarmOptions) {
    for (int index = 1; index < argc; index++) {
#ifdef CONFIG_LITECRAB_CAP_ALARM
        int alarmArgument = AlarmAppParseArg(alarmOptions, argc, argv, &index);
        if (alarmArgument < 0)
            return -1;
        if (alarmArgument > 0)
            continue;
#else
        (void) alarmOptions;
#endif
        if (!strcmp(argv[index], "--help"))
            return 1;
        if ((!strcmp(argv[index], "--config") || !strcmp(argv[index], "--llm-config") ||
             !strcmp(argv[index], "--llm-provider") || !strcmp(argv[index], "--workspace") ||
             !strcmp(argv[index], "--port")) &&
            index + 1 >= argc)
            return -1;
        if (!strcmp(argv[index], "--config"))
            *baseConfig = argv[++index];
        else if (!strcmp(argv[index], "--llm-config"))
            *llmConfig = argv[++index];
        else if (!strcmp(argv[index], "--llm-provider"))
            *llmProvider = argv[++index];
        else if (!strcmp(argv[index], "--workspace"))
            *workspace = argv[++index];
        else if (!strcmp(argv[index], "--port"))
            *portText = argv[++index];
        else if (argv[index][0] == '-')
            return -1;
        else if (!*workspace)
            *workspace = argv[index];
        else if (!*portText)
            *portText = argv[index];
        else
            return -1;
    }
    return 0;
}

int LiteCrabStandaloneRun(int argc, char** argv) {
    const char *baseConfig = NULL, *llmConfig = NULL, *llmProvider = NULL, *workspace = NULL,
               *portText = NULL;
    LiteCrabContext context;
    memset(&context, 0, sizeof context);
    atomic_init(&context.gatewayThreadStarted, 0);
    atomic_init(&context.gatewayFinished, 0);
#ifdef CONFIG_LITECRAB_CAP_ALARM
    AlarmAppDefaults(&context.alarmOptions);
#endif
    int argumentResult = parse_arguments(argc,
                                         argv,
                                         &baseConfig,
                                         &llmConfig,
                                         &llmProvider,
                                         &workspace,
                                         &portText,
                                         &context.alarmOptions);
    if (argumentResult) {
        usage(argv[0]);
        return argumentResult > 0 ? 0 : 2;
    }
    char error[512];
    if (LiteCrabConfigLoadProvider(
            &context.config, baseConfig, llmConfig, llmProvider, error, sizeof error)) {
        fprintf(stderr, "configuration error: %s\n", error);
        return 2;
    }
    if (workspace)
        snprintf(context.config.workspaceRoot, sizeof context.config.workspaceRoot, "%s", workspace);
    /* 审批存储（安全计划第 10/11 步）：先于一切能力初始化，失败按配置错误退出 */
    if (CrabApprovalInit(context.config.workspaceRoot, context.config.approval.timeoutMs)) {
        fprintf(stderr, "approval store initialization failed\n");
        return 2;
    }
    {
        const char* excludes[LITECRAB_APPROVAL_MAX_EXCLUDE];
        for (int i = 0; i < context.config.approval.excludeCount; i++)
            excludes[i] = context.config.approval.excludeTools[i];
        AgentLoopConfigureApproval(context.config.approval.enabled,
                                   excludes,
                                   context.config.approval.excludeCount);
        LogPrint("[security] interactive approval %s timeoutMs=%d excludeCount=%d",
                 context.config.approval.enabled ? "enabled" : "disabled",
                 context.config.approval.timeoutMs,
                 context.config.approval.excludeCount);
    }
#ifdef CONFIG_LITECRAB_CAP_ALARM
    if (!context.alarmOptions.config.spoolDirectory[0] &&
        snprintf(context.alarmOptions.config.spoolDirectory,
                 sizeof context.alarmOptions.config.spoolDirectory,
                 "%s/.crab/alarm",
                 context.config.workspaceRoot) >=
            (int) sizeof context.alarmOptions.config.spoolDirectory) {
        fprintf(stderr, "Alarm spool path is too long\n");
        return 2;
    }
#endif
    if (portText && parse_port(portText, &context.config.server.listenPort)) {
        fprintf(stderr, "invalid port: %s\n", portText);
        return 2;
    }
#ifndef CONFIG_LITECRAB_CAP_MCP_CLIENT
    if (context.config.mcp.enabled) {
        fprintf(stderr, "MCP is enabled in config but omitted from this build\n");
        return 1;
    }
#endif
    context.runtimeConfig = (CrabRuntimeConfig){.workspaceRoot = context.config.workspaceRoot,
                                                .rawResultDir = ".crab/raw"};
    size_t capabilityCount = 0;
    const LiteCrabCapabilityOps* const* capabilities =
        LiteCrabGeneratedCapabilityRegistry(&capabilityCount);
    LiteCrabCapabilityManager manager;
    if (LiteCrabCapabilityManagerInit(&manager, &context, capabilities, capabilityCount)) {
        const char* failed = LiteCrabCapabilityManagerFailedCapability(&manager);
        if (failed && !strcmp(failed, "memory"))
            fprintf(stderr, "failed to initialize LiteCrab\n");
        fprintf(stderr,
                "capability initialization failed: %s\n",
                failed ? failed : "registry");
        return 1;
    }
    LogPrint("[startup] revision=%s build_time_utc=%s product=%s workspace=%s "
             "limits={connections:%d,workers:%d,request_bytes:%d}",
             LITECRAB_BUILD_REVISION,
             LITECRAB_BUILD_TIME_UTC,
             CONFIG_LITECRAB_PRODUCT_ID,
             context.config.workspaceRoot,
             context.config.server.maxConnections,
             context.config.server.workerThreads,
             context.config.server.maxReqBytes);
    struct sigaction action = {0};
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    standaloneStopRequested = 0;
#ifdef CONFIG_LITECRAB_CAP_TCP_GATEWAY
    if (context.config.server.allowUnauthenticatedRemote)
        LogPrint("[security] WARNING unauthenticated remote TCP is explicitly enabled; "
                 "request userId is untrusted; bind=%s:%d",
                 context.config.server.listenIp,
                 context.config.server.listenPort);
    else
        LogPrint("[security] TCP gateway has no client authentication; request userId is "
                 "untrusted; exposure=loopback bind=%s:%d",
                 context.config.server.listenIp,
                 context.config.server.listenPort);
    LogPrint("[gateway] LiteCrab starting on %s:%d",
             context.config.server.listenIp,
             context.config.server.listenPort);
#endif
    if (LiteCrabCapabilityManagerStart(&manager)) {
        fprintf(stderr,
                "capability startup failed: %s\n",
                LiteCrabCapabilityManagerFailedCapability(&manager));
        return 1;
    }
    while (!standaloneStopRequested) {
        if (atomic_load(&context.gatewayFinished))
            break;
        struct timespec interval = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&interval, NULL);
    }
    int result = context.gatewayResult;
    if (LiteCrabCapabilityManagerShutdown(&manager))
        result = -1;
    CrabApprovalDestroy();
    return result ? 1 : 0;
}
