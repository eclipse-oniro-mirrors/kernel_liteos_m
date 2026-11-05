#include "It_posix_rwlock.h"
#ifdef LOSCFG_KERNEL_SMP
#include <sched.h>
extern int sched_getaffinity(pid_t, size_t, cpu_set_t *);
extern int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
extern int pthread_attr_setaffinity_np(pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset);
#endif

static pthread_rwlock_t g_rwl071;
static volatile int g_rwl071Readers = 0;

static void *ReaderTask071(void *arg)
{
    int ret;

    ret = pthread_rwlock_rdlock(&g_rwl071);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    g_rwl071Readers++;
    LOS_TaskDelay(5);

    g_rwl071Readers--;
    pthread_rwlock_unlock(&g_rwl071);
EXIT:
    return NULL;
}

static UINT32 Testcase(void)
{
    int ret;
    pthread_t t1, t2;
    pthread_attr_t attr1, attr2;
    struct sched_param sp;

    g_rwl071Readers = 0;

    ret = pthread_rwlock_init(&g_rwl071, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_rdlock(&g_rwl071);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_DESTROY);

    ret = pthread_attr_init(&attr1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_UNLOCK);
    sp.sched_priority = RWLOCK_TEST_LOW_PRIO;
    pthread_attr_setschedparam(&attr1, &sp);
    pthread_attr_setinheritsched(&attr1, PTHREAD_EXPLICIT_SCHED);

#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t cpuset;
        (void)memset_s(&cpuset, sizeof(cpu_set_t), 0, sizeof(cpu_set_t));
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
        (VOID)pthread_attr_setaffinity_np(&attr1, sizeof(cpu_set_t), &cpuset);
    }
#endif
    ret = pthread_create(&t1, &attr1, ReaderTask071, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR1);

    ret = pthread_rwlock_unlock(&g_rwl071);

    LOS_TaskDelay(1);

    ret = pthread_attr_init(&attr2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN1);
    sp.sched_priority = RWLOCK_TEST_LOW_PRIO;
    pthread_attr_setschedparam(&attr2, &sp);
    pthread_attr_setinheritsched(&attr2, PTHREAD_EXPLICIT_SCHED);

#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t cpuset;
        (void)memset_s(&cpuset, sizeof(cpu_set_t), 0, sizeof(cpu_set_t));
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
        (VOID)pthread_attr_setaffinity_np(&attr2, sizeof(cpu_set_t), &cpuset);
    }
#endif
    ret = pthread_create(&t2, &attr2, ReaderTask071, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR2);

    LOS_TaskDelay(3);

    ICUNIT_GOTO_EQUAL(g_rwl071Readers, 2, g_rwl071Readers, EXIT_JOIN2);

EXIT_JOIN2:
    PosixPthreadDestroy(&attr2, t2);
EXIT_ATTR2:
    pthread_attr_destroy(&attr2);
EXIT_JOIN1:
    PosixPthreadDestroy(&attr1, t1);
EXIT_ATTR1:
    pthread_attr_destroy(&attr1);
EXIT_UNLOCK:
    pthread_rwlock_unlock(&g_rwl071);
EXIT_DESTROY:
    pthread_rwlock_destroy(&g_rwl071);
    return LOS_OK;
}

VOID ItPosixRwlock071(void)
{
    TEST_ADD_CASE("ItPosixRwlock071", Testcase, TEST_LIB, TEST_FN_COMPAT_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
