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

/* Relocatable object exercising MOVW/MOVT address materialization relocations. */

int g_movwt_val = 500;
static int g_movwt_arr[4] = { 5, 10, 15, 20 };

int movwt_read_data(void)
{
    return g_movwt_val; /* &g_movwt_val through MOVW/MOVT */
}

int movwt_read_array(int i)
{
    return g_movwt_arr[i]; /* array base through MOVW/MOVT */
}

/* noinline keeps the indirect call a real BL (R_ARM_THM_CALL) */
__attribute__((noinline)) int movwt_add(int a, int b)
{
    return a + b;
}

__attribute__((noinline)) int movwt_sub(int a, int b)
{
    return a - b;
}

/* function addresses materialized with MOVW/MOVT and called indirectly */
int movwt_indirect(int a, int b)
{
    int (*fn)(int, int) = (a & 1) ? movwt_add : movwt_sub;
    return fn(a, b);
}
