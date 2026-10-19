#include "mcp_internal.h"

#include "litecrab/json.h"

#include <curl/curl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    McpHttpResponse* response;
    size_t limit;
} WriteContext;

static size_t write_body(char* data, size_t size, size_t count, void* userData) {
    WriteContext* context = userData;
    size_t bytes = size * count;
    McpHttpResponse* response = context->response;
    if (bytes > context->limit - response->bodySize) {
        response->overflow = 1;
        return 0;
    }
    char* next = realloc(response->body, response->bodySize + bytes + 1);
    if (!next)
        return 0;
    response->body = next;
    memcpy(response->body + response->bodySize, data, bytes);
    response->bodySize += bytes;
    response->body[response->bodySize] = 0;
    return bytes;
}

static int header_value(const char* line, size_t bytes, const char* name, char* out, size_t outSize) {
    size_t nameSize = strlen(name);
    if (bytes <= nameSize + 1 || strncasecmp(line, name, nameSize) || line[nameSize] != ':')
        return 0;
    const char* begin = line + nameSize + 1;
    const char* end = line + bytes;
    while (begin < end && isspace((unsigned char) *begin))
        begin++;
    while (end > begin && isspace((unsigned char) end[-1]))
        end--;
    size_t length = (size_t) (end - begin);
    if (!out || !outSize || length >= outSize)
        return -1;
    memcpy(out, begin, length);
    out[length] = 0;
    return 1;
}

static size_t write_header(char* data, size_t size, size_t count, void* userData) {
    McpHttpResponse* response = userData;
    size_t bytes = size * count;
    header_value(data, bytes, "Content-Type", response->contentType, sizeof response->contentType);
    header_value(data, bytes, "MCP-Session-Id", response->sessionId, sizeof response->sessionId);
    return bytes;
}

static void set_error(char* error, size_t size, const char* text) {
    if (error && size)
        snprintf(error, size, "%s", text ? text : "MCP HTTP failure");
}

static int is_json_rpc_response(const char* json) {
    LjToken tokens[128];
    LjParser parser;
    LjInit(&parser);
    int count = LjParse(&parser, json, strlen(json), tokens, 128);
    if (count < 1 || tokens[0].type != LJ_OBJECT)
        return 0;
    int id = LjObjectGet(json, tokens, count, 0, "id");
    int result = LjObjectGet(json, tokens, count, 0, "result");
    int rpcError = LjObjectGet(json, tokens, count, 0, "error");
    return id >= 0 && (result >= 0 || rpcError >= 0);
}

static int select_sse_json(McpHttpResponse* response, size_t limit) {
    const char* cursor = response->body;
    char* event = calloc(limit + 1, 1);
    char* selected = NULL;
    size_t eventSize = 0;
    if (!event)
        return -1;
    while (cursor && *cursor) {
        const char* end = strchr(cursor, '\n');
        size_t length = end ? (size_t) (end - cursor) : strlen(cursor);
        if (length && cursor[length - 1] == '\r')
            length--;
        if (!length) {
            if (eventSize && is_json_rpc_response(event)) {
                char* next = strdup(event);
                if (!next) {
                    free(selected);
                    free(event);
                    return -1;
                }
                free(selected);
                selected = next;
            }
            eventSize = 0;
            event[0] = 0;
        } else if (length >= 5 && !memcmp(cursor, "data:", 5)) {
            const char* value = cursor + 5;
            size_t valueSize = length - 5;
            if (valueSize && *value == ' ') {
                value++;
                valueSize--;
            }
            if (eventSize + valueSize + 2 > limit) {
                free(selected);
                free(event);
                return -1;
            }
            if (eventSize)
                event[eventSize++] = '\n';
            memcpy(event + eventSize, value, valueSize);
            eventSize += valueSize;
            event[eventSize] = 0;
        }
        cursor = end ? end + 1 : NULL;
    }
    if (eventSize && is_json_rpc_response(event)) {
        char* next = strdup(event);
        if (!next) {
            free(selected);
            free(event);
            return -1;
        }
        free(selected);
        selected = next;
    }
    free(event);
    if (!selected)
        return -1;
    free(response->body);
    response->body = selected;
    response->bodySize = strlen(selected);
    snprintf(response->contentType, sizeof response->contentType, "application/json");
    return 0;
}

void McpHttpResponseClear(McpHttpResponse* response) {
    if (response) {
        free(response->body);
        memset(response, 0, sizeof *response);
    }
}

int McpHttpPost(const McpDeviceConfig* device,
                const McpClientConfig* config,
                const char* bearerToken,
                const char* protocolVersion,
                const char* method,
                const char* resourceName,
                const char* sessionId,
                const char* body,
                McpHttpResponse* response,
                char* error,
                size_t errorSize) {
    if (!device || !config || !method || !body || !response)
        return -1;
    memset(response, 0, sizeof *response);
    CURL* curl = curl_easy_init();
    if (!curl) {
        set_error(error, errorSize, "cannot initialize libcurl");
        return -1;
    }
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    char line[512];
    if (protocolVersion && *protocolVersion) {
        snprintf(line, sizeof line, "MCP-Protocol-Version: %s", protocolVersion);
        headers = curl_slist_append(headers, line);
    }
    snprintf(line, sizeof line, "Mcp-Method: %s", method);
    headers = curl_slist_append(headers, line);
    if (resourceName && *resourceName) {
        snprintf(line, sizeof line, "Mcp-Name: %s", resourceName);
        headers = curl_slist_append(headers, line);
    }
    if (sessionId && *sessionId) {
        snprintf(line, sizeof line, "MCP-Session-Id: %s", sessionId);
        headers = curl_slist_append(headers, line);
    }
    if (bearerToken && *bearerToken) {
        size_t needed = strlen(bearerToken) + 32;
        char* auth = malloc(needed);
        if (!auth) {
            set_error(error, errorSize, "out of memory building MCP authorization");
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return -1;
        }
        snprintf(auth, needed, "Authorization: Bearer %s", bearerToken);
        headers = curl_slist_append(headers, auth);
        memset(auth, 0, needed);
        free(auth);
    }
    WriteContext context = {.response = response, .limit = (size_t) config->maxMessageBytes};
    curl_easy_setopt(curl, CURLOPT_URL, device->url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) strlen(body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long) config->connectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long) config->requestTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);
    if (device->caFile[0])
        curl_easy_setopt(curl, CURLOPT_CAINFO, device->caFile);
    if (device->clientCertFile[0])
        curl_easy_setopt(curl, CURLOPT_SSLCERT, device->clientCertFile);
    if (device->clientKeyFile[0])
        curl_easy_setopt(curl, CURLOPT_SSLKEY, device->clientKeyFile);
    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
    if (result != CURLE_OK) {
        set_error(error,
                  errorSize,
                  response->overflow ? "MCP response exceeds configured limit"
                                     : curl_easy_strerror(result));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response->status < 200 || response->status >= 300) {
        if (error && errorSize)
            snprintf(error, errorSize, "MCP HTTP status %ld", response->status);
        return -1;
    }
    if (response->status != 202) {
        if (!strncasecmp(response->contentType,
                         "text/event-stream",
                         strlen("text/event-stream"))) {
            if (select_sse_json(response, (size_t) config->maxMessageBytes)) {
                set_error(error, errorSize, "invalid MCP SSE response");
                return -1;
            }
        } else if (strncasecmp(response->contentType,
                               "application/json",
                               strlen("application/json"))) {
            set_error(error, errorSize, "unsupported MCP response Content-Type");
            return -1;
        }
    }
    if (response->status != 202 && (!response->body || LjValidate(response->body, LJ_OBJECT))) {
        set_error(error, errorSize, "invalid MCP JSON response");
        return -1;
    }
    return 0;
}
