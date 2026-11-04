/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * sample/posix 目录的独立套件入口：目录内全部子套件的聚合调用（与历史上
 * unittest/posix 的 ItSuitePosix 串调顺序保持一致）。放在本目录使
 * TEST_ICUNIT_POSIX 不再依赖 TEST_UNI_POSIX——两个目录各自持有入口，
 * 由 osTest.c 分别按目录开关调用。
 */

extern int PthreadFuncTestSuite(void);
extern void ItSuitePosixPthread(void);
extern void ItSuitePosixMutex(void);
extern void ItSuitePosixRwlock(void);
extern void ItSuitePosixMqueue(void);
extern void ItSuitePosixTime(void);
#if defined(LOSCFG_POSIX_PIPE_API)
extern void ItSuitePosixIo(void);
#endif
#if defined(LOSCFG_ARCH_ARM) && defined(LOSCFG_POSIX_SIGNAL_API)
extern void ItSuitePosixSignal(void);
#endif

void ItSuitePosixSample(void)
{
    PthreadFuncTestSuite();
    ItSuitePosixPthread();
    ItSuitePosixMutex();
    ItSuitePosixRwlock();
    ItSuitePosixMqueue();
    ItSuitePosixTime();
#if defined(LOSCFG_POSIX_PIPE_API)
    ItSuitePosixIo();
#endif
#if defined(LOSCFG_ARCH_ARM) && defined(LOSCFG_POSIX_SIGNAL_API)
    ItSuitePosixSignal();
#endif
}
