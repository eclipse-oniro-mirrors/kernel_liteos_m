#include "litecrab/approval.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int checks;
static int failures;
static const char* currentTest;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        checks++;                                                                         \
        if (!(condition)) {                                                               \
            fprintf(stderr,                                                               \
                    "FAIL [%s] %s:%d: %s\n",                                              \
                    currentTest ? currentTest : "unknown",                                \
                    __FILE__,                                                             \
                    __LINE__,                                                             \
                    #condition);                                                          \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

static void begin_test(const char* name) {
    currentTest = name;
    printf("approval-test: %s\n", name);
}

static char g_root[256];

static char* make_ws(char* out, size_t outSize, int n) {
    snprintf(out, outSize, "%s/ws%d", g_root, n);
    mkdir(out, 0755);
    return out;
}

static void pending_path(char* out, size_t outSize, const char* ws) {
    snprintf(out, outSize, "%s/.crab/approvals/pending.json", ws);
}

static int create_simple(const char* session, const char* call, char* id, size_t idSize) {
    return CrabApprovalCreate(session, call, "exec_program",
                              "{\"command\":\"main\",\"args\":[\"set_freq_band\",\"3\"]}",
                              id, idSize);
}

static void test_create_decide_consume(void) {
    begin_test("create -> decide(approve) -> consume roundtrip with persistence");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE], path[400];
    make_ws(ws, sizeof ws, 1);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);

    CHECK(create_simple("s1", "call-1", id, sizeof id) == CRAB_APPROVAL_OK);
    CHECK(id[0] == 'a' && strncmp(id, "apr-", 4) == 0);
    pending_path(path, sizeof path, ws);
    CHECK(access(path, F_OK) == 0);

    CrabApprovalEntry e;
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_OK);
    CHECK(e.state == CRAB_APPROVAL_WAITING && e.revision == 0);
    CHECK(strcmp(e.toolName, "exec_program") == 0);

    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_APPROVE, &e)
          == CRAB_APPROVAL_OK);
    CHECK(e.state == CRAB_APPROVAL_APPROVED && e.revision == 1);

    CHECK(CrabApprovalConsume("s1", id, &e) == CRAB_APPROVAL_OK);
    CHECK(e.state == CRAB_APPROVAL_CONSUMED && e.revision == 2);

    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_STATE);

    /* 决策后重启：条目已离开 WAITING，不再出现在 pending.json */
    CrabApprovalDestroy();
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_NOT_FOUND);
    CrabApprovalDestroy();
}

static void test_double_consume(void) {
    begin_test("double consume is rejected");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 2);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-2", id, sizeof id) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_APPROVE, NULL)
          == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalConsume("s1", id, NULL) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalConsume("s1", id, NULL) == CRAB_APPROVAL_ERR_STATE);
    CrabApprovalDestroy();
}

static void test_decide_on_denied(void) {
    begin_test("decide on already-denied entry is rejected");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 3);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-3", id, sizeof id) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_REJECT, NULL)
          == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_APPROVE, NULL)
          == CRAB_APPROVAL_ERR_STATE);
    CrabApprovalDestroy();
}

static void test_cross_session(void) {
    begin_test("cross-session decide and consume are rejected");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 4);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-4", id, sizeof id) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s2", id, CRAB_APPROVAL_DECISION_APPROVE, NULL)
          == CRAB_APPROVAL_ERR_NOT_FOUND);
    CHECK(CrabApprovalConsume("s2", id, NULL) == CRAB_APPROVAL_ERR_NOT_FOUND);
    CrabApprovalEntry e;
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
}

static void test_unknown_id(void) {
    begin_test("unknown interrupt id is rejected");
    char ws[300];
    make_ws(ws, sizeof ws, 5);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s1", "apr-99999999", CRAB_APPROVAL_DECISION_APPROVE, NULL)
          == CRAB_APPROVAL_ERR_NOT_FOUND);
    CHECK(CrabApprovalConsume("s1", "apr-99999999", NULL) == CRAB_APPROVAL_ERR_NOT_FOUND);
    CHECK(CrabApprovalFindWaiting("s1", "apr-99999999", NULL)
          == CRAB_APPROVAL_ERR_NOT_FOUND);
    CrabApprovalDestroy();
}

static void test_expiry(void) {
    begin_test("expired approval cannot be decided");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 6);
    CHECK(CrabApprovalInit(ws, 50) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-6", id, sizeof id) == CRAB_APPROVAL_OK);
    usleep(120 * 1000);
    CrabApprovalEntry e;
    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_APPROVE, &e)
          == CRAB_APPROVAL_ERR_EXPIRED);
    CHECK(e.state == CRAB_APPROVAL_EXPIRED);
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_STATE);
    /* 过期条目不再落盘 */
    CrabApprovalDestroy();
    CHECK(CrabApprovalInit(ws, 50) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_NOT_FOUND);
    CrabApprovalDestroy();
}

static int rewrite_file_replacing(const char* path, const char* needle, char replacement) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    char buf[128 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char* hit = strstr(buf, needle);
    if (!hit)
        return -1;
    *hit = replacement;
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(buf, 1, n, f);
    fclose(f);
    return 0;
}

static void test_tampered_file(void) {
    begin_test("tampered pending.json invalidates all entries on load");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE], path[400];
    make_ws(ws, sizeof ws, 7);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-7", id, sizeof id) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
    pending_path(path, sizeof path, ws);
    /* 篡改 argsJson 内容（"main" -> "nain"）：摘要不再自洽 */
    CHECK(rewrite_file_replacing(path, "main", 'n') == 0);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CrabApprovalEntry e;
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_NOT_FOUND);
    CrabApprovalDestroy();
}

static void test_restart_recovery(void) {
    begin_test("pending approvals survive restart");
    char ws[300], ids[3][CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 8);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    for (int i = 0; i < 3; i++)
        CHECK(create_simple("s1", "call-8", ids[i], sizeof ids[i]) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CrabApprovalEntry e;
    for (int i = 0; i < 3; i++) {
        CHECK(CrabApprovalFindWaiting("s1", ids[i], &e) == CRAB_APPROVAL_OK);
        CHECK(e.state == CRAB_APPROVAL_WAITING);
    }
    CHECK(CrabApprovalDecide("s1", ids[0], CRAB_APPROVAL_DECISION_APPROVE, NULL)
          == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalConsume("s1", ids[0], NULL) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
}

static void test_corrupt_file(void) {
    begin_test("corrupt pending.json is discarded but init succeeds");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE], path[400];
    make_ws(ws, sizeof ws, 9);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-9", id, sizeof id) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
    pending_path(path, sizeof path, ws);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL);
    fwrite("this is not json {{{", 1, 20, f);
    fclose(f);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CrabApprovalEntry e;
    CHECK(CrabApprovalFindWaiting("s1", id, &e) == CRAB_APPROVAL_ERR_NOT_FOUND);
    /* 存储仍可用：可以创建新审批 */
    char id2[CRAB_APPROVAL_ID_SIZE];
    CHECK(create_simple("s1", "call-9b", id2, sizeof id2) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
}

static void test_storage_failure(void) {
    begin_test("unwritable workspace fails creation closed");
    CHECK(CrabApprovalInit("/proc/litecrab-approval-unwritable", 600000)
          == CRAB_APPROVAL_OK);
    char id[CRAB_APPROVAL_ID_SIZE];
    CHECK(create_simple("s1", "call-10", id, sizeof id) == CRAB_APPROVAL_ERR_STORAGE);
    CrabApprovalDestroy();
}

static void test_capacity(void) {
    begin_test("queue capacity is enforced at 16");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 11);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    for (int i = 0; i < CRAB_APPROVAL_MAX_PENDING; i++) {
        char call[32];
        snprintf(call, sizeof call, "call-11-%d", i);
        CHECK(create_simple("s1", call, id, sizeof id) == CRAB_APPROVAL_OK);
    }
    CHECK(create_simple("s1", "call-11-overflow", id, sizeof id)
          == CRAB_APPROVAL_ERR_CAPACITY);
    CrabApprovalDestroy();
}

static void test_args_limits(void) {
    begin_test("oversized and malformed args are rejected");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 12);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    char big[2600];
    big[0] = 0;
    strcat(big, "{\"pad\":\"");
    memset(big + strlen(big), 'x', sizeof big - strlen(big) - 3);
    strcat(big, "\"}");
    CHECK(CrabApprovalCreate("s1", "call-12", "exec_program", big, id, sizeof id)
          == CRAB_APPROVAL_ERR_ARGS_TOO_LARGE);
    CHECK(CrabApprovalCreate("s1", "call-12", "exec_program", "not json", id, sizeof id)
          == CRAB_APPROVAL_ERR_INVALID_ARG);
    CHECK(CrabApprovalCreate(NULL, "call-12", "exec_program", "{}", id, sizeof id)
          == CRAB_APPROVAL_ERR_INVALID_ARG);
    CHECK(CrabApprovalCreate("s1", "call-12", NULL, "{}", id, sizeof id)
          == CRAB_APPROVAL_ERR_INVALID_ARG);
    CrabApprovalDestroy();
}

static void test_session_allow(void) {
    begin_test("session allowlist grant and check");
    char ws[300];
    make_ws(ws, sizeof ws, 13);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(CrabSessionAllowGrant("s1", "exec_program") == CRAB_APPROVAL_OK);
    CHECK(CrabSessionAllowCheck("s1", "exec_program") == 1);
    CHECK(CrabSessionAllowCheck("s1", "write") == 0);
    CHECK(CrabSessionAllowCheck("s2", "exec_program") == 0);
    /* 重复授予幂等 */
    CHECK(CrabSessionAllowGrant("s1", "exec_program") == CRAB_APPROVAL_OK);
    CrabSessionAllowClearSession("s1");
    CHECK(CrabSessionAllowCheck("s1", "exec_program") == 0);
    CrabApprovalDestroy();
}

static void test_session_allow_capacity(void) {
    begin_test("session allowlist capacity is enforced");
    char ws[300];
    make_ws(ws, sizeof ws, 14);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    for (int i = 0; i < CRAB_APPROVAL_MAX_SESSION_ALLOW; i++) {
        char session[32];
        snprintf(session, sizeof session, "s-%d", i);
        CHECK(CrabSessionAllowGrant(session, "exec_program") == CRAB_APPROVAL_OK);
    }
    CHECK(CrabSessionAllowGrant("s-overflow", "exec_program")
          == CRAB_APPROVAL_ERR_CAPACITY);
    CHECK(CrabSessionAllowCheck("s-overflow", "exec_program") == 0);
    CrabApprovalDestroy();
}

static void test_allow_not_persisted(void) {
    begin_test("session allowlist does not survive restart");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 15);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-15", id, sizeof id) == CRAB_APPROVAL_OK);
    CHECK(CrabApprovalDecide("s1", id, CRAB_APPROVAL_DECISION_ALWAYS_ALLOW, NULL)
          == CRAB_APPROVAL_OK);
    CHECK(CrabSessionAllowCheck("s1", "exec_program") == 1);
    CrabApprovalDestroy();
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(CrabSessionAllowCheck("s1", "exec_program") == 0);
    CrabApprovalDestroy();
}

typedef struct {
    const char* session;
    const char* id;
    int rc;
} DecideThreadArg;

static void* decide_thread(void* p) {
    DecideThreadArg* a = p;
    a->rc = CrabApprovalDecide(a->session, a->id, CRAB_APPROVAL_DECISION_APPROVE, NULL);
    return NULL;
}

static void test_concurrent_decide(void) {
    begin_test("concurrent decide: exactly one wins");
    char ws[300], id[CRAB_APPROVAL_ID_SIZE];
    make_ws(ws, sizeof ws, 16);
    CHECK(CrabApprovalInit(ws, 600000) == CRAB_APPROVAL_OK);
    CHECK(create_simple("s1", "call-16", id, sizeof id) == CRAB_APPROVAL_OK);
    DecideThreadArg a = {"s1", id, 0}, b = {"s1", id, 0};
    pthread_t ta, tb;
    CHECK(pthread_create(&ta, NULL, decide_thread, &a) == 0);
    CHECK(pthread_create(&tb, NULL, decide_thread, &b) == 0);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    CHECK((a.rc == CRAB_APPROVAL_OK) != (b.rc == CRAB_APPROVAL_OK));
    CHECK(a.rc == CRAB_APPROVAL_OK || b.rc == CRAB_APPROVAL_OK);
    int loser = a.rc == CRAB_APPROVAL_OK ? b.rc : a.rc;
    CHECK(loser == CRAB_APPROVAL_ERR_STATE);
    CHECK(CrabApprovalConsume("s1", id, NULL) == CRAB_APPROVAL_OK);
    CrabApprovalDestroy();
}

int main(void) {
    char template[] = "/tmp/litecrab-approval-tests-XXXXXX";
    char* root = mkdtemp(template);
    if (!root) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_root, sizeof g_root, "%s", root);

    test_create_decide_consume();
    test_double_consume();
    test_decide_on_denied();
    test_cross_session();
    test_unknown_id();
    test_expiry();
    test_tampered_file();
    test_restart_recovery();
    test_corrupt_file();
    test_storage_failure();
    test_capacity();
    test_args_limits();
    test_session_allow();
    test_session_allow_capacity();
    test_allow_not_persisted();
    test_concurrent_decide();

    printf("approval checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
