#include "mcp_internal.h"

#include "litecrab/json.h"

#include <curl/curl.h>

#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[CRAB_TOOL_ARG_NAME_MAX];
    char description[192];
    size_t maxStringBytes;
    int hasMinimum;
    int hasMaximum;
    int64_t minimum;
    int64_t maximum;
    CrabToolParamSpec spec;
} McpRemoteParam;

typedef struct McpManagedDevice McpManagedDevice;
typedef struct {
    McpManagedDevice* device;
    char publicName[MCP_CLIENT_PUBLIC_NAME_MAX];
    char remoteName[CRAB_TOOL_NAME_MAX];
    char description[MCP_CLIENT_DESCRIPTION_MAX];
    McpRemoteParam params[MCP_CLIENT_MAX_PARAMS_PER_TOOL];
    CrabToolParamSpec paramSpecs[MCP_CLIENT_MAX_PARAMS_PER_TOOL];
    size_t paramCount;
    CrabToolSpec spec;
} McpRemoteTool;

struct McpManagedDevice {
    McpDeviceConfig config;
    char token[256];
    char version[16];
    char sessionId[MCP_CLIENT_SESSION_MAX];
    int ready;
    pthread_mutex_t mutex;
    McpRemoteTool tools[MCP_CLIENT_MAX_TOOLS_PER_DEVICE];
    int toolCount;
};

struct McpClientManager {
    McpClientConfig config;
    CrabRuntime* runtime;
    McpManagedDevice devices[LITECRAB_MCP_MAX_DEVICES];
    int deviceCount;
    int readyDevices;
    int registeredTools;
    int started;
    atomic_ullong nextRequestId;
};

typedef struct {
    const char* json;
    LjToken* tokens;
    int count;
} JsonDoc;

static McpClientManager* activeManager;
static pthread_mutex_t activeMutex = PTHREAD_MUTEX_INITIALIZER;

static void set_error(char* out, size_t size, const char* format, const char* value) {
    if (out && size)
        snprintf(out, size, format, value ? value : "");
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
        memset(doc, 0, sizeof *doc);
        return -1;
    }
    return 0;
}

static void clear_doc(JsonDoc* doc) {
    free(doc->tokens);
    memset(doc, 0, sizeof *doc);
}

static int object_get(const JsonDoc* doc, int object, const char* name) {
    return LjObjectGet(doc->json, doc->tokens, doc->count, object, name);
}

static int get_string(const JsonDoc* doc, int object, const char* name, char* out, size_t size) {
    int token = object_get(doc, object, name);
    return token >= 0 && !LjString(doc->json, &doc->tokens[token], out, size) ? 0 : -1;
}

static int get_int64(const JsonDoc* doc, int object, const char* name, int64_t* out) {
    int token = object_get(doc, object, name);
    return token >= 0 ? LjInt64(doc->json, &doc->tokens[token], out) : -1;
}

static int array_has_string(const JsonDoc* doc, int array, const char* value) {
    if (array < 0 || doc->tokens[array].type != LJ_ARRAY)
        return 0;
    for (int index = 0;; index++) {
        int token = LjArrayGet(doc->tokens, doc->count, array, index);
        if (token < 0)
            break;
        if (LjTokenEq(doc->json, &doc->tokens[token], value))
            return 1;
    }
    return 0;
}

static void sanitize_component(const char* input, char* output, size_t size) {
    size_t written = 0;
    if (!output || !size)
        return;
    for (const unsigned char* p = (const unsigned char*) input; p && *p && written + 1 < size;
         p++)
        output[written++] = isalnum(*p) || *p == '_' || *p == '-' ? (char) *p : '_';
    output[written] = 0;
}

static int valid_remote_name(const char* name) {
    if (!name || !*name)
        return 0;
    for (const unsigned char* p = (const unsigned char*) name; *p; p++)
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.'))
            return 0;
    return 1;
}

static int resolve_device_token(McpManagedDevice* device, char* error, size_t errorSize) {
    device->token[0] = 0;
    if (!device->config.tokenEnv[0])
        return 0;
    const char* value = getenv(device->config.tokenEnv);
    if (!value || !*value || strlen(value) >= sizeof device->token) {
        set_error(error,
                  errorSize,
                  "missing or oversized MCP token environment variable: %s",
                  device->config.tokenEnv);
        return -1;
    }
    for (const unsigned char* p = (const unsigned char*) value; *p; p++)
        if (*p < 0x21 || *p == 0x7f) {
            set_error(error, errorSize, "MCP token contains invalid characters: %s", device->config.id);
            return -1;
        }
    snprintf(device->token, sizeof device->token, "%s", value);
    return 0;
}

static int parse_tool(McpManagedDevice* device,
                      const JsonDoc* doc,
                      int toolObject,
                      McpRemoteTool* tool,
                      char* error,
                      size_t errorSize) {
    memset(tool, 0, sizeof *tool);
    tool->device = device;
    if (get_string(doc, toolObject, "name", tool->remoteName, sizeof tool->remoteName)) {
        set_error(error, errorSize, "MCP tool has invalid name: %s", device->config.id);
        return -1;
    }
    if (!valid_remote_name(tool->remoteName)) {
        set_error(error, errorSize, "MCP tool name has unsafe characters: %s", device->config.id);
        return -1;
    }
    if (get_string(doc, toolObject, "description", tool->description, sizeof tool->description))
        snprintf(tool->description, sizeof tool->description, "Remote MCP tool %s", tool->remoteName);
    char safeAlias[LITECRAB_MCP_ALIAS_MAX], safeName[CRAB_TOOL_NAME_MAX];
    sanitize_component(device->config.alias, safeAlias, sizeof safeAlias);
    sanitize_component(tool->remoteName, safeName, sizeof safeName);
    int nameSize = snprintf(tool->publicName,
                            sizeof tool->publicName,
                            "%s__%s",
                            safeAlias,
                            safeName);
    if (!safeAlias[0] || !safeName[0] || nameSize < 0 ||
        (size_t) nameSize >= CRAB_TOOL_NAME_MAX) {
        set_error(error, errorSize, "MCP public tool name is too long: %s", tool->remoteName);
        return -1;
    }
    int schema = object_get(doc, toolObject, "inputSchema");
    if (schema < 0 || doc->tokens[schema].type != LJ_OBJECT) {
        set_error(error, errorSize, "MCP tool inputSchema is missing: %s", tool->remoteName);
        return -1;
    }
    char rootType[24];
    if (get_string(doc, schema, "type", rootType, sizeof rootType) || strcmp(rootType, "object")) {
        set_error(error, errorSize, "MCP tool root schema must be object: %s", tool->remoteName);
        return -1;
    }
    int properties = object_get(doc, schema, "properties");
    int required = object_get(doc, schema, "required");
    if (required >= 0 && doc->tokens[required].type != LJ_ARRAY) {
        set_error(error, errorSize, "MCP tool required must be array: %s", tool->remoteName);
        return -1;
    }
    if (properties >= 0) {
        if (doc->tokens[properties].type != LJ_OBJECT) {
            set_error(error, errorSize, "MCP tool properties must be object: %s", tool->remoteName);
            return -1;
        }
        int cursor = properties + 1;
        while (cursor < doc->count && doc->tokens[cursor].start < doc->tokens[properties].end) {
            int value = cursor + 1;
            if (value >= doc->count || tool->paramCount >= MCP_CLIENT_MAX_PARAMS_PER_TOOL) {
                set_error(error, errorSize, "MCP tool has too many parameters: %s", tool->remoteName);
                return -1;
            }
            McpRemoteParam* param = &tool->params[tool->paramCount];
            if (LjString(doc->json, &doc->tokens[cursor], param->name, sizeof param->name) ||
                doc->tokens[value].type != LJ_OBJECT) {
                set_error(error, errorSize, "MCP tool parameter is invalid: %s", tool->remoteName);
                return -1;
            }
            char type[32];
            if (get_string(doc, value, "type", type, sizeof type)) {
                set_error(error, errorSize, "MCP tool parameter type is missing: %s", param->name);
                return -1;
            }
            if (!strcmp(type, "string"))
                param->spec.type = CRAB_TOOL_VALUE_STRING;
            else if (!strcmp(type, "integer")) {
                param->spec.type = CRAB_TOOL_VALUE_INT;
                param->hasMinimum = !get_int64(doc, value, "minimum", &param->minimum);
                param->hasMaximum = !get_int64(doc, value, "maximum", &param->maximum);
                if (param->hasMinimum && param->hasMaximum && param->minimum > param->maximum) {
                    set_error(error, errorSize, "invalid MCP integer range: %s", param->name);
                    return -1;
                }
            } else if (!strcmp(type, "boolean"))
                param->spec.type = CRAB_TOOL_VALUE_BOOL;
            else {
                set_error(error, errorSize, "unsupported MCP parameter type: %s", type);
                return -1;
            }
            if (param->spec.type == CRAB_TOOL_VALUE_STRING) {
                int64_t maximumLength;
                if (!get_int64(doc, value, "maxLength", &maximumLength)) {
                    if (maximumLength < 1 || maximumLength > CRAB_TOOL_CONTENT_MAX) {
                        set_error(error, errorSize, "unsupported MCP maxLength: %s", param->name);
                        return -1;
                    }
                    param->maxStringBytes = (size_t) maximumLength;
                }
            }
            if (get_string(doc,
                           value,
                           "description",
                           param->description,
                           sizeof param->description))
                snprintf(param->description, sizeof param->description, "MCP parameter %s", param->name);
            param->spec.name = param->name;
            param->spec.description = param->description;
            param->spec.required = array_has_string(doc, required, param->name);
            tool->paramSpecs[tool->paramCount] = param->spec;
            tool->paramCount++;
            cursor = LjSkip(doc->tokens, doc->count, value);
        }
    }
    tool->spec.name = tool->publicName;
    tool->spec.description = tool->description;
    tool->spec.enabledByDefault = 1;
    tool->spec.readonly = 1;
    tool->spec.permissions = 0;
    tool->spec.concurrency = CRAB_TOOL_CONCURRENCY_SHARED_READ;
    tool->spec.riskLevel = CRAB_TOOL_RISK_LOW;
    tool->spec.params = tool->paramSpecs;
    tool->spec.paramCount = tool->paramCount;
    tool->spec.filter = CrabBasicFilter;
    return 0;
}

static int parse_catalog(McpManagedDevice* device,
                         const char* json,
                         char* error,
                         size_t errorSize) {
    JsonDoc doc;
    if (parse_doc(json, &doc)) {
        set_error(error, errorSize, "invalid tools/list response from device: %s", device->config.id);
        return -1;
    }
    int result = object_get(&doc, 0, "result");
    int tools = result >= 0 ? object_get(&doc, result, "tools") : -1;
    if (tools < 0 || doc.tokens[tools].type != LJ_ARRAY) {
        clear_doc(&doc);
        set_error(error, errorSize, "tools/list response has no tools array: %s", device->config.id);
        return -1;
    }
    device->toolCount = 0;
    for (int index = 0;; index++) {
        int object = LjArrayGet(doc.tokens, doc.count, tools, index);
        if (object < 0)
            break;
        if (device->toolCount >= MCP_CLIENT_MAX_TOOLS_PER_DEVICE ||
            parse_tool(device,
                       &doc,
                       object,
                       &device->tools[device->toolCount],
                       error,
                       errorSize)) {
            clear_doc(&doc);
            return -1;
        }
        device->toolCount++;
    }
    clear_doc(&doc);
    return 0;
}

static int parse_tool_result(const char* json, CrabRawResult* raw) {
    JsonDoc doc;
    if (parse_doc(json, &doc))
        return -1;
    int errorObject = object_get(&doc, 0, "error");
    if (errorObject >= 0) {
        char message[512] = "remote MCP error";
        get_string(&doc, errorObject, "message", message, sizeof message);
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, message);
        clear_doc(&doc);
        return 0;
    }
    int result = object_get(&doc, 0, "result");
    if (result < 0) {
        clear_doc(&doc);
        return -1;
    }
    int isError = object_get(&doc, result, "isError");
    int failed = 0;
    if (isError >= 0)
        LjBool(doc.json, &doc.tokens[isError], &failed);
    char text[CRAB_TOOL_CONTENT_MAX] = "";
    int content = object_get(&doc, result, "content");
    if (content >= 0 && doc.tokens[content].type == LJ_ARRAY) {
        int item = LjArrayGet(doc.tokens, doc.count, content, 0);
        if (item >= 0)
            get_string(&doc, item, "text", text, sizeof text);
    }
    if (!text[0]) {
        int structured = object_get(&doc, result, "structuredContent");
        if (structured >= 0) {
            int bytes = doc.tokens[structured].end - doc.tokens[structured].start;
            if (bytes > 0 && (size_t) bytes < sizeof text) {
                memcpy(text, doc.json + doc.tokens[structured].start, (size_t) bytes);
                text[bytes] = 0;
            }
        }
    }
    raw->exitCode = failed ? 1 : 0;
    if (failed)
        CrabRawResultSetStderr(raw, text[0] ? text : "remote tool failed");
    else
        CrabRawResultSetStdout(raw, text);
    clear_doc(&doc);
    return 0;
}

static McpRemoteTool* find_and_lock_active_tool(const char* publicName,
                                                McpClientManager** active) {
    McpRemoteTool* found = NULL;
    if (active)
        *active = NULL;
    pthread_mutex_lock(&activeMutex);
    McpClientManager* manager = activeManager;
    if (manager && manager->started)
        for (int i = 0; i < manager->deviceCount && !found; i++)
            for (int j = 0; j < manager->devices[i].toolCount; j++)
                if (!strcmp(manager->devices[i].tools[j].publicName, publicName)) {
                    found = &manager->devices[i].tools[j];
                    break;
                }
    if (found) {
        pthread_mutex_lock(&found->device->mutex);
        if (active)
            *active = manager;
    }
    pthread_mutex_unlock(&activeMutex);
    return found;
}

static int execute_remote(CrabRuntime* runtime, const CrabToolCall* call, CrabRawResult* raw) {
    (void) runtime;
    McpClientManager* manager = NULL;
    McpRemoteTool* tool = find_and_lock_active_tool(call ? call->toolName : NULL, &manager);
    if (!tool) {
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, "MCP client or device is unavailable");
        return 0;
    }
    McpManagedDevice* device = tool->device;
    if (!device->ready) {
        pthread_mutex_unlock(&device->mutex);
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, "MCP device is unavailable");
        return 0;
    }
    unsigned long long requestId = manager ? atomic_fetch_add(&manager->nextRequestId, 1) : 0;
    if (!manager || !requestId) {
        pthread_mutex_unlock(&device->mutex);
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, "MCP client stopped");
        return 0;
    }
    for (size_t i = 0; i < tool->paramCount; i++) {
        const McpRemoteParam* param = &tool->params[i];
        const CrabToolArg* argument = CrabToolCallFindArg(call, param->name);
        if (argument && param->maxStringBytes &&
            argument->value.type == CRAB_TOOL_VALUE_STRING &&
            argument->value.stringValue &&
            strlen(argument->value.stringValue) > param->maxStringBytes) {
            pthread_mutex_unlock(&device->mutex);
            raw->exitCode = 1;
            CrabRawResultSetStderr(raw, "MCP string argument exceeds remote schema maxLength");
            return 0;
        }
        if (argument && argument->value.type == CRAB_TOOL_VALUE_INT &&
            ((param->hasMinimum && argument->value.intValue < param->minimum) ||
             (param->hasMaximum && argument->value.intValue > param->maximum))) {
            pthread_mutex_unlock(&device->mutex);
            raw->exitCode = 1;
            CrabRawResultSetStderr(raw, "MCP integer argument exceeds remote schema range");
            return 0;
        }
    }
    size_t requestSize = (size_t) manager->config.maxMessageBytes;
    char* request = malloc(requestSize);
    if (!request || McpBuildCallToolRequest(device->version,
                                            requestId,
                                            tool->remoteName,
                                            call,
                                            request,
                                            requestSize)) {
        free(request);
        pthread_mutex_unlock(&device->mutex);
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, "cannot encode MCP tool call");
        return 0;
    }
    McpHttpResponse response;
    char error[256] = "";
    int rc = McpHttpPost(&device->config,
                         &manager->config,
                         device->token,
                         device->version,
                         "tools/call",
                         tool->remoteName,
                         device->sessionId,
                         request,
                         &response,
                         error,
                         sizeof error);
    free(request);
    if (!rc && !McpResponseIdMatches(response.body, requestId)) {
        rc = -1;
        snprintf(error, sizeof error, "MCP response id mismatch");
    }
    if (rc || parse_tool_result(response.body, raw)) {
        raw->exitCode = 1;
        CrabRawResultSetStderr(raw, error[0] ? error : "invalid MCP tool result");
    }
    McpHttpResponseClear(&response);
    pthread_mutex_unlock(&device->mutex);
    return 0;
}

static int negotiate(McpClientManager* manager,
                     McpManagedDevice* device,
                     char* error,
                     size_t errorSize) {
    char request[4096];
    McpHttpResponse response;
    unsigned long long id = atomic_fetch_add(&manager->nextRequestId, 1);
    if (McpBuildDiscoverRequest(id, request, sizeof request))
        return -1;
    int rc = McpHttpPost(&device->config,
                         &manager->config,
                         device->token,
                         MCP_PROTOCOL_MODERN,
                         "server/discover",
                         NULL,
                         NULL,
                         request,
                         &response,
                         error,
                         errorSize);
    char protocolError[256] = "";
    if (!rc && McpResponseIdMatches(response.body, id) &&
        !McpResponseHasError(response.body, protocolError, sizeof protocolError) &&
        McpDiscoverSupportsVersion(response.body, MCP_PROTOCOL_MODERN)) {
        snprintf(device->version, sizeof device->version, "%s", MCP_PROTOCOL_MODERN);
        McpHttpResponseClear(&response);
        return 0;
    }
    McpHttpResponseClear(&response);
    id = atomic_fetch_add(&manager->nextRequestId, 1);
    if (McpBuildInitializeRequest(id, request, sizeof request))
        return -1;
    rc = McpHttpPost(&device->config,
                     &manager->config,
                     device->token,
                     NULL,
                     "initialize",
                     NULL,
                     NULL,
                     request,
                     &response,
                     error,
                     errorSize);
    if (rc || !McpResponseIdMatches(response.body, id) ||
        McpResponseHasError(response.body, protocolError, sizeof protocolError) ||
        !McpInitializeVersionMatches(response.body, MCP_PROTOCOL_LEGACY)) {
        if (!rc && protocolError[0])
            set_error(error, errorSize, "MCP initialize failed: %s", protocolError);
        McpHttpResponseClear(&response);
        return -1;
    }
    snprintf(device->version, sizeof device->version, "%s", MCP_PROTOCOL_LEGACY);
    snprintf(device->sessionId, sizeof device->sessionId, "%s", response.sessionId);
    McpHttpResponseClear(&response);
    if (McpBuildInitializedNotification(request, sizeof request))
        return -1;
    rc = McpHttpPost(&device->config,
                     &manager->config,
                     device->token,
                     MCP_PROTOCOL_LEGACY,
                     "notifications/initialized",
                     NULL,
                     device->sessionId,
                     request,
                     &response,
                     error,
                     errorSize);
    McpHttpResponseClear(&response);
    return rc;
}

static int discover_tools(McpClientManager* manager,
                          McpManagedDevice* device,
                          char* error,
                          size_t errorSize) {
    size_t requestSize = (size_t) manager->config.maxMessageBytes;
    char* request = malloc(requestSize);
    if (!request)
        return -1;
    unsigned long long id = atomic_fetch_add(&manager->nextRequestId, 1);
    if (McpBuildListToolsRequest(device->version, id, request, requestSize)) {
        free(request);
        return -1;
    }
    McpHttpResponse response;
    int rc = McpHttpPost(&device->config,
                         &manager->config,
                         device->token,
                         device->version,
                         "tools/list",
                         NULL,
                         device->sessionId,
                         request,
                         &response,
                         error,
                         errorSize);
    free(request);
    if (!rc && !McpResponseIdMatches(response.body, id)) {
        set_error(error, errorSize, "tools/list response id mismatch: %s", device->config.id);
        rc = -1;
    }
    if (!rc)
        rc = parse_catalog(device, response.body, error, errorSize);
    McpHttpResponseClear(&response);
    return rc;
}

int McpClientManagerCreate(McpClientManager** out, const McpClientConfig* config) {
    if (!out || !config)
        return -1;
    *out = NULL;
    McpClientManager* manager = calloc(1, sizeof *manager);
    if (!manager)
        return -1;
    manager->config = *config;
    manager->deviceCount = config->deviceCount;
    atomic_init(&manager->nextRequestId, 1);
    for (int i = 0; i < manager->deviceCount; i++) {
        manager->devices[i].config = config->devices[i];
        pthread_mutex_init(&manager->devices[i].mutex, NULL);
    }
    *out = manager;
    return 0;
}

int McpClientManagerStart(McpClientManager* manager,
                          CrabRuntime* runtime,
                          char* error,
                          size_t errorSize) {
    if (!manager || !runtime)
        return -1;
    if (!manager->config.enabled)
        return 0;
    manager->runtime = runtime;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        set_error(error, errorSize, "cannot initialize libcurl: %s", "global initialization failed");
        return -1;
    }
    pthread_mutex_lock(&activeMutex);
    if (activeManager && activeManager != manager) {
        pthread_mutex_unlock(&activeMutex);
        set_error(error, errorSize, "another MCP Client Manager is active: %s", "");
        return -1;
    }
    activeManager = manager;
    manager->started = 1;
    pthread_mutex_unlock(&activeMutex);
    int failures = 0;
    for (int i = 0; i < manager->deviceCount; i++) {
        McpManagedDevice* device = &manager->devices[i];
        if (!device->config.enabled)
            continue;
        char deviceError[512] = "";
        if (resolve_device_token(device, deviceError, sizeof deviceError) ||
            negotiate(manager, device, deviceError, sizeof deviceError) ||
            discover_tools(manager, device, deviceError, sizeof deviceError)) {
            failures++;
            if (device->config.required || manager->config.required) {
                set_error(error, errorSize, "required MCP device failed: %s", deviceError);
                McpClientManagerStop(manager);
                return -1;
            }
            continue;
        }
        for (int j = 0; j < device->toolCount; j++) {
            device->tools[j].spec.execute = execute_remote;
            int rc = CrabRegistryRegister(&runtime->registry, &device->tools[j].spec);
            if (rc) {
                snprintf(deviceError,
                         sizeof deviceError,
                         "cannot register MCP tool %s (%d)",
                         device->tools[j].publicName,
                         rc);
                set_error(error, errorSize, "%s", deviceError);
                McpClientManagerStop(manager);
                return -1;
            }
            manager->registeredTools++;
        }
        device->ready = 1;
        manager->readyDevices++;
    }
    if (manager->config.required && manager->readyDevices == 0) {
        set_error(error, errorSize, "MCP is required but no device is ready: %s", "check configuration and connectivity");
        McpClientManagerStop(manager);
        return -1;
    }
    if (failures && error && errorSize)
        snprintf(error, errorSize, "%d optional MCP device(s) unavailable", failures);
    return 0;
}

void McpClientManagerStop(McpClientManager* manager) {
    if (!manager)
        return;
    pthread_mutex_lock(&activeMutex);
    manager->started = 0;
    if (activeManager == manager)
        activeManager = NULL;
    pthread_mutex_unlock(&activeMutex);
    for (int i = 0; i < manager->deviceCount; i++) {
        pthread_mutex_lock(&manager->devices[i].mutex);
        manager->devices[i].ready = 0;
        pthread_mutex_unlock(&manager->devices[i].mutex);
    }
}

void McpClientManagerDestroy(McpClientManager* manager) {
    if (!manager)
        return;
    McpClientManagerStop(manager);
    for (int i = 0; i < manager->deviceCount; i++)
        pthread_mutex_destroy(&manager->devices[i].mutex);
    memset(manager, 0, sizeof *manager);
    free(manager);
}

int McpClientManagerReadyDevices(const McpClientManager* manager) {
    return manager ? manager->readyDevices : 0;
}

int McpClientManagerRegisteredTools(const McpClientManager* manager) {
    return manager ? manager->registeredTools : 0;
}
