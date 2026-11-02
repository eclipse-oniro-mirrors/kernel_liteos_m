/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF ANY SUCH DAMAGE.
 */

/*
 * Relocation-type coverage asset, loaded as an ET_REL object via LOS_ObjLoad.
 * Hand-written asm emits the relocation families the plain C assets cannot:
 * PCREL_HI20/PCREL_LO12_I/PCREL_LO12_S pairs, LO12_S, JAL, BRANCH, ADD8/16/32,
 * SUB8/16/32 (via .reloc), plus a COMMON symbol (LD_ComSymAdd path).
 */

int g_rel_target = 0x1111;      /* symbol target for relocations */
volatile int g_rel_store = 0;   /* store target for PCREL_LO12_S (It_los_dynload_017) */

/* Kernel-side write targets (see It_los_dynload_dso.c): on W^X boards the module
 * window is RX while running, so store-family relocations write kernel-side
 * globals resolved through the OS symbol table instead of module-internal data. */
extern int g_dynload_krel_branch_hit;
extern int g_dynload_krel_jal_hit;
extern int g_dynload_krel_store;

#ifdef RELOC_WITH_COMMON
/* COMMON symbol (tentative definition): loader SHN_COMMON path (see It_los_dynload_016,
 * isolated by KERNEL_BUG_14_DYNLOAD_SHN_COMMON while the OOB index defect is open). */
int g_rel_common;
#endif

/* global (not static): the loader resolves relocation symbols from GLOBAL/WEAK
 * bindings only, so the jump/branch targets must be module-visible symbols. */
void jal_target(void)
{
    g_dynload_krel_jal_hit = 1;
}

/* R_RISCV_PCREL_HI20 + R_RISCV_PCREL_LO12_I: address materialization. */
int *pcrel_get_target(void)
{
    int *p;
    __asm__ volatile(
        "1: auipc %0, %%pcrel_hi(g_rel_target)\n"
        "   addi  %0, %0, %%pcrel_lo(1b)"
        : "=r"(p));
    return p;
}

/* R_RISCV_PCREL_HI20 + R_RISCV_PCREL_LO12_S: store through a pcrel address. */
void pcrel_store(int v)
{
    __asm__ volatile(
        "1: auipc t0, %%pcrel_hi(g_rel_store)\n"
        "   sw   %0, %%pcrel_lo(1b)(t0)"
        :
        : "r"(v)
        : "t0", "memory");
}

/* R_RISCV_HI20 (lui) + R_RISCV_LO12_S (sw): absolute store to a kernel-side
 * symbol resolved from the OS symbol table (W^X-safe write target). */
void lo12s_store(int v)
{
    __asm__ volatile(
        "   lui  t0, %%hi(g_dynload_krel_store)\n"
        "   sw   %0, %%lo(g_dynload_krel_store)(t0)"
        :
        : "r"(v)
        : "t0", "memory");
}

/* R_RISCV_JAL: direct jump to a defined function. */
void jal_call(void)
{
    __asm__ volatile("jal ra, jal_target" : : : "ra");
}

/* R_RISCV_BRANCH: forward reference to a later-defined function makes the
 * assembler emit exactly one BRANCH relocation (backward/self references get
 * resolved in place or duplicated by an extra .L label reloc). */
void branch_call(void)
{
    __asm__ volatile("beq x0, x0, branch_target");
}

void branch_target(void)
{
    g_dynload_krel_branch_hit = 1;
}

/*
 * ADD/SUB relocation slots: one global symbol per relocation type, anchored on
 * a standalone (unreachable) .text word. The C accessors below read them via
 * absolute addressing (HI20/LO12_I, unaffected by the PCREL_LO12 defect), so
 * the patched word equals the (width-truncated) symbol address of g_rel_target.
 */
#define RELOC_SLOT(name, rtype)                                        \
    __asm__(".text\n.align 2\n.globl " name "\n" name ":\n"            \
            ".reloc " name ", " rtype ", g_rel_target\n.word 0\n")

RELOC_SLOT("g_add8_slot", "R_RISCV_ADD8");
RELOC_SLOT("g_add16_slot", "R_RISCV_ADD16");
RELOC_SLOT("g_add32_slot", "R_RISCV_ADD32");
RELOC_SLOT("g_sub8_slot", "R_RISCV_SUB8");
RELOC_SLOT("g_sub16_slot", "R_RISCV_SUB16");
RELOC_SLOT("g_sub32_slot", "R_RISCV_SUB32");

extern unsigned char g_add8_slot;
extern unsigned short g_add16_slot;
extern unsigned int g_add32_slot;
extern unsigned char g_sub8_slot;
extern unsigned short g_sub16_slot;
extern unsigned int g_sub32_slot;

unsigned char add8_get(void)
{
    return g_add8_slot;
}

unsigned short add16_get(void)
{
    return g_add16_slot;
}

unsigned int add32_get(void)
{
    return g_add32_slot;
}

unsigned char sub8_get(void)
{
    return g_sub8_slot;
}

unsigned short sub16_get(void)
{
    return g_sub16_slot;
}

unsigned int sub32_get(void)
{
    return g_sub32_slot;
}

int *reloc_target_addr(void)
{
    return &g_rel_target;
}

/* Accessors so the test case can observe relocation effects. */
int get_rel_store(void)
{
    return g_rel_store;
}

#ifdef RELOC_WITH_COMMON
/* COMMON symbol accessor: proves the loader allocated .bss space for it. */
int common_inc(void)
{
    return ++g_rel_common;
}
#endif
