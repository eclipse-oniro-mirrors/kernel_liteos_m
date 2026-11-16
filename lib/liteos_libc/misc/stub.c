/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: C++ ABI link stubs (__cxa_atexit/__dso_handle)
 */

#include "los_typedef.h"

/* __dso_handle is used during execution of global destructors via _fini array,
 * or a system crash occurs when using c++.
 */
#ifndef LOSCFG_MAKE_COMPILER_LIBS
#ifndef __ICCARM__
void *__dso_handle = NULL;
#endif
#endif

/* libstdc++.a init_array segment uses it */
#ifndef __ICCARM__
void __cxa_atexit(void (*func)(void *), void *arg1, void *arg2)
{
    (VOID)func;
    (VOID)arg1;
    (VOID)arg2;
}
#endif

/* ARM EABI variant (arm-none-eabi-gcc emits this instead of __cxa_atexit) */
void __aeabi_atexit(void *arg, void (*func)(void *), void *dso)
{
    (VOID)arg;
    (VOID)func;
    (VOID)dso;
}
