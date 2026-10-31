/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
 * Description: AARCH32 Dynamic Load HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2022-02-25
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */
/* ----------------------------------------------------------------------------
 * Notice of Export Control Law
 * ===============================================
 * Huawei LiteOS may be subject to applicable export control laws and regulations, which might
 * include those applicable to Huawei LiteOS of U.S. and the country in which you are located.
 * Import, export and usage of Huawei LiteOS in any manner by you shall be in compliance with such
 * applicable export control laws and regulations.
 * --------------------------------------------------------------------------- */

#ifndef _ARCH_DYNLOAD_H
#define _ARCH_DYNLOAD_H

#include "unistd.h"
#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DYNLOAD_ALIGN_SIZE 64
#define UINTMAX            UINT32_MAX

/* Symbol table */
typedef struct {
    UINT32 stName;  /* Symbol table name (string tbl index) */
    UINT32 stValue; /* Symbol table value */
    UINT32 stSize;  /* Symbol table size */
    UINT8 stInfo;   /* Symbol table type and binding */
    UINT8 stOther;  /* Symbol table visibility */
    UINT16 stShndx; /* Section table index */
} LDElf32Sym;

#define LD_ELF_SYM             LDElf32Sym

/* Section header */
typedef struct {
    UINT32 shName;      /* Section name (string tbl index) */
    UINT32 shType;      /* Section type */
    UINT32 shFlags;     /* Section flags */
    UINT32 shAddr;      /* Section virtual addr at execution */
    UINT32 shOffset;    /* Section file offset */
    UINT32 shSize;      /* Section size in bytes */
    UINT32 shLink;      /* Link to another section */
    UINT32 shInfo;      /* Additional section information */
    UINT32 shAddrAlign; /* Section alignment */
    UINT32 shEntSize;   /* Entry size if section holds table */
} LDElf32Shdr;

#define LD_ELF_SHDR            LDElf32Shdr

/* Program Header */
typedef struct {
    UINT32 type;     /* Segment type */
    UINT32 offset;   /* Segment file offset */
    UINT32 vAddr;    /* Segment virtual address */
    UINT32 phyAddr;  /* Segment physical address */
    UINT32 fileSize; /* Segment size in file */
    UINT32 memSize;  /* Segment size in memory */
    UINT32 flags;    /* Segment flags */
    UINT32 align;    /* Segment alignment */
} LDElf32Phdr;

#define LD_ELF_PHDR            LDElf32Phdr

/* Elf header */
#define LD_EI_NIDENT           16
typedef struct {
    UINT8       elfIdent[LD_EI_NIDENT]; /* Magic number and other info */
    UINT16      elfType;                /* Object file type */
    UINT16      elfMachine;             /* Architecture */
    UINT32      elfVersion;             /* Object file version */
    UINT32      elfEntry;               /* Entry point virtual address */
    UINT32      elfPhoff;               /* Program header table file offset */
    UINT32      elfShoff;               /* Section header table file offset */
    UINT32      elfFlags;               /* Processor-specific flags */
    UINT16      elfHeadSize;            /* ELF header size in bytes */
    UINT16      elfPhSize;              /* Program header table entry size */
    UINT16      elfPhNum;               /* Program header table entry count */
    UINT16      elfShEntSize;           /* Section header table entry size */
    UINT16      elfShNum;               /* Section header table entry count */
    UINT16      elfShStrIndex;          /* Section header string table index */
} LDElf32Ehdr;

#define LD_ELF_EHDR            LDElf32Ehdr

/* EI_CLASS */
#define LD_ELF_CLASS32         1
#define LD_ELF_CLASS LD_ELF_CLASS32

/* e_machine */
#define LD_EM_ARM              40    /* ARM  */
#define LD_EM_TYPE  LD_EM_ARM

/* Dynamic */
typedef struct {
    UINT32 dynTag;  /* Dynamic entry type */
    union {
        UINT32 val; /* Integer value */
        UINT32 ptr; /* Address value */
    } dyn;
} LDElf32Dyn;

#define LD_ELF_DYN             LDElf32Dyn

/* Relocation */
typedef struct {
    UINT32 offset; /* Address */
    UINT32 info;   /* Relocation type and symbol index */
} LDElf32Rel;

typedef struct {
    UINT32 offset; /* Address */
    UINT32 info;   /* Relocation type and symbol index */
    INT32 addend;  /* Addend */
} LDElf32Rela;

#define LD_ELF_REL             LDElf32Rel
#define LD_ELF_RELA            LDElf32Rela

#define LD_ELF_R_SYM(info) ((info) >> 8)
#define LD_ELF_R_TYPE(info) ((info) & 0xFFU)
#define LD_ELF_R_INFO(sym, type) (((sym) << 8) + (UINT8)(type))

/* Relocation Type Definitions */
#define OS_R_ARM_NONE                       0 /* No reloc */
#define OS_R_ARM_PC24                       1
#define OS_R_ARM_ABS32                      2
#define OS_R_ARM_REL32                      3
#define OS_R_ARM_LDR_PC_G0                  4
#define OS_R_ARM_ABS16                      5
#define OS_R_ARM_ABS12                      6
#define OS_R_ARM_THM_ABS5                   7
#define OS_R_ARM_ABS8                       8
#define OS_R_ARM_SBREL32                    9
#define OS_R_ARM_THM_CALL                   10
#define OS_R_ARM_THM_PC8                    11
#define OS_R_ARM_BREL_ADJ                   12
#define OS_R_ARM_TLS_DESC                   13
#define OS_R_ARM_THM_SWI8                   14
#define OS_R_ARM_XPC25                      15
#define OS_R_ARM_THM_XPC22                  16
#define OS_R_ARM_COPY                       20 /* Copy symbol at runtime */
#define OS_R_ARM_GLOB_DAT                   21 /* Create GOT entry */
#define OS_R_ARM_JUMP_SLOT                  22 /* Create PLT entry */
#define OS_R_ARM_RELATIVE                   23 /* Adjust by program base */
#define OS_R_ARM_GOTOFF32                   24 /* 32 bit offset to GOT */
#define OS_R_ARM_BASE_PREL                  25 /* 32 bit PC relative offset to GOT */
#define OS_R_ARM_GOT_BREL                   26 /* 32 bit GOT entry */
#define OS_R_ARM_PLT32                      27 /* 32 bit PLT address */
#define OS_R_ARM_CALL                       28
#define OS_R_ARM_THM_JUMP24                 30
#define OS_R_ARM_MOVW_ABS_NC                43
#define OS_R_ARM_MOVT_ABS                   44
#define OS_ARM_THM_MOVW_ABS_NC              47
#define OS_ARM_THM_MOVT_ABS                 48

/* These are GNU extensions to enable C++ vtable garbage collection. */
#define OS_R_ARM_GNU_VTENTRY                100
#define OS_R_ARM_GNU_VTINHERIT              101
#define OS_R_ARM_THM_JUMP11                 102 /* Thumb unconditional branch */
#define OS_R_ARM_THM_JUMP8                  103 /* Thumb conditional branch */
#define OS_R_ARM_RXPC25                     249
#define OS_R_ARM_RSBREL32                   250
#define OS_R_ARM_THM_RPC22                  251
#define OS_R_ARM_RREL32                     252
#define OS_R_ARM_RABS32                     253
#define OS_R_ARM_RPC24                      254
#define OS_R_ARM_RBASE                      255

typedef struct {
    UINT32 relocType;
    UINT32 shType;        /* REL section or RELA section. */
    UINTPTR position;     /* the address of the place being relocated. */
    UINTPTR symAdd;       /* the address of the symbol. */
    ssize_t addend;       /* the addend for the relocation. */
    UINTPTR baseSegment;  /* the addressing origin of the output segment defining the symbol S. */
    UINTPTR gotAddr;      /* the address of the Global Offset Table. */
    /*
     * T is 1 if the target symbol S has type STT_FUNC and
     * the symbol addresses a Thumb instruction; it is 0 otherwise.
     */
    UINT8 thumbFunc;
#define RESERVE_NUM 7
    UINT8 reserved[RESERVE_NUM];
} LDRelocParam;

typedef struct {
    UINTPTR relTable;     /* address of relocation table. */
    UINTPTR relTableSize; /* total size of relocation table. */
    UINTPTR relEntrySize; /* size of every relocation entry. */
} RelocationInfo;

extern UINT32 ELF_ArmPc24Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmAbs32Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmRel32Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ThumbPc22Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmCallReloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ThumbPc9Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ThumbPc11Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmAbs8Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmAbs16Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmAbs12Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ThumbAbs5Reloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ThumbMovwOrMovtReloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmMovwOrMovtReloc(const LDRelocParam *relocParam);
extern UINT32 ELF_GlobDatReloc(const LDRelocParam *relocParam);
extern UINT32 ELF_JumpSlotReloc(const LDRelocParam *relocParam);
extern UINT32 ELF_ArmRelativeReloc(const LDRelocParam *relocParam);
extern UINT32 ArchSegRelocImp(const LD_ELF_RELA *rela, const LDRelocParam *relocParam);
extern UINT32 ArchObjRelocImp(const LD_ELF_RELA *relocCmd, const LDRelocParam *relocParam);
extern UINT32 ArchSegRelocTblCheck(const RelocationInfo *relocInfo);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_DYNLOAD_H */
