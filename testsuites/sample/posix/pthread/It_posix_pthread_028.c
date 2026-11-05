#define _GNU_SOURCE
#include "It_posix_pthread.h"
#ifdef LOSCFG_KERNEL_SMP
#include <sched.h>
extern int sched_getaffinity(pid_t, size_t, cpu_set_t *);
extern int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
#endif

#define THREAD_NAME_LEN 32

static UINT32 g_nameTestResult = 0;

static VOID *NameThreadFunc(VOID *arg)
{
    (void)arg;
    char buf[THREAD_NAME_LEN] = {0};

    g_nameTestResult = 0;

    if (pthread_setname_np(pthread_self(), "nameTestThread") != 0) {
        g_nameTestResult = 1;
        return NULL;
    }

    if (pthread_getname_np(pthread_self(), buf, THREAD_NAME_LEN) != 0) {
        g_nameTestResult = 2;
        return NULL;
    }

    if (strcmp(buf, "nameTestThread") != 0) {
        g_nameTestResult = 3;
        return NULL;
    }

    if (pthread_getname_np(pthread_self(), buf, 1) == 0) {
        g_nameTestResult = 4;
        return NULL;
    }

    return NULL;
}

static UINT32 Testcase(VOID)
{
    int ret;
    pthread_t thread;

    ret = pthread_create(&thread, NULL, NameThreadFunc, NULL);
#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t inheritCpuset;
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &inheritCpuset);
        (void)sched_setaffinity((pid_t)(UINTPTR)thread, sizeof(cpu_set_t), &inheritCpuset);
    }
#endif
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_join(thread, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ICUNIT_ASSERT_EQUAL(g_nameTestResult, 0, g_nameTestResult);

    return LOS_OK;
}

/**
 * @tc.name: ItPosixPthread028
 * @tc.desc: Test interface pthread_setname_np and pthread_getname_np
 * @tc.type: FUNC
 */

VOID ItPosixPthread028(VOID)
{
    TEST_ADD_CASE("ItPosixPthread028", Testcase, TEST_LIB, TEST_FN_COMPAT_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}