#include "litecrab/config.h"

#include "litecrab/json.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* json;
    LjToken* tokens;
    int count;
} ParsedFile;

static void copy_text(char* out, size_t size, const char* value) {
    if (!out || !size)
        return;
    size_t n = value ? strlen(value) : 0;
    if (n >= size)
        n = size - 1;
    if (n)
        memcpy(out, value, n);
    out[n] = 0;
}
static void set_error(char* out, size_t size, const char* fmt, const char* value) {
    if (out && size)
        snprintf(out, size, fmt, value ? value : "");
}
static void parsed_clear(ParsedFile* p) {
    if (p) {
        free(p->json);
        free(p->tokens);
        memset(p, 0, sizeof *p);
    }
}
static int parse_file(const char* path, ParsedFile* out, char* error, size_t errorSize) {
    memset(out, 0, sizeof *out);
    if (!path || !*path)
        return 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        set_error(error, errorSize, "cannot open config: %s", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END)) {
        fclose(fp);
        return -1;
    }
    long length = ftell(fp);
    rewind(fp);
    if (length < 2 || length > 1024 * 1024) {
        fclose(fp);
        set_error(error, errorSize, "invalid config size: %s", path);
        return -1;
    }
    out->json = malloc((size_t) length + 1);
    if (!out->json) {
        fclose(fp);
        return -1;
    }
    size_t got = fread(out->json, 1, (size_t) length, fp);
    fclose(fp);
    out->json[got] = 0;
    if (LjValidate(out->json, LJ_OBJECT)) {
        set_error(error, errorSize, "invalid JSON config: %s", path);
        parsed_clear(out);
        return -1;
    }
    size_t cap = got / 2 + 64;
    out->tokens = calloc(cap, sizeof *out->tokens);
    if (!out->tokens) {
        parsed_clear(out);
        return -1;
    }
    LjParser parser;
    LjInit(&parser);
    out->count = LjParse(&parser, out->json, got, out->tokens, (unsigned) cap);
    if (out->count < 1) {
        set_error(error, errorSize, "cannot parse config: %s", path);
        parsed_clear(out);
        return -1;
    }
    return 0;
}
static int object_get(const ParsedFile* p, int object, const char* key) {
    return LjObjectGet(p->json, p->tokens, p->count, object, key);
}
static int get_string(const ParsedFile* p, int object, const char* key, char* out, size_t size) {
    int v = object_get(p, object, key);
    return v >= 0 && p->tokens[v].type == LJ_STRING && !LjString(p->json, &p->tokens[v], out, size);
}
static int get_int(const ParsedFile* p, int object, const char* key, int* out) {
    int v = object_get(p, object, key);
    int64_t n;
    if (v < 0 || LjInt64(p->json, &p->tokens[v], &n) || n < INT32_MIN || n > INT32_MAX)
        return 0;
    *out = (int) n;
    return 1;
}
static int get_double(const ParsedFile* p, int object, const char* key, double* out) {
    int v = object_get(p, object, key);
    return v >= 0 && !LjDouble(p->json, &p->tokens[v], out);
}
static int get_bool(const ParsedFile* p, int object, const char* key, int* out) {
    int v = object_get(p, object, key);
    return v >= 0 && !LjBool(p->json, &p->tokens[v], out);
}

void LiteCrabConfigDefaults(LiteCrabAppConfig* c) {
    if (!c)
        return;
    memset(c, 0, sizeof *c);
    copy_text(c->workspaceRoot, sizeof c->workspaceRoot, ".");
    copy_text(c->logDir, sizeof c->logDir, "logs");
    copy_text(c->llmBaseUrl, sizeof c->llmBaseUrl, "http://127.0.0.1:18080/v1/chat/completions");
    copy_text(c->llmModel, sizeof c->llmModel, "litecrab");
    c->llm.maxTokens = 2048;
    c->llm.temperature = 0.2;
    c->llm.stream = 1;
    c->llm.timeoutMs = 60000;
    c->llm.baseUrl = c->llmBaseUrl;
    c->llm.apiKey = c->llmApiKey;
    c->llm.model = c->llmModel;
    c->llm.reasoningEffort = c->reasoningEffort;
    MemoryConfigDefaults(&c->memory);
}
static int load_memory(LiteCrabAppConfig* c, const ParsedFile* p) {
    int m=object_get(p,0,"memory");
    if (m<0) return 0;
    if (p->tokens[m].type!=LJ_OBJECT) return -1;
    int v;
#define MBOOL(key,field) do { v=object_get(p,m,key); if (v>=0 && !get_bool(p,m,key,&c->memory.field)) return -1; } while(0)
#define MINT(key,field) do { v=object_get(p,m,key); if (v>=0 && !get_int(p,m,key,&c->memory.field)) return -1; } while(0)
    MBOOL("enabled",enabled); MBOOL("sync_writes",syncWrites);
    MINT("summary_max_bytes",summaryMaxBytes); MINT("recent_max_bytes",recentMaxBytes);
    MINT("recent_context_items",recentContextItems); MINT("max_context_bytes",maxContextBytes);
#undef MBOOL
#undef MINT
    v=object_get(p,m,"store_dir");
    if (v>=0 && !get_string(p,m,"store_dir",c->memory.storeDir,sizeof c->memory.storeDir)) return -1;
    return MemoryConfigValid(&c->memory)?0:-1;
}

static int load_base(LiteCrabAppConfig* c, const char* path, char* error, size_t errorSize) {
    ParsedFile p;
    if (parse_file(path, &p, error, errorSize))
        return -1;
    if (!p.json)
        return 0;
    int server = object_get(&p, 0, "server");
    if (server >= 0) {
        int aur = object_get(&p, server, "allow_unauthenticated_remote");
        if (aur >= 0) {
            int bv;
            if (!get_bool(&p, server, "allow_unauthenticated_remote", &bv)) {
                set_error(error,
                          errorSize,
                          "allow_unauthenticated_remote must be a JSON boolean: %s",
                          path ? path : "");
                parsed_clear(&p);
                return -1;
            }
        }
    }
    int log = object_get(&p, 0, "log");
    if (log >= 0) {
        if (!get_string(&p, log, "dir", c->logDir, sizeof c->logDir)) {
            char file[512];
            if (get_string(&p, log, "filename", file, sizeof file)) {
                char* slash = strrchr(file, '/');
                if (slash) {
                    *slash = 0;
                    copy_text(c->logDir, sizeof c->logDir, file);
                }
            }
        }
    }
    get_string(&p, 0, "workspace_root", c->workspaceRoot, sizeof c->workspaceRoot);
    if (load_memory(c,&p)) {
        set_error(error,errorSize,"invalid memory config: %s",path);
        parsed_clear(&p);
        return -1;
    }
    parsed_clear(&p);
    return 0;
}
static int load_llm(LiteCrabAppConfig* c,
                    const char* path,
                    const char* providerOverride,
                    char* error,
                    size_t errorSize) {
    ParsedFile p;
    if (parse_file(path, &p, error, errorSize))
        return -1;
    if (!p.json)
        return 0;
    int llm = object_get(&p, 0, "llm");
    if (llm < 0)
        llm = 0;
    if (p.tokens[llm].type != LJ_OBJECT) {
        set_error(error, errorSize, "LLM config must be an object: %s", path);
        parsed_clear(&p);
        return -1;
    }
    char selected[128] = "";
    if (providerOverride && *providerOverride)
        copy_text(selected, sizeof selected, providerOverride);
    else
        get_string(&p, llm, "default", selected, sizeof selected);
    int provider = selected[0] ? object_get(&p, llm, selected) : llm;
    if (provider < 0) {
        set_error(error, errorSize, "unknown default LLM provider: %s", selected);
        parsed_clear(&p);
        return -1;
    }
    if (p.tokens[provider].type != LJ_OBJECT) {
        set_error(error, errorSize, "LLM provider must be an object: %s", selected);
        parsed_clear(&p);
        return -1;
    }
    if (!get_string(&p, provider, "baseUrl", c->llmBaseUrl, sizeof c->llmBaseUrl))
        get_string(&p, provider, "url", c->llmBaseUrl, sizeof c->llmBaseUrl);
    if (!get_string(&p, provider, "apiKey", c->llmApiKey, sizeof c->llmApiKey))
        get_string(&p, provider, "api_key", c->llmApiKey, sizeof c->llmApiKey);
    char apiKeyEnv[128] = "";
    if (get_string(&p, provider, "apiKeyEnv", apiKeyEnv, sizeof apiKeyEnv)) {
        const char* value = getenv(apiKeyEnv);
        if (!value || !*value) {
            set_error(
                error, errorSize, "LLM API key environment variable is not set: %s", apiKeyEnv);
            parsed_clear(&p);
            return -1;
        }
        copy_text(c->llmApiKey, sizeof c->llmApiKey, value);
    }
    if (!get_string(&p, provider, "modelName", c->llmModel, sizeof c->llmModel))
        get_string(&p, provider, "model", c->llmModel, sizeof c->llmModel);
    int setting = object_get(&p, llm, "max_tokens");
    if (setting >= 0 && !get_int(&p, llm, "max_tokens", &c->llm.maxTokens))
        goto invalid_setting;
    setting = object_get(&p, llm, "temperature");
    if (setting >= 0 && !get_double(&p, llm, "temperature", &c->llm.temperature))
        goto invalid_setting;
    setting = object_get(&p, llm, "stream");
    if (setting >= 0 && !get_bool(&p, llm, "stream", &c->llm.stream))
        goto invalid_setting;
    setting = object_get(&p, llm, "timeout_ms");
    if (setting >= 0 && !get_int(&p, llm, "timeout_ms", &c->llm.timeoutMs))
        goto invalid_setting;
    get_string(&p, llm, "reasoning_effort", c->reasoningEffort, sizeof c->reasoningEffort);
    parsed_clear(&p);
    return 0;
invalid_setting:
    set_error(error, errorSize, "invalid LLM setting in config: %s", path);
    parsed_clear(&p);
    return -1;
}
static void env_text(const char* name, char* out, size_t size) {
    const char* v = getenv(name);
    if (v && *v)
        copy_text(out, size, v);
}
static int safe_name(const char* text, int allowDot) {
    if (!text || !*text)
        return 0;
    for (const unsigned char* p = (const unsigned char*) text; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || (allowDot && *p == '.')))
            return 0;
    return 1;
}
static int safe_env_name(const char* text) {
    if (!text || !(*text == '_' || (*text >= 'A' && *text <= 'Z') ||
                   (*text >= 'a' && *text <= 'z')))
        return 0;
    for (const unsigned char* p = (const unsigned char*) text + 1; *p; p++)
        if (!(isalnum(*p) || *p == '_'))
            return 0;
    return 1;
}
static int safe_mcp_url(const char* url) {
    if (!url || !*url)
        return 0;
    for (const unsigned char* p = (const unsigned char*) url; *p; p++)
        if (*p < 0x21 || *p == 0x7f)
            return 0;
    if (!strncmp(url, "https://", 8))
        return url[8] != 0;
    if (strncmp(url, "http://", 7))
        return 0;
    const char* host = url + 7;
    const char* end = host;
    while (*end && *end != ':' && *end != '/')
        end++;
    size_t size = (size_t) (end - host);
    return (size == strlen("127.0.0.1") && !strncmp(host, "127.0.0.1", size)) ||
           (size == strlen("localhost") && !strncmp(host, "localhost", size));
}
static int validate(LiteCrabAppConfig* c, char* error, size_t errorSize) {
    if (strncmp(c->llmBaseUrl, "http://", 7) && strncmp(c->llmBaseUrl, "https://", 8)) {
        set_error(error, errorSize, "invalid LLM URL: %s", c->llmBaseUrl);
        return -1;
    }
    if (!c->llmModel[0]) {
        set_error(error, errorSize, "missing LLM model: %s", "");
        return -1;
    }
    if (c->llm.maxTokens < 1 || c->llm.maxTokens > 1024 * 1024 || c->llm.timeoutMs < 1 ||
        c->llm.timeoutMs > 60 * 60 * 1000 || c->llm.temperature < 0.0 || c->llm.temperature > 2.0) {
        set_error(error, errorSize, "invalid LLM limits: %s", "");
        return -1;
    }
    return 0;
}
int LiteCrabConfigLoadProvider(LiteCrabAppConfig* c,
                               const char* basePath,
                               const char* llmPath,
                               const char* llmProvider,
                               char* error,
                               size_t errorSize) {
    if (!c)
        return -1;
    LiteCrabConfigDefaults(c);
    if (error && errorSize)
        error[0] = 0;
    if (basePath && *basePath && load_base(c, basePath, error, errorSize))
        return -1;
    if (llmPath && *llmPath && load_llm(c, llmPath, llmProvider, error, errorSize))
        return -1;
    env_text("LITECRAB_WORKSPACE", c->workspaceRoot, sizeof c->workspaceRoot);
    env_text("LITECRAB_LOG_DIR", c->logDir, sizeof c->logDir);
    env_text("LITECRAB_BASE_URL", c->llmBaseUrl, sizeof c->llmBaseUrl);
    env_text("LITECRAB_API_KEY", c->llmApiKey, sizeof c->llmApiKey);
    env_text("LITECRAB_MODEL", c->llmModel, sizeof c->llmModel);
    env_text("LITECRAB_REASONING_EFFORT", c->reasoningEffort, sizeof c->reasoningEffort);
    const char* port = getenv("LITECRAB_PORT");
    if (port && *port)
    c->llm.baseUrl = c->llmBaseUrl;
    c->llm.apiKey = c->llmApiKey;
    c->llm.model = c->llmModel;
    c->llm.reasoningEffort = c->reasoningEffort;
    return validate(c, error, errorSize);
}
int LiteCrabConfigLoad(LiteCrabAppConfig* c,
                       const char* basePath,
                       const char* llmPath,
                       char* error,
                       size_t errorSize) {
    return LiteCrabConfigLoadProvider(c, basePath, llmPath, NULL, error, errorSize);
}
