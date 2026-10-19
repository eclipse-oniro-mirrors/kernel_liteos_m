#include "litecrab/approval.h"

#include "litecrab/json.h"
#include "litecrab/observability.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- 内部状态 */

typedef struct {
    CrabApprovalEntry entry;
    int used;
} ApprovalSlot;

typedef struct {
    char sessionId[CRAB_APPROVAL_SESSION_ID_SIZE];
    char toolName[CRAB_APPROVAL_TOOL_NAME_SIZE];
    int used;
} SessionAllowSlot;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static ApprovalSlot g_slots[CRAB_APPROVAL_MAX_PENDING];
static SessionAllowSlot g_allows[CRAB_APPROVAL_MAX_SESSION_ALLOW];
static int g_initialized;
static int g_storageAvailable;
static long long g_timeoutMs;
static unsigned long g_sequence;
static char g_pendingDir[512];
static char g_pendingPath[540];

#define CRAB_APPROVAL_FILE_LIMIT (256 * 1024)
#define CRAB_APPROVAL_BUF_LIMIT (96 * 1024)

/* ---------------------------------------------------------------- 小工具 */

/* 审批有效期跨重启存活，必须使用墙钟；单调时钟重启后会归零。 */
static long long approval_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long) ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static unsigned int approval_fnv1a(const char* s) {
    unsigned int hash = 2166136261u;
    for (const unsigned char* p = (const unsigned char*) s; *p; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static int mkdir_if_missing(const char* path) {
    return mkdir(path, 0700) == 0 || errno == EEXIST ? 0 : -1;
}

static int ensure_dirs_locked(void) {
    char parent[512];
    snprintf(parent, sizeof parent, "%s", g_pendingDir);
    char* slash = strrchr(parent, '/');
    if (!slash || slash == parent)
        return -1;
    *slash = 0;
    if (mkdir_if_missing(parent))
        return -1;
    return mkdir_if_missing(g_pendingDir);
}

static int write_all_fd(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t) n;
    }
    return 0;
}

static int fsync_dir(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int rc = fsync(fd);
    close(fd);
    return rc;
}

static void transition_locked(ApprovalSlot* s, int to, const char* reason) {
    int from = s->entry.state;
    s->entry.state = to;
    s->entry.revision++;
    LogPrint("[approval] event=approval.state_changed id=%s session=%.32s tool=%s "
             "from=%d to=%d revision=%u reason=%s",
             s->entry.interruptId, s->entry.sessionId, s->entry.toolName,
             from, to, s->entry.revision, reason);
}

static int find_slot_locked(const char* interruptId) {
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++)
        if (g_slots[i].used && !strcmp(g_slots[i].entry.interruptId, interruptId))
            return i;
    return -1;
}

static int waiting_count_locked(void) {
    int n = 0;
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++)
        if (g_slots[i].used && g_slots[i].entry.state == CRAB_APPROVAL_WAITING)
            n++;
    return n;
}

static int free_slot_locked(void) {
    int unused = -1;
    int oldest = -1;
    long long oldestMs = 0;
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++) {
        if (!g_slots[i].used)
            return i;
        if (g_slots[i].entry.state != CRAB_APPROVAL_WAITING &&
            (oldest < 0 || g_slots[i].entry.createdAtMs < oldestMs)) {
            oldest = i;
            oldestMs = g_slots[i].entry.createdAtMs;
        }
    }
    return oldest; /* 全满且全 WAITING 时为 -1，由容量检查先行拦截 */
}

/* ---------------------------------------------------------------- 持久化 */

static int persist_locked(void) {
    if (!g_storageAvailable)
        return -1;
    char* buf = malloc(CRAB_APPROVAL_BUF_LIMIT);
    if (!buf)
        return -1;
    LjBuf b;
    LjBufInit(&b, buf, CRAB_APPROVAL_BUF_LIMIT);
    LjAppend(&b, "[");
    int first = 1;
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++) {
        const CrabApprovalEntry* e = &g_slots[i].entry;
        if (!g_slots[i].used || e->state != CRAB_APPROVAL_WAITING)
            continue;
        if (!first)
            LjAppend(&b, ",");
        first = 0;
        LjAppend(&b, "{\"interruptId\":");
        LjAppendJsonString(&b, e->interruptId);
        LjAppend(&b, ",\"sessionId\":");
        LjAppendJsonString(&b, e->sessionId);
        LjAppend(&b, ",\"toolCallId\":");
        LjAppendJsonString(&b, e->toolCallId);
        LjAppend(&b, ",\"toolName\":");
        LjAppendJsonString(&b, e->toolName);
        LjAppend(&b, ",\"argsJson\":");
        LjAppendJsonString(&b, e->argsJson);
        LjAppend(&b, ",\"argsHash\":%u,\"createdAtMs\":%lld,\"expiresAtMs\":%lld,\"revision\":%u}",
                 e->argsHash, e->createdAtMs, e->expiresAtMs, e->revision);
    }
    LjAppend(&b, "]");
    int rc = -1;
    do {
        if (b.failed)
            break;
        char tmp[560];
        if (snprintf(tmp, sizeof tmp, "%s.tmp", g_pendingPath) >= (int) sizeof tmp)
            break;
        int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            break;
        if (write_all_fd(fd, b.data, b.len) || fsync(fd)) {
            close(fd);
            unlink(tmp);
            break;
        }
        close(fd);
        if (rename(tmp, g_pendingPath))
            break;
        rc = fsync_dir(g_pendingDir);
    } while (0);
    free(buf);
    return rc;
}

static int entry_str_field(const char* json, LjToken* tokens, int count,
                           int obj, const char* key, char* out, size_t outSize) {
    int tok = LjObjectGet(json, tokens, count, obj, key);
    if (tok < 0 || tokens[tok].type != LJ_STRING)
        return -1;
    return LjString(json, &tokens[tok], out, outSize);
}

static int entry_i64_field(const char* json, LjToken* tokens, int count,
                           int obj, const char* key, int64_t* out) {
    int tok = LjObjectGet(json, tokens, count, obj, key);
    if (tok < 0)
        return -1;
    return LjInt64(json, &tokens[tok], out);
}

/* 加载 pending.json：只恢复 WAITING 条目；任何条目非法即整文件作废。 */
static void load_locked(void) {
    FILE* f = fopen(g_pendingPath, "rb");
    if (!f)
        return;
    char* buf = NULL;
    LjToken* tokens = NULL;
    do {
        long fsz;
        if (fseek(f, 0, SEEK_END) || (fsz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
            break;
        if (fsz == 0)
            break; /* 空文件视为无待决条目 */
        if (fsz > (long) CRAB_APPROVAL_FILE_LIMIT)
            break;
        buf = malloc((size_t) fsz + 1);
        if (!buf)
            break;
        if (fread(buf, 1, (size_t) fsz, f) != (size_t) fsz)
            break;
        buf[fsz] = 0;
        if (LjValidate(buf, LJ_ARRAY))
            break;
        tokens = calloc((size_t) fsz + 1, sizeof *tokens);
        if (!tokens)
            break;
        LjParser parser;
        LjInit(&parser);
        int count = LjParse(&parser, buf, (size_t) fsz, tokens, (unsigned) fsz + 1);
        if (count <= 0 || tokens[0].type != LJ_ARRAY ||
            tokens[0].size <= 0 || tokens[0].size > CRAB_APPROVAL_MAX_PENDING)
            break;
        int loaded = 0;
        for (int i = 0; i < tokens[0].size; i++) {
            int obj = LjArrayGet(tokens, count, 0, i);
            if (obj < 0 || tokens[obj].type != LJ_OBJECT)
                break;
            CrabApprovalEntry e;
            memset(&e, 0, sizeof e);
            int64_t hash = 0, created = 0, expires = 0, revision = 0;
            if (entry_str_field(buf, tokens, count, obj, "interruptId",
                                e.interruptId, sizeof e.interruptId) ||
                entry_str_field(buf, tokens, count, obj, "sessionId",
                                e.sessionId, sizeof e.sessionId) ||
                entry_str_field(buf, tokens, count, obj, "toolCallId",
                                e.toolCallId, sizeof e.toolCallId) ||
                entry_str_field(buf, tokens, count, obj, "toolName",
                                e.toolName, sizeof e.toolName) ||
                entry_str_field(buf, tokens, count, obj, "argsJson",
                                e.argsJson, sizeof e.argsJson) ||
                entry_i64_field(buf, tokens, count, obj, "argsHash", &hash) ||
                entry_i64_field(buf, tokens, count, obj, "createdAtMs", &created) ||
                entry_i64_field(buf, tokens, count, obj, "expiresAtMs", &expires))
                break;
            if (strncmp(e.interruptId, "apr-", 4) != 0 || !e.sessionId[0] ||
                !e.toolName[0] || created <= 0 || expires <= created)
                break;
            e.argsHash = (unsigned int) hash;
            e.createdAtMs = created;
            e.expiresAtMs = expires;
            e.revision = revision > 0 ? (unsigned) revision : 0;
            e.state = CRAB_APPROVAL_WAITING;
            if (approval_fnv1a(e.argsJson) != e.argsHash)
                break;
            g_slots[loaded].used = 1;
            g_slots[loaded].entry = e;
            unsigned long seq = 0;
            if (sscanf(e.interruptId, "apr-%lu", &seq) == 1 && seq > g_sequence)
                g_sequence = seq;
            loaded++;
        }
        if (loaded != tokens[0].size)
            break; /* 中途遇到非法条目：整文件作废 */
        free(tokens);
        free(buf);
        fclose(f);
        return;
    } while (0);
    memset(g_slots, 0, sizeof g_slots);
    LogPrint("[approval] ERROR pending approvals file invalid; "
             "all pending entries discarded path=%s",
             g_pendingPath);
    free(buf);
    free(tokens);
    fclose(f);
}

/* ---------------------------------------------------------------- 会话白名单 */

static int session_allow_find_locked(const char* sessionId, const char* toolName) {
    for (int i = 0; i < CRAB_APPROVAL_MAX_SESSION_ALLOW; i++)
        if (g_allows[i].used && !strcmp(g_allows[i].sessionId, sessionId) &&
            !strcmp(g_allows[i].toolName, toolName))
            return i;
    return -1;
}

/* 返回 0 成功；白名单已满返回 -1（调用方决定降级语义）。 */
static int session_allow_grant_locked(const char* sessionId, const char* toolName) {
    if (session_allow_find_locked(sessionId, toolName) >= 0)
        return 0;
    for (int i = 0; i < CRAB_APPROVAL_MAX_SESSION_ALLOW; i++) {
        if (!g_allows[i].used) {
            g_allows[i].used = 1;
            snprintf(g_allows[i].sessionId, sizeof g_allows[i].sessionId, "%s", sessionId);
            snprintf(g_allows[i].toolName, sizeof g_allows[i].toolName, "%s", toolName);
            LogPrint("[approval] event=approval.session_allowed session=%.32s tool=%s",
                     sessionId, toolName);
            return 0;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- 清扫 */

typedef struct {
    int index;
    int prevState;
    unsigned prevRevision;
} SweepUndo;

static void sweep_locked(void) {
    SweepUndo undo[CRAB_APPROVAL_MAX_PENDING];
    int changed = 0;
    long long now = approval_now_ms();
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++) {
        if (!g_slots[i].used || g_slots[i].entry.state != CRAB_APPROVAL_WAITING)
            continue;
        if (now < g_slots[i].entry.expiresAtMs)
            continue;
        undo[changed].index = i;
        undo[changed].prevState = CRAB_APPROVAL_WAITING;
        undo[changed].prevRevision = g_slots[i].entry.revision;
        transition_locked(&g_slots[i], CRAB_APPROVAL_EXPIRED, "expired");
        changed++;
    }
    if (!changed)
        return;
    if (persist_locked()) {
        for (int i = 0; i < changed; i++) { /* 落盘失败回滚，保持内存与磁盘一致 */
            g_slots[undo[i].index].entry.state = undo[i].prevState;
            g_slots[undo[i].index].entry.revision = undo[i].prevRevision;
        }
        LogPrint("[approval] ERROR expired sweep persist failed; will retry");
    }
}

/* ---------------------------------------------------------------- 公共 API */

int CrabApprovalInit(const char* workspaceDir, long long timeoutMs) {
    if (!workspaceDir || !*workspaceDir || timeoutMs <= 0)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    pthread_mutex_lock(&g_lock);
    memset(g_slots, 0, sizeof g_slots);
    memset(g_allows, 0, sizeof g_allows);
    g_sequence = 0;
    g_timeoutMs = timeoutMs;
    g_storageAvailable = 0;
    g_initialized = 0;
    if (snprintf(g_pendingDir, sizeof g_pendingDir, "%s/.crab/approvals", workspaceDir)
            >= (int) sizeof g_pendingDir ||
        snprintf(g_pendingPath, sizeof g_pendingPath, "%s/pending.json", g_pendingDir)
            >= (int) sizeof g_pendingPath) {
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    }
    g_storageAvailable = ensure_dirs_locked() == 0;
    load_locked();
    g_initialized = 1;
    int pending = waiting_count_locked();
    pthread_mutex_unlock(&g_lock);
    if (!g_storageAvailable)
        LogPrint("[approval] WARNING storage unavailable (cannot create %s); "
                 "approval creation will fail closed",
                 g_pendingDir);
    LogPrint("[approval] store initialized pending=%d timeoutMs=%lld storage=%s",
             pending, timeoutMs, g_storageAvailable ? "ok" : "unavailable");
    return CRAB_APPROVAL_OK;
}

void CrabApprovalDestroy(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_slots, 0, sizeof g_slots);
    memset(g_allows, 0, sizeof g_allows);
    g_initialized = 0;
    g_storageAvailable = 0;
    g_sequence = 0;
    pthread_mutex_unlock(&g_lock);
}

int CrabApprovalCreate(const char* sessionId,
                       const char* toolCallId,
                       const char* toolName,
                       const char* argsJson,
                       char* interruptIdOut,
                       size_t interruptIdSize) {
    if (!g_initialized)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!sessionId || !*sessionId || !toolName || !*toolName || !argsJson)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!toolCallId)
        toolCallId = "";
    if (strlen(sessionId) >= CRAB_APPROVAL_SESSION_ID_SIZE ||
        strlen(toolCallId) >= CRAB_APPROVAL_TOOL_CALL_ID_SIZE ||
        strlen(toolName) >= CRAB_APPROVAL_TOOL_NAME_SIZE)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (strlen(argsJson) >= CRAB_APPROVAL_ARGS_LIMIT)
        return CRAB_APPROVAL_ERR_ARGS_TOO_LARGE;
    if (LjValidate(argsJson, LJ_OBJECT))
        return CRAB_APPROVAL_ERR_INVALID_ARG;

    pthread_mutex_lock(&g_lock);
    sweep_locked();
    if (waiting_count_locked() >= CRAB_APPROVAL_MAX_PENDING) {
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_CAPACITY;
    }
    int idx = free_slot_locked();
    if (idx < 0) {
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_CAPACITY;
    }
    ApprovalSlot* s = &g_slots[idx];
    memset(s, 0, sizeof *s);
    s->used = 1;
    snprintf(s->entry.interruptId, sizeof s->entry.interruptId, "apr-%08lu", ++g_sequence);
    snprintf(s->entry.sessionId, sizeof s->entry.sessionId, "%s", sessionId);
    snprintf(s->entry.toolCallId, sizeof s->entry.toolCallId, "%s", toolCallId);
    snprintf(s->entry.toolName, sizeof s->entry.toolName, "%s", toolName);
    snprintf(s->entry.argsJson, sizeof s->entry.argsJson, "%s", argsJson);
    s->entry.argsHash = approval_fnv1a(argsJson);
    s->entry.createdAtMs = approval_now_ms();
    s->entry.expiresAtMs = s->entry.createdAtMs + g_timeoutMs;
    s->entry.state = CRAB_APPROVAL_WAITING;
    s->entry.revision = 0;
    if (persist_locked()) {
        memset(s, 0, sizeof *s); /* 无法持久化就不放行 */
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_STORAGE;
    }
    if (interruptIdOut && interruptIdSize)
        snprintf(interruptIdOut, interruptIdSize, "%s", s->entry.interruptId);
    LogPrint("[approval] event=approval.created id=%s session=%.32s tool=%s "
             "argsBytes=%zu expiresAtMs=%lld",
             s->entry.interruptId, sessionId, toolName, strlen(argsJson),
             s->entry.expiresAtMs);
    pthread_mutex_unlock(&g_lock);
    return CRAB_APPROVAL_OK;
}

/* 定位并校验 WAITING 条目；返回槽下标，失败返回负的错误码。
 * 失败时若 entryOut 非空，回填条目快照（含已迁移后的状态）供调用方说明原因。 */
static int locate_waiting_locked(const char* sessionId, const char* interruptId,
                                 CrabApprovalEntry* entryOut) {
    int idx = find_slot_locked(interruptId);
    if (idx < 0 || strcmp(g_slots[idx].entry.sessionId, sessionId))
        return CRAB_APPROVAL_ERR_NOT_FOUND;
    if (g_slots[idx].entry.state != CRAB_APPROVAL_WAITING)
        return CRAB_APPROVAL_ERR_STATE;
    long long now = approval_now_ms();
    if (now >= g_slots[idx].entry.expiresAtMs) {
        ApprovalSlot* s = &g_slots[idx];
        int prevState = s->entry.state;
        unsigned prevRevision = s->entry.revision;
        transition_locked(s, CRAB_APPROVAL_EXPIRED, "expired");
        if (persist_locked()) {
            s->entry.state = prevState;
            s->entry.revision = prevRevision;
            LogPrint("[approval] ERROR expiry persist failed");
        }
        if (entryOut)
            *entryOut = s->entry;
        return CRAB_APPROVAL_ERR_EXPIRED;
    }
    if (approval_fnv1a(g_slots[idx].entry.argsJson) != g_slots[idx].entry.argsHash) {
        ApprovalSlot* s = &g_slots[idx];
        int prevState = s->entry.state;
        unsigned prevRevision = s->entry.revision;
        transition_locked(s, CRAB_APPROVAL_EXPIRED, "hash-mismatch");
        if (persist_locked()) {
            s->entry.state = prevState;
            s->entry.revision = prevRevision;
            LogPrint("[approval] ERROR invalidation persist failed");
        }
        if (entryOut)
            *entryOut = s->entry;
        return CRAB_APPROVAL_ERR_HASH;
    }
    return idx;
}

int CrabApprovalDecide(const char* sessionId,
                       const char* interruptId,
                       CrabApprovalDecision decision,
                       CrabApprovalEntry* entryOut) {
    if (!g_initialized)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!sessionId || !*sessionId || !interruptId || !*interruptId)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (decision != CRAB_APPROVAL_DECISION_APPROVE &&
        decision != CRAB_APPROVAL_DECISION_REJECT &&
        decision != CRAB_APPROVAL_DECISION_ALWAYS_ALLOW)
        return CRAB_APPROVAL_ERR_INVALID_ARG;

    pthread_mutex_lock(&g_lock);
    int idx = locate_waiting_locked(sessionId, interruptId, entryOut);
    if (idx < 0) {
        pthread_mutex_unlock(&g_lock);
        return idx;
    }
    ApprovalSlot* s = &g_slots[idx];
    int prevState = s->entry.state;
    unsigned prevRevision = s->entry.revision;
    if (decision == CRAB_APPROVAL_DECISION_REJECT) {
        transition_locked(s, CRAB_APPROVAL_DENIED, "operator-reject");
    } else {
        transition_locked(s, CRAB_APPROVAL_APPROVED,
                          decision == CRAB_APPROVAL_DECISION_ALWAYS_ALLOW
                              ? "operator-always-allow"
                              : "operator-approve");
    }
    if (persist_locked()) {
        s->entry.state = prevState; /* 决策无法落盘则不生效 */
        s->entry.revision = prevRevision;
        pthread_mutex_unlock(&g_lock);
        LogPrint("[approval] ERROR decide persist failed id=%s", interruptId);
        return CRAB_APPROVAL_ERR_STORAGE;
    }
    if (decision == CRAB_APPROVAL_DECISION_ALWAYS_ALLOW &&
        session_allow_grant_locked(s->entry.sessionId, s->entry.toolName)) {
        /* 白名单满：降级为单次批准（向更多确认倾斜，不向更少倾斜） */
        LogPrint("[approval] WARNING session allowlist full; "
                 "always_allow degraded to single approve id=%s",
                 interruptId);
    }
    if (entryOut)
        *entryOut = s->entry;
    pthread_mutex_unlock(&g_lock);
    return CRAB_APPROVAL_OK;
}

int CrabApprovalConsume(const char* sessionId,
                        const char* interruptId,
                        CrabApprovalEntry* entryOut) {
    if (!g_initialized)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!sessionId || !*sessionId || !interruptId || !*interruptId)
        return CRAB_APPROVAL_ERR_INVALID_ARG;

    pthread_mutex_lock(&g_lock);
    int idx = find_slot_locked(interruptId);
    if (idx < 0 || strcmp(g_slots[idx].entry.sessionId, sessionId)) {
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_NOT_FOUND;
    }
    ApprovalSlot* s = &g_slots[idx];
    if (s->entry.state != CRAB_APPROVAL_APPROVED) {
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_STATE;
    }
    if (approval_now_ms() >= s->entry.expiresAtMs) {
        int prevState = s->entry.state;
        unsigned prevRevision = s->entry.revision;
        transition_locked(s, CRAB_APPROVAL_EXPIRED, "expired-before-consume");
        if (persist_locked()) {
            s->entry.state = prevState;
            s->entry.revision = prevRevision;
        }
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_EXPIRED;
    }
    if (approval_fnv1a(s->entry.argsJson) != s->entry.argsHash) {
        int prevState = s->entry.state;
        unsigned prevRevision = s->entry.revision;
        transition_locked(s, CRAB_APPROVAL_EXPIRED, "hash-mismatch");
        if (persist_locked()) {
            s->entry.state = prevState;
            s->entry.revision = prevRevision;
        }
        pthread_mutex_unlock(&g_lock);
        return CRAB_APPROVAL_ERR_HASH;
    }
    transition_locked(s, CRAB_APPROVAL_CONSUMED, "executed");
    if (entryOut)
        *entryOut = s->entry;
    pthread_mutex_unlock(&g_lock);
    return CRAB_APPROVAL_OK;
}

int CrabApprovalFindWaiting(const char* sessionId,
                            const char* interruptId,
                            CrabApprovalEntry* entryOut) {
    if (!g_initialized)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!sessionId || !*sessionId || !interruptId || !*interruptId)
        return CRAB_APPROVAL_ERR_INVALID_ARG;

    pthread_mutex_lock(&g_lock);
    int idx = locate_waiting_locked(sessionId, interruptId, entryOut);
    if (idx < 0) {
        pthread_mutex_unlock(&g_lock);
        return idx;
    }
    if (entryOut)
        *entryOut = g_slots[idx].entry;
    pthread_mutex_unlock(&g_lock);
    return CRAB_APPROVAL_OK;
}

int CrabApprovalFindWaitingBySession(const char* sessionId, CrabApprovalEntry* entryOut) {
    if (!g_initialized || !sessionId || !*sessionId)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    pthread_mutex_lock(&g_lock);
    sweep_locked();
    int best = -1;
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++)
        if (g_slots[i].used && g_slots[i].entry.state == CRAB_APPROVAL_WAITING &&
            !strcmp(g_slots[i].entry.sessionId, sessionId) &&
            (best < 0 || g_slots[i].entry.expiresAtMs < g_slots[best].entry.expiresAtMs))
            best = i;
    int rc = CRAB_APPROVAL_ERR_NOT_FOUND;
    if (best >= 0 && entryOut)
        *entryOut = g_slots[best].entry;
    pthread_mutex_unlock(&g_lock);
    return best >= 0 ? CRAB_APPROVAL_OK : rc;
}

int CrabApprovalIsReady(void) {
    pthread_mutex_lock(&g_lock);
    int ready = g_initialized;
    pthread_mutex_unlock(&g_lock);
    return ready;
}

void CrabApprovalSweepExpired(void) {
    if (!g_initialized)
        return;
    pthread_mutex_lock(&g_lock);
    sweep_locked();
    pthread_mutex_unlock(&g_lock);
}

int CrabSessionAllowCheck(const char* sessionId, const char* toolName) {
    if (!g_initialized || !sessionId || !toolName)
        return 0;
    pthread_mutex_lock(&g_lock);
    int hit = session_allow_find_locked(sessionId, toolName) >= 0;
    pthread_mutex_unlock(&g_lock);
    return hit;
}

int CrabSessionAllowGrant(const char* sessionId, const char* toolName) {
    if (!g_initialized)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    if (!sessionId || !*sessionId || !toolName || !*toolName ||
        strlen(sessionId) >= CRAB_APPROVAL_SESSION_ID_SIZE ||
        strlen(toolName) >= CRAB_APPROVAL_TOOL_NAME_SIZE)
        return CRAB_APPROVAL_ERR_INVALID_ARG;
    pthread_mutex_lock(&g_lock);
    int rc = session_allow_grant_locked(sessionId, toolName)
                 ? CRAB_APPROVAL_ERR_CAPACITY
                 : CRAB_APPROVAL_OK;
    pthread_mutex_unlock(&g_lock);
    return rc;
}

void CrabSessionAllowClearSession(const char* sessionId) {
    if (!g_initialized || !sessionId)
        return;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < CRAB_APPROVAL_MAX_SESSION_ALLOW; i++)
        if (g_allows[i].used && !strcmp(g_allows[i].sessionId, sessionId))
            g_allows[i].used = 0;
    pthread_mutex_unlock(&g_lock);
}
