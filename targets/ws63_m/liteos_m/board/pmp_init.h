/*
 * Copyright (c) 2024 HiHope Open Source Organization.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Description: PMP initialization for OHOS SRAM — configures entry 5 to
 *              cover the OHOS-used SRAM range with NORMAL (WB+RALLOC)
 *              attribute, eliminating misaligned-access traps caused by
 *              SRAM defaulting to DEVICE when no PMP entry covers it.
 *
 * Design doc: 初始化pmp解析.md §11
 */

#ifndef _PMP_INIT_H_
#define _PMP_INIT_H_

#include <stdint.h>

/*
 * Configure PMP entry 5 to cover the OHOS-used SRAM range
 * [__sram_start__, __sram_end__) defined in board.ld:
 *   - mode  : TOR
 *   - upper : __sram_end__ >> 2  (lower bound auto-derived from pmpaddr4)
 *   - attr  : WB+RALLOC (NORMAL, 0x7)
 *   - perm  : RWX
 *   - lock  : true (matches SDK convention)
 *
 * Call site: earliest in main(), after dump_pmp_state().
 * Depends on: board.ld symbols __sram_start__ / __sram_end__
 */
void pmp_init(void);

#endif /* _PMP_INIT_H_ */
