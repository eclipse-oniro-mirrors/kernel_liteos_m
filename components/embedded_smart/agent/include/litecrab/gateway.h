#ifndef LITECRAB_GATEWAY_H
#define LITECRAB_GATEWAY_H
#include <stddef.h>
typedef struct {
    int connFd;
    int requestTimeoutMs;
    char sessionId[64];
    char userId[64];
    char lastSessionId[64]; /* 最近一次请求的目标会话（响应信封装配 interrupt 用） */
    void* user;
} ConnectionContext;
typedef int (*RequestHandlerFn)(const char*, char*, int, ConnectionContext*);
typedef struct {
    char listenIp[64];
    int listenPort, backlog, recvTimeoutMs, sendTimeoutMs, maxReqBytes;
    int workerThreads, maxConnections;
    int allowUnauthenticatedRemote;
} RequestServerConfig;
int RequestServerValidateBindPolicy(const RequestServerConfig* config,
                                    char* error,
                                    size_t errorSize);
int StartRequestServer(const RequestServerConfig*, RequestHandlerFn, void*);
void RequestServerRequestStop(void);
void RequestServerResetStop(void);
int LiteCrabHandleNetworkRequest(const char*, char*, int, ConnectionContext*);
#endif
