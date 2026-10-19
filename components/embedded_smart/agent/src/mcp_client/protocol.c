#include "mcp_internal.h"

#include "litecrab/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_meta(LjBuf* b) {
    return LjAppend(b,
                    "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"%s\","
                    "\"io.modelcontextprotocol/clientInfo\":{\"name\":\"LiteCrab\","
                    "\"version\":\"1.0.0\"},"
                    "\"io.modelcontextprotocol/clientCapabilities\":{}}",
                    MCP_PROTOCOL_MODERN);
}

int McpBuildDiscoverRequest(unsigned long long id, char* out, size_t outSize) {
    LjBuf b;
    LjBufInit(&b, out, outSize);
    LjAppend(&b,
             "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"server/discover\","
             "\"params\":{",
             id);
    append_meta(&b);
    LjAppend(&b, "}}");
    return b.failed ? -1 : 0;
}

int McpBuildInitializeRequest(unsigned long long id, char* out, size_t outSize) {
    int n = snprintf(out,
                     outSize,
                     "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"initialize\","
                     "\"params\":{\"protocolVersion\":\"%s\",\"capabilities\":{},"
                     "\"clientInfo\":{\"name\":\"LiteCrab\",\"version\":\"1.0.0\"}}}",
                     id,
                     MCP_PROTOCOL_LEGACY);
    return n < 0 || (size_t) n >= outSize ? -1 : 0;
}

int McpBuildInitializedNotification(char* out, size_t outSize) {
    int n = snprintf(out,
                     outSize,
                     "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    return n < 0 || (size_t) n >= outSize ? -1 : 0;
}

int McpBuildListToolsRequest(const char* version,
                             unsigned long long id,
                             char* out,
                             size_t outSize) {
    LjBuf b;
    LjBufInit(&b, out, outSize);
    LjAppend(&b,
             "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"tools/list\","
             "\"params\":{",
             id);
    if (version && !strcmp(version, MCP_PROTOCOL_MODERN))
        append_meta(&b);
    LjAppend(&b, "}}");
    return b.failed ? -1 : 0;
}

static int append_arguments(LjBuf* b, const CrabToolCall* call) {
    if (LjAppend(b, "{") < 0)
        return -1;
    for (size_t i = 0; call && i < call->argCount; i++) {
        const CrabToolArg* arg = &call->args[i];
        if (i && LjAppend(b, ",") < 0)
            return -1;
        if (LjAppendJsonString(b, arg->name) < 0 || LjAppend(b, ":") < 0)
            return -1;
        switch (arg->value.type) {
        case CRAB_TOOL_VALUE_STRING:
            if (LjAppendJsonString(b, arg->value.stringValue) < 0)
                return -1;
            break;
        case CRAB_TOOL_VALUE_INT:
            if (LjAppend(b, "%lld", (long long) arg->value.intValue) < 0)
                return -1;
            break;
        case CRAB_TOOL_VALUE_BOOL:
            if (LjAppend(b, "%s", arg->value.boolValue ? "true" : "false") < 0)
                return -1;
            break;
        case CRAB_TOOL_VALUE_STRING_ARRAY:
            if (LjAppend(b, "[") < 0)
                return -1;
            for (size_t j = 0; j < arg->value.stringArrayValue.count; j++) {
                if ((j && LjAppend(b, ",") < 0) ||
                    LjAppendJsonString(b, arg->value.stringArrayValue.items[j]) < 0)
                    return -1;
            }
            if (LjAppend(b, "]") < 0)
                return -1;
            break;
        default:
            if (LjAppend(b, "null") < 0)
                return -1;
        }
    }
    return LjAppend(b, "}");
}

int McpBuildCallToolRequest(const char* version,
                            unsigned long long id,
                            const char* toolName,
                            const CrabToolCall* call,
                            char* out,
                            size_t outSize) {
    LjBuf b;
    LjBufInit(&b, out, outSize);
    LjAppend(&b,
             "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"tools/call\","
             "\"params\":{\"name\":",
             id);
    LjAppendJsonString(&b, toolName);
    LjAppend(&b, ",\"arguments\":");
    append_arguments(&b, call);
    if (version && !strcmp(version, MCP_PROTOCOL_MODERN)) {
        LjAppend(&b, ",");
        append_meta(&b);
    }
    LjAppend(&b, "}}");
    return b.failed ? -1 : 0;
}

int McpResponseHasError(const char* json, char* message, size_t messageSize) {
    if (message && messageSize)
        message[0] = 0;
    if (!json || LjValidate(json, LJ_OBJECT))
        return 1;
    LjToken tokens[256];
    LjParser parser;
    LjInit(&parser);
    int count = LjParse(&parser, json, strlen(json), tokens, 256);
    int error = count > 0 ? LjObjectGet(json, tokens, count, 0, "error") : -1;
    if (error < 0)
        return 0;
    int text = LjObjectGet(json, tokens, count, error, "message");
    if (text >= 0 && message && messageSize)
        LjString(json, &tokens[text], message, messageSize);
    return 1;
}

static int parse_dynamic(const char* json, LjToken** out) {
    if (!json || !out || LjValidate(json, LJ_OBJECT))
        return -1;
    size_t capacity = strlen(json) / 2 + 64;
    if (capacity > 65536)
        return -1;
    LjToken* tokens = calloc(capacity, sizeof *tokens);
    if (!tokens)
        return -1;
    LjParser parser;
    LjInit(&parser);
    int count = LjParse(&parser, json, strlen(json), tokens, (unsigned) capacity);
    if (count < 1) {
        free(tokens);
        return -1;
    }
    *out = tokens;
    return count;
}

int McpResponseIdMatches(const char* json, unsigned long long expectedId) {
    LjToken* tokens = NULL;
    int count = parse_dynamic(json, &tokens);
    if (count < 1)
        return 0;
    int id = LjObjectGet(json, tokens, count, 0, "id");
    int64_t value = -1;
    int matches = id >= 0 && !LjInt64(json, &tokens[id], &value) && value >= 0 &&
                  (unsigned long long) value == expectedId;
    free(tokens);
    return matches;
}

int McpDiscoverSupportsVersion(const char* json, const char* version) {
    LjToken* tokens = NULL;
    int count = parse_dynamic(json, &tokens);
    if (count < 1)
        return 0;
    int result = LjObjectGet(json, tokens, count, 0, "result");
    int versions = result >= 0 ? LjObjectGet(json, tokens, count, result, "supportedVersions") : -1;
    int found = 0;
    if (versions >= 0 && tokens[versions].type == LJ_ARRAY)
        for (int index = 0;; index++) {
            int item = LjArrayGet(tokens, count, versions, index);
            if (item < 0)
                break;
            if (LjTokenEq(json, &tokens[item], version)) {
                found = 1;
                break;
            }
        }
    free(tokens);
    return found;
}

int McpInitializeVersionMatches(const char* json, const char* version) {
    LjToken* tokens = NULL;
    int count = parse_dynamic(json, &tokens);
    if (count < 1)
        return 0;
    int result = LjObjectGet(json, tokens, count, 0, "result");
    int selected = result >= 0 ? LjObjectGet(json, tokens, count, result, "protocolVersion") : -1;
    int matches = selected >= 0 && LjTokenEq(json, &tokens[selected], version);
    free(tokens);
    return matches;
}
