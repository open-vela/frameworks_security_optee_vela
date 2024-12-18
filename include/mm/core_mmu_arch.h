/*
 * Copyright (c) 2016, Linaro Limited
 * Copyright (C) 2020-2023 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __CORE_MMU_ARCH_H
#define __CORE_MMU_ARCH_H

#include <types_ext.h>

#define SMALL_PAGE_SHIFT	U(12)
struct core_mmu_user_map {
};



static inline bool core_mmu_check_max_pa(paddr_t pa __maybe_unused)
{
    return true;
}

typedef struct core_mmu_config core_mmu_config;

#endif /* __CORE_MMU_ARCH_H */
