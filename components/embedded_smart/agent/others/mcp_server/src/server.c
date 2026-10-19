#include "litecrab/json.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MCP_MODERN_VERSION "2026-07-28"
#define MCP_LEGACY_VERSION "2025-11-25"
#define HEADER_LIMIT (16 * 1024)
#define DEFAULT_BODY_LIMIT (256 * 1024)
#define MAX_WORKERS 8
#define CONNECTION_QUEUE 32
#define MAX_SESSIONS 32

typedef struct {
    char method[16];
    char path[256];
    long contentLength;
    char contentType[96];
    char authorization[512];
    char origin[256];
    char protocolVersion[32];
    char mcpMethod[128];
    char mcpName[128];
    char sessionId[128];
    int chunked;
    int seenContentLength, seenAuthorization, seenOrigin, seenProtocol;
    int seenMcpMethod, seenMcpName, seenSession;
} HttpRequest;

typedef struct {
    char listenIp[64];
    int port;
    int workers;
    int maxBodyBytes;
    int legacyOnly;
    char token[256];
    char allowedOrigin[256];
    int listenFd;
    atomic_int running;
    atomic_ullong calls;
    atomic_ullong sessionSequence;
    struct timespec startedAt;
    pthread_t workerThreads[MAX_WORKERS];
    pthread_mutex_t queueMutex;
    pthread_cond_t queueReady;
    int queue[CONNECTION_QUEUE];
    int queueHead, queueTail, queueCount;
    pthread_mutex_t sessionMutex;
    char sessions[MAX_SESSIONS][128];
} DeviceServer;

typedef struct {
    const char* json;
    LjToken* tokens;
    int count;
} JsonDoc;

static DeviceServer* activeServer;

static void copy_text(char* output, size_t size, const char* input) {
    if (output && size)
        snprintf(output, size, "%s", input ? input : "");
}

static int send_all(int fd, const char* data, size_t size) {
    while (size) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return -1;
        data += sent;
        size -= (size_t) sent;
    }
    return 0;
}

static const char* status_text(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 202:
        return "Accepted";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 415:
        return "Unsupported Media Type";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

static int send_response(int fd,
                         int status,
                         const char* body,
                         const char* sessionId,
                         const char* extraHeaders) {
    size_t bodySize = body ? strlen(body) : 0;
    char header[2048];
    int length = snprintf(header,
                          sizeof header,
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Content-Type: application/json\r\n"
                          "Connection: close\r\n"
                          "%s%s%s%s"
                          "\r\n",
                          status,
                          status_text(status),
                          bodySize,
                          sessionId && *sessionId ? "MCP-Session-Id: " : "",
                          sessionId && *sessionId ? sessionId : "",
                          sessionId && *sessionId ? "\r\n" : "",
                          extraHeaders ? extraHeaders : "");
    if (length < 0 || (size_t) length >= sizeof header ||
        send_all(fd, header, (size_t) length))
        return -1;
    return bodySize ? send_all(fd, body, bodySize) : 0;
}

static int parse_header_line(HttpRequest* request, char* line) {
    char* colon = strchr(line, ':');
    if (!colon)
        return -1;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t')
        colon++;
    char* end = colon + strlen(colon);
    while (end > colon && isspace((unsigned char) end[-1]))
        *--end = 0;
    if (!strcasecmp(line, "Content-Length")) {
        if (request->seenContentLength++)
            return -1;
        char* numberEnd = NULL;
        errno = 0;
        long value = strtol(colon, &numberEnd, 10);
        if (errno || !numberEnd || *numberEnd || value < 0)
            return -1;
        request->contentLength = value;
    } else if (!strcasecmp(line, "Content-Type"))
        copy_text(request->contentType, sizeof request->contentType, colon);
    else if (!strcasecmp(line, "Authorization")) {
        if (request->seenAuthorization++)
            return -1;
        copy_text(request->authorization, sizeof request->authorization, colon);
    } else if (!strcasecmp(line, "Origin")) {
        if (request->seenOrigin++)
            return -1;
        copy_text(request->origin, sizeof request->origin, colon);
    } else if (!strcasecmp(line, "MCP-Protocol-Version")) {
        if (request->seenProtocol++)
            return -1;
        copy_text(request->protocolVersion, sizeof request->protocolVersion, colon);
    } else if (!strcasecmp(line, "Mcp-Method")) {
        if (request->seenMcpMethod++)
            return -1;
        copy_text(request->mcpMethod, sizeof request->mcpMethod, colon);
    } else if (!strcasecmp(line, "Mcp-Name")) {
        if (request->seenMcpName++)
            return -1;
        copy_text(request->mcpName, sizeof request->mcpName, colon);
    } else if (!strcasecmp(line, "MCP-Session-Id")) {
        if (request->seenSession++)
            return -1;
        copy_text(request->sessionId, sizeof request->sessionId, colon);
    } else if (!strcasecmp(line, "Transfer-Encoding"))
        request->chunked = 1;
    return 0;
}

static int receive_request(int fd,
                           int maxBodyBytes,
                           HttpRequest* request,
                           char** body,
                           int* errorStatus) {
    *body = NULL;
    *errorStatus = 400;
    memset(request, 0, sizeof *request);
    request->contentLength = -1;
    char* data = malloc(HEADER_LIMIT + 1);
    if (!data)
        return -1;
    size_t used = 0;
    char* headerEnd = NULL;
    while (used < HEADER_LIMIT) {
        ssize_t got = recv(fd, data + used, HEADER_LIMIT - used, 0);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        used += (size_t) got;
        data[used] = 0;
        headerEnd = strstr(data, "\r\n\r\n");
        if (headerEnd)
            break;
    }
    if (!headerEnd) {
        free(data);
        *errorStatus = 413;
        return -1;
    }
    size_t headerBytes = (size_t) (headerEnd + 4 - data);
    *headerEnd = 0;
    char* save = NULL;
    char version[16] = "";
    char* line = strtok_r(data, "\r\n", &save);
    if (!line || sscanf(line, "%15s %255s %15s", request->method, request->path, version) != 3 ||
        (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
        free(data);
        return -1;
    }
    while ((line = strtok_r(NULL, "\r\n", &save)))
        if (parse_header_line(request, line)) {
            free(data);
            return -1;
        }
    if (request->chunked) {
        free(data);
        *errorStatus = 400;
        return -1;
    }
    if (!strcmp(request->method, "POST") && request->contentLength < 0) {
        free(data);
        *errorStatus = 411;
        return -1;
    }
    if (request->contentLength > maxBodyBytes) {
        size_t alreadyReceived = used > headerBytes ? used - headerBytes : 0;
        long remaining = request->contentLength - (long) alreadyReceived;
        char discard[4096];
        /* Drain a bounded rejected body so the peer can reliably receive 413
         * instead of observing a TCP reset caused by unread socket data. */
        long drainLimit = maxBodyBytes * 4L;
        while (remaining > 0 && drainLimit > 0) {
            size_t wanted = (size_t) remaining < sizeof discard ? (size_t) remaining : sizeof discard;
            if ((long) wanted > drainLimit)
                wanted = (size_t) drainLimit;
            ssize_t got = recv(fd, discard, wanted, 0);
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                break;
            remaining -= got;
            drainLimit -= got;
        }
        free(data);
        *errorStatus = 413;
        return -1;
    }
    size_t bodyBytes = request->contentLength > 0 ? (size_t) request->contentLength : 0;
    char* output = calloc(bodyBytes + 1, 1);
    if (!output) {
        free(data);
        *errorStatus = 500;
        return -1;
    }
    size_t already = used > headerBytes ? used - headerBytes : 0;
    if (already > bodyBytes)
        already = bodyBytes;
    if (already)
        memcpy(output, data + headerBytes, already);
    free(data);
    while (already < bodyBytes) {
        ssize_t got = recv(fd, output + already, bodyBytes - already, 0);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            free(output);
            return -1;
        }
        already += (size_t) got;
    }
    output[bodyBytes] = 0;
    *body = output;
    return 0;
}

static int secure_equal(const char* left, const char* right) {
    size_t leftSize = left ? strlen(left) : 0;
    size_t rightSize = right ? strlen(right) : 0;
    size_t count = leftSize > rightSize ? leftSize : rightSize;
    unsigned char difference = (unsigned char) (leftSize ^ rightSize);
    for (size_t i = 0; i < count; i++) {
        unsigned char a = i < leftSize ? (unsigned char) left[i] : 0;
        unsigned char b = i < rightSize ? (unsigned char) right[i] : 0;
        difference |= (unsigned char) (a ^ b);
    }
    return difference == 0;
}

static int authenticate(const DeviceServer* server, const HttpRequest* request) {
    if (!server->token[0])
        return 1;
    char expected[sizeof request->authorization];
    snprintf(expected, sizeof expected, "Bearer %s", server->token);
    return secure_equal(expected, request->authorization);
}

static int parse_doc(const char* json, JsonDoc* doc) {
    memset(doc, 0, sizeof *doc);
    if (!json || LjValidate(json, LJ_OBJECT))
        return -1;
    size_t capacity = strlen(json) / 2 + 64;
    if (capacity > 65536)
        return -1;
    doc->tokens = calloc(capacity, sizeof *doc->tokens);
    if (!doc->tokens)
        return -1;
    LjParser parser;
    LjInit(&parser);
    doc->count = LjParse(&parser, json, strlen(json), doc->tokens, (unsigned) capacity);
    doc->json = json;
    if (doc->count < 1) {
        free(doc->tokens);
        return -1;
    }
    return 0;
}

static void clear_doc(JsonDoc* doc) {
    free(doc->tokens);
    memset(doc, 0, sizeof *doc);
}

static int object_get(const JsonDoc* doc, int object, const char* key) {
    return LjObjectGet(doc->json, doc->tokens, doc->count, object, key);
}

static int string_field(const JsonDoc* doc, int object, const char* key, char* out, size_t size) {
    int value = object_get(doc, object, key);
    return value >= 0 && !LjString(doc->json, &doc->tokens[value], out, size) ? 0 : -1;
}

static int int_field(const JsonDoc* doc, int object, const char* key, int64_t* output) {
    int value = object_get(doc, object, key);
    return value >= 0 ? LjInt64(doc->json, &doc->tokens[value], output) : -1;
}

static int modern_metadata_valid(const JsonDoc* doc) {
    int params = object_get(doc, 0, "params");
    int meta = params >= 0 ? object_get(doc, params, "_meta") : -1;
    char version[32];
    if (params < 0 || doc->tokens[params].type != LJ_OBJECT || meta < 0 ||
        doc->tokens[meta].type != LJ_OBJECT ||
        string_field(doc,
                     meta,
                     "io.modelcontextprotocol/protocolVersion",
                     version,
                     sizeof version) ||
        strcmp(version, MCP_MODERN_VERSION))
        return 0;
    int capabilities = object_get(doc, meta, "io.modelcontextprotocol/clientCapabilities");
    return capabilities >= 0 && doc->tokens[capabilities].type == LJ_OBJECT;
}

static int copy_id(const JsonDoc* doc, char* out, size_t size) {
    int id = object_get(doc, 0, "id");
    if (id < 0)
        return -1;
    int bytes = doc->tokens[id].end - doc->tokens[id].start;
    if (bytes <= 0 || (size_t) bytes >= size)
        return -1;
    if (doc->tokens[id].type == LJ_STRING) {
        LjBuf buffer;
        LjBufInit(&buffer, out, size);
        char decoded[256];
        if (LjString(doc->json, &doc->tokens[id], decoded, sizeof decoded) ||
            LjAppendJsonString(&buffer, decoded))
            return -1;
    } else {
        memcpy(out, doc->json + doc->tokens[id].start, (size_t) bytes);
        out[bytes] = 0;
    }
    return 0;
}

static void build_error(char* out,
                        size_t size,
                        const char* id,
                        int code,
                        const char* message) {
    LjBuf buffer;
    LjBufInit(&buffer, out, size);
    LjAppend(&buffer,
             "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":",
             id && *id ? id : "null",
             code);
    LjAppendJsonString(&buffer, message);
    LjAppend(&buffer, "}}");
}

static void create_session(DeviceServer* server, char* output, size_t size) {
    unsigned long long sequence = atomic_fetch_add(&server->sessionSequence, 1);
    unsigned long long now = (unsigned long long) time(NULL);
    snprintf(output, size, "%llx-%llx-%x", now, sequence, (unsigned) getpid());
    pthread_mutex_lock(&server->sessionMutex);
    snprintf(server->sessions[sequence % MAX_SESSIONS],
             sizeof server->sessions[0],
             "%s",
             output);
    pthread_mutex_unlock(&server->sessionMutex);
}

static int valid_session(DeviceServer* server, const char* session) {
    if (!session || !*session)
        return 0;
    int found = 0;
    pthread_mutex_lock(&server->sessionMutex);
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (secure_equal(server->sessions[i], session)) {
            found = 1;
            break;
        }
    pthread_mutex_unlock(&server->sessionMutex);
    return found;
}

static const char* tools_json(void) {
    return "["
           "{\"name\":\"device.get_status\",\"description\":\"Read device MCP server status\","
           "\"inputSchema\":{\"type\":\"object\",\"properties\":{},"
           "\"additionalProperties\":false}},"
           "{\"name\":\"device.echo\",\"description\":\"Echo a text value on the device\","
           "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"message\":{"
           "\"type\":\"string\",\"maxLength\":1024,\"description\":\"Text to echo\"}},"
           "\"required\":[\"message\"],\"additionalProperties\":false}},"
           "{\"name\":\"device.add\",\"description\":\"Add two signed integers on the device\","
           "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"a\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"}},"
           "\"required\":[\"a\",\"b\"],\"additionalProperties\":false}},"
           "{\"name\":\"device.delay\",\"description\":\"Wait for a bounded test delay\","
           "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"milliseconds\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":5000}},"
           "\"required\":[\"milliseconds\"],"
           "\"additionalProperties\":false}}]";
}

static int build_tool_success(char* out,
                              size_t size,
                              const char* id,
                              const char* text,
                              const char* structuredJson) {
    LjBuf buffer;
    LjBufInit(&buffer, out, size);
    LjAppend(&buffer,
             "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{"
             "\"type\":\"text\",\"text\":",
             id);
    LjAppendJsonString(&buffer, text);
    LjAppend(&buffer,
             "}],\"structuredContent\":%s,\"isError\":false,"
             "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
             "\"name\":\"litecrab-device-mcp\",\"version\":\"1.0.0\"}}}}",
             structuredJson);
    return buffer.failed ? -1 : 0;
}

static int handle_tool_call(DeviceServer* server,
                            const JsonDoc* doc,
                            const char* id,
                            char* output,
                            size_t outputSize) {
    int params = object_get(doc, 0, "params");
    char name[128];
    if (params < 0 || string_field(doc, params, "name", name, sizeof name)) {
        build_error(output, outputSize, id, -32602, "missing tool name");
        return 0;
    }
    int arguments = object_get(doc, params, "arguments");
    if (arguments < 0 || doc->tokens[arguments].type != LJ_OBJECT) {
        build_error(output, outputSize, id, -32602, "arguments must be an object");
        return 0;
    }
    atomic_fetch_add(&server->calls, 1);
    if (!strcmp(name, "device.get_status")) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long uptime = (now.tv_sec - server->startedAt.tv_sec) * 1000LL +
                           (now.tv_nsec - server->startedAt.tv_nsec) / 1000000LL;
        char structured[256], text[256];
        snprintf(structured,
                 sizeof structured,
                 "{\"status\":\"ok\",\"uptimeMs\":%lld,\"calls\":%llu}",
                 uptime,
                 (unsigned long long) atomic_load(&server->calls));
        snprintf(text, sizeof text, "device status ok; uptime=%lldms", uptime);
        return build_tool_success(output, outputSize, id, text, structured);
    }
    if (!strcmp(name, "device.echo")) {
        char message[1025];
        if (string_field(doc, arguments, "message", message, sizeof message)) {
            build_error(output, outputSize, id, -32602, "message is required and <= 1024 bytes");
            return 0;
        }
        char structured[1400];
        LjBuf buffer;
        LjBufInit(&buffer, structured, sizeof structured);
        LjAppend(&buffer, "{\"echo\":");
        LjAppendJsonString(&buffer, message);
        LjAppend(&buffer, "}");
        return buffer.failed ? -1
                             : build_tool_success(output, outputSize, id, message, structured);
    }
    if (!strcmp(name, "device.add")) {
        int64_t a, b;
        if (int_field(doc, arguments, "a", &a) || int_field(doc, arguments, "b", &b) ||
            (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
            build_error(output, outputSize, id, -32602, "a and b must be safe signed integers");
            return 0;
        }
        long long sum = (long long) (a + b);
        char structured[96], text[96];
        snprintf(structured, sizeof structured, "{\"sum\":%lld}", sum);
        snprintf(text, sizeof text, "%lld", sum);
        return build_tool_success(output, outputSize, id, text, structured);
    }
    if (!strcmp(name, "device.delay")) {
        int64_t milliseconds;
        if (int_field(doc, arguments, "milliseconds", &milliseconds) || milliseconds < 0 ||
            milliseconds > 5000) {
            build_error(output, outputSize, id, -32602, "milliseconds must be in range 0..5000");
            return 0;
        }
        struct timespec delay = {.tv_sec = milliseconds / 1000,
                                 .tv_nsec = (milliseconds % 1000) * 1000000};
        while (nanosleep(&delay, &delay) && errno == EINTR)
            ;
        char structured[96], text[96];
        snprintf(structured, sizeof structured, "{\"waitedMs\":%lld}", (long long) milliseconds);
        snprintf(text, sizeof text, "waited %lldms", (long long) milliseconds);
        return build_tool_success(output, outputSize, id, text, structured);
    }
    build_error(output, outputSize, id, -32601, "unknown tool");
    return 0;
}

static int handle_json_rpc(DeviceServer* server,
                           const HttpRequest* request,
                           const char* body,
                           char* output,
                           size_t outputSize,
                           char* responseSession,
                           size_t responseSessionSize,
                           int* responseStatus) {
    JsonDoc doc;
    if (parse_doc(body, &doc)) {
        build_error(output, outputSize, NULL, -32700, "invalid JSON");
        return 0;
    }
    char method[128], id[256] = "null", jsonrpc[16];
    if (string_field(&doc, 0, "jsonrpc", jsonrpc, sizeof jsonrpc) || strcmp(jsonrpc, "2.0")) {
        build_error(output, outputSize, NULL, -32600, "jsonrpc must be 2.0");
        clear_doc(&doc);
        return 0;
    }
    if (string_field(&doc, 0, "method", method, sizeof method)) {
        build_error(output, outputSize, NULL, -32600, "missing method");
        clear_doc(&doc);
        return 0;
    }
    copy_id(&doc, id, sizeof id);
    int modern = !strcmp(request->protocolVersion, MCP_MODERN_VERSION);
    int legacy = !strcmp(request->protocolVersion, MCP_LEGACY_VERSION) ||
                 (!request->protocolVersion[0] && !strcmp(method, "initialize"));
    if (!modern && !legacy) {
        build_error(output, outputSize, id, -32600, "unsupported MCP protocol version");
        clear_doc(&doc);
        return 0;
    }
    if (modern && !modern_metadata_valid(&doc)) {
        build_error(output, outputSize, id, -32600, "missing or invalid per-request MCP metadata");
        clear_doc(&doc);
        return 0;
    }
    if (modern && (!request->mcpMethod[0] || strcmp(request->mcpMethod, method))) {
        build_error(output, outputSize, id, -32600, "Mcp-Method header mismatch");
        clear_doc(&doc);
        return 0;
    }
    if (!strcmp(method, "server/discover")) {
        if (server->legacyOnly) {
            build_error(output, outputSize, id, -32601, "method not found");
        } else {
            snprintf(output,
                     outputSize,
                     "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
                     "\"supportedVersions\":[\"%s\",\"%s\"],"
                     "\"capabilities\":{\"tools\":{}},"
                     "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
                     "\"name\":\"litecrab-device-mcp\",\"version\":\"1.0.0\"}}}}",
                     id,
                     MCP_MODERN_VERSION,
                     MCP_LEGACY_VERSION);
        }
    } else if (server->legacyOnly && modern) {
        build_error(output, outputSize, id, -32600, "modern protocol disabled");
    } else if (!strcmp(method, "initialize")) {
        int params = object_get(&doc, 0, "params");
        char requested[32];
        if (modern || params < 0 ||
            string_field(&doc, params, "protocolVersion", requested, sizeof requested) ||
            strcmp(requested, MCP_LEGACY_VERSION)) {
            build_error(output, outputSize, id, -32602, "unsupported initialize version");
            clear_doc(&doc);
            return 0;
        }
        create_session(server, responseSession, responseSessionSize);
        snprintf(output,
                 outputSize,
                 "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
                 "\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":{}},"
                 "\"serverInfo\":{\"name\":\"litecrab-device-mcp\","
                 "\"version\":\"1.0.0\"}}}",
                 id,
                 MCP_LEGACY_VERSION);
    } else if (!modern && !valid_session(server, request->sessionId)) {
        build_error(output, outputSize, id, -32001, "missing or invalid MCP session");
    } else if (!strcmp(method, "notifications/initialized")) {
        output[0] = 0;
        *responseStatus = 202;
    } else if (!strcmp(method, "tools/list")) {
        snprintf(output,
                 outputSize,
                 "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":%s,"
                 "\"ttlMs\":30000,\"cacheScope\":\"private\"}}",
                 id,
                 tools_json());
    } else if (!strcmp(method, "tools/call")) {
        int params = object_get(&doc, 0, "params");
        char name[128] = "";
        if (params >= 0)
            string_field(&doc, params, "name", name, sizeof name);
        if (modern && (!request->mcpName[0] || strcmp(request->mcpName, name)))
            build_error(output, outputSize, id, -32600, "Mcp-Name header mismatch");
        else
            handle_tool_call(server, &doc, id, output, outputSize);
    } else if (!strcmp(method, "ping")) {
        snprintf(output, outputSize, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}", id);
    } else {
        build_error(output, outputSize, id, -32601, "method not found");
    }
    clear_doc(&doc);
    return 0;
}

static void handle_connection(DeviceServer* server, int fd) {
    struct timeval timeout = {.tv_sec = 15, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    HttpRequest request;
    char* body = NULL;
    int errorStatus = 400;
    if (receive_request(fd, server->maxBodyBytes, &request, &body, &errorStatus)) {
        send_response(fd, errorStatus, "{\"error\":\"invalid HTTP request\"}", NULL, NULL);
        return;
    }
    if (strcmp(request.path, "/mcp")) {
        send_response(fd, 404, "{\"error\":\"not found\"}", NULL, NULL);
        free(body);
        return;
    }
    if (strcmp(request.method, "POST")) {
        send_response(fd,
                      405,
                      "{\"error\":\"only POST is enabled\"}",
                      NULL,
                      "Allow: POST\r\n");
        free(body);
        return;
    }
    if (request.origin[0] &&
        (!server->allowedOrigin[0] || strcmp(server->allowedOrigin, request.origin))) {
        send_response(fd, 403, "{\"error\":\"origin rejected\"}", NULL, NULL);
        free(body);
        return;
    }
    if (!authenticate(server, &request)) {
        send_response(fd,
                      401,
                      "{\"error\":\"unauthorized\"}",
                      NULL,
                      "WWW-Authenticate: Bearer\r\n");
        free(body);
        return;
    }
    if (strncasecmp(request.contentType, "application/json", strlen("application/json"))) {
        send_response(fd, 415, "{\"error\":\"application/json required\"}", NULL, NULL);
        free(body);
        return;
    }
    char* output = calloc((size_t) server->maxBodyBytes + 1, 1);
    if (!output) {
        send_response(fd, 500, "{\"error\":\"out of memory\"}", NULL, NULL);
        free(body);
        return;
    }
    char session[128] = "";
    int responseStatus = 200;
    if (handle_json_rpc(server,
                        &request,
                        body,
                        output,
                        (size_t) server->maxBodyBytes,
                        session,
                        sizeof session,
                        &responseStatus)) {
        send_response(fd, 500, "{\"error\":\"internal error\"}", NULL, NULL);
    } else {
        send_response(fd, responseStatus, output[0] ? output : NULL, session, NULL);
    }
    free(output);
    free(body);
}

static void* worker_main(void* data) {
    DeviceServer* server = data;
    for (;;) {
        pthread_mutex_lock(&server->queueMutex);
        while (!server->queueCount && atomic_load(&server->running))
            pthread_cond_wait(&server->queueReady, &server->queueMutex);
        if (!server->queueCount && !atomic_load(&server->running)) {
            pthread_mutex_unlock(&server->queueMutex);
            break;
        }
        int fd = server->queue[server->queueHead];
        server->queueHead = (server->queueHead + 1) % CONNECTION_QUEUE;
        server->queueCount--;
        pthread_mutex_unlock(&server->queueMutex);
        handle_connection(server, fd);
        close(fd);
    }
    return NULL;
}

static void stop_signal(int signalNumber) {
    (void) signalNumber;
    DeviceServer* server = activeServer;
    if (server) {
        atomic_store(&server->running, 0);
        if (server->listenFd >= 0)
            close(server->listenFd);
    }
}

static int parse_integer(const char* text, int minimum, int maximum, int* output) {
    char* end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !end || *end || value < minimum || value > maximum)
        return -1;
    *output = (int) value;
    return 0;
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--listen IP] [--port PORT] [--workers N] [--max-body BYTES] "
            "[--token-env NAME] [--allowed-origin ORIGIN] [--legacy-only]\n",
            program);
}

static int parse_args(DeviceServer* server, int argc, char** argv) {
    copy_text(server->listenIp, sizeof server->listenIp, "127.0.0.1");
    server->port = 18081;
    server->workers = 4;
    server->maxBodyBytes = DEFAULT_BODY_LIMIT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--legacy-only")) {
            server->legacyOnly = 1;
            continue;
        }
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 1;
        }
        if (i + 1 >= argc)
            return -1;
        if (!strcmp(argv[i], "--listen"))
            copy_text(server->listenIp, sizeof server->listenIp, argv[++i]);
        else if (!strcmp(argv[i], "--port")) {
            if (parse_integer(argv[++i], 1, 65535, &server->port))
                return -1;
        } else if (!strcmp(argv[i], "--workers")) {
            if (parse_integer(argv[++i], 1, MAX_WORKERS, &server->workers))
                return -1;
        } else if (!strcmp(argv[i], "--max-body")) {
            if (parse_integer(argv[++i], 4096, 1024 * 1024, &server->maxBodyBytes))
                return -1;
        } else if (!strcmp(argv[i], "--token-env")) {
            const char* token = getenv(argv[++i]);
            if (!token || !*token || strlen(token) >= sizeof server->token)
                return -1;
            copy_text(server->token, sizeof server->token, token);
        } else if (!strcmp(argv[i], "--allowed-origin"))
            copy_text(server->allowedOrigin, sizeof server->allowedOrigin, argv[++i]);
        else
            return -1;
    }
    if (strcmp(server->listenIp, "127.0.0.1") && strcmp(server->listenIp, "::1") &&
        strcmp(server->listenIp, "localhost") && !server->token[0]) {
        fprintf(stderr, "refusing non-loopback listen without --token-env\n");
        return -1;
    }
    return 0;
}

static int start_listener(DeviceServer* server) {
    char service[16];
    snprintf(service, sizeof service, "%d", server->port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    struct addrinfo* addresses = NULL;
    if (getaddrinfo(server->listenIp, service, &hints, &addresses))
        return -1;
    for (struct addrinfo* address = addresses; address; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0)
            continue;
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
        if (!bind(fd, address->ai_addr, address->ai_addrlen) &&
            !listen(fd, CONNECTION_QUEUE)) {
            server->listenFd = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(addresses);
    return server->listenFd < 0 ? -1 : 0;
}

int main(int argc, char** argv) {
    DeviceServer server;
    memset(&server, 0, sizeof server);
    server.listenFd = -1;
    int parsed = parse_args(&server, argc, argv);
    if (parsed) {
        if (parsed < 0)
            usage(argv[0]);
        return parsed < 0 ? 2 : 0;
    }
    pthread_mutex_init(&server.queueMutex, NULL);
    pthread_cond_init(&server.queueReady, NULL);
    pthread_mutex_init(&server.sessionMutex, NULL);
    atomic_init(&server.running, 1);
    atomic_init(&server.calls, 0);
    atomic_init(&server.sessionSequence, 1);
    clock_gettime(CLOCK_MONOTONIC, &server.startedAt);
    if (start_listener(&server)) {
        perror("MCP server listen");
        return 1;
    }
    activeServer = &server;
    struct sigaction action = {0};
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    for (int i = 0; i < server.workers; i++)
        if (pthread_create(&server.workerThreads[i], NULL, worker_main, &server)) {
            fprintf(stderr, "cannot start worker %d\n", i);
            stop_signal(0);
            server.workers = i;
            break;
        }
    fprintf(stderr,
            "litecrab_device_mcp listening on %s:%d/mcp workers=%d mode=%s auth=%s\n",
            server.listenIp,
            server.port,
            server.workers,
            server.legacyOnly ? "legacy" : "modern+legacy",
            server.token[0] ? "bearer" : "loopback-only");
    while (atomic_load(&server.running)) {
        int fd = accept(server.listenFd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (!atomic_load(&server.running) || errno == EBADF || errno == EINVAL)
                break;
            continue;
        }
        pthread_mutex_lock(&server.queueMutex);
        if (server.queueCount >= CONNECTION_QUEUE) {
            pthread_mutex_unlock(&server.queueMutex);
            send_response(fd, 503, "{\"error\":\"server busy\"}", NULL, NULL);
            close(fd);
            continue;
        }
        server.queue[server.queueTail] = fd;
        server.queueTail = (server.queueTail + 1) % CONNECTION_QUEUE;
        server.queueCount++;
        pthread_cond_signal(&server.queueReady);
        pthread_mutex_unlock(&server.queueMutex);
    }
    atomic_store(&server.running, 0);
    pthread_mutex_lock(&server.queueMutex);
    pthread_cond_broadcast(&server.queueReady);
    pthread_mutex_unlock(&server.queueMutex);
    for (int i = 0; i < server.workers; i++)
        pthread_join(server.workerThreads[i], NULL);
    while (server.queueCount) {
        close(server.queue[server.queueHead]);
        server.queueHead = (server.queueHead + 1) % CONNECTION_QUEUE;
        server.queueCount--;
    }
    pthread_mutex_destroy(&server.sessionMutex);
    pthread_cond_destroy(&server.queueReady);
    pthread_mutex_destroy(&server.queueMutex);
    activeServer = NULL;
    return 0;
}
