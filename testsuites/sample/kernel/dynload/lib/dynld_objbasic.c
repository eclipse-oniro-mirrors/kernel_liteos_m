/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Relocatable object exercising literal-pool relocations (THM_CALL, THM_JUMP24, ABS32). */

int g_obj_val = 200;
static int g_obj_local = 3;
static const int g_obj_ro = 40;

/* noinline + external linkage keep the BL/B.W relocations below real. */
__attribute__((noinline)) int obj_helper(int a, int b)
{
    return a + b + g_obj_local; /* text -> .data via literal pool (ABS32) */
}

int obj_get_ro(void)
{
    return g_obj_ro; /* text -> .rodata via literal pool (ABS32) */
}

/* -O2 turns this call into a tail call: B.W, R_ARM_THM_JUMP24 */
int obj_tail_call(int a, int b)
{
    return obj_helper(a, b);
}

/* plain call: BL, R_ARM_THM_CALL */
int obj_add(int a, int b)
{
    return obj_helper(a, b) * 2;
}

/* const tables in .rodata holding code and data addresses: R_ARM_ABS32 */
int (*const g_obj_fn)(int, int) = obj_add;
int *const g_obj_ptr = &g_obj_val;

/* runtime-selected target keeps the call indirect via the ABS32 pointers */
int obj_call_by_table(int a, int b)
{
    int (*fn)(int, int) = (a & 1) ? obj_add : obj_helper;
    return fn(a, b) + *g_obj_ptr;
}
