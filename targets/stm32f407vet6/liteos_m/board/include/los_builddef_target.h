#ifndef _LOS_BUILDDEF_TARGET_H
#define _LOS_BUILDDEF_TARGET_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef LITE_OS_SEC_ALW_INLINE
#define LITE_OS_SEC_ALW_INLINE                  __attribute__((always_inline))
#endif

#ifndef LITE_OS_SEC_TEXT                    
#define LITE_OS_SEC_TEXT                        __attribute__((section(".kernel.text")))
#endif

#ifndef LITE_OS_SEC_TEXT_MINOR
#define LITE_OS_SEC_TEXT_MINOR                  __attribute__((section(".kernel.text")))
#endif
 
#ifndef LITE_OS_SEC_TEXT_INIT
#define LITE_OS_SEC_TEXT_INIT                   __attribute__((section(".kernel.text")))
#endif

#ifndef LITE_OS_SEC_KEEP_TEXT_ITCM
#define LITE_OS_SEC_KEEP_TEXT_ITCM              __attribute__((section(".itcm.kernel.text")))
#endif

#ifndef LITE_OS_SEC_DATA                    
#define LITE_OS_SEC_DATA                        __attribute__((section(".kernel.data")))
#endif

#ifndef LITE_OS_SEC_DATA_INIT
#define LITE_OS_SEC_DATA_INIT                   __attribute__((section(".kernel.data")))
#endif

#ifndef LITE_OS_SEC_BSS
#define LITE_OS_SEC_BSS                         __attribute__((section(".kernel.bss"))) 
#endif

#ifndef LITE_OS_SEC_BSS_MINOR
#define LITE_OS_SEC_BSS_MINOR                   __attribute__((section(".kernel.bss")))
#endif

#ifndef LITE_OS_SEC_BSS_INIT
#define LITE_OS_SEC_BSS_INIT                    __attribute__((section(".kernel.bss")))
#endif

#ifndef LITE_OS_SEC_TEXT_USRSPACE
#define LITE_OS_SEC_TEXT_USRSPACE               __attribute__((section(".usr.text")))
#endif

#ifndef LITE_OS_SEC_TEXT_USRROM
#define LITE_OS_SEC_TEXT_USRROM                 __attribute__((section(".usrtcm.text")))
#endif

#ifndef LITE_OS_SEC_RODATA_USRSPACE
#define LITE_OS_SEC_RODATA_USRSPACE             __attribute__((section(".usr.ro.data")))
#endif

#ifndef LITE_OS_SEC_WEAK
#define LITE_OS_SEC_WEAK                        __attribute__((weak))
#endif

#ifndef LITE_OS_SEC_BOX_RESSPACE
#define LITE_OS_SEC_BOX_RESSPACE                __attribute__((section(".box.res")))
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* */