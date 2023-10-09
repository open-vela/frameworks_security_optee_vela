/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2016-2022, Linaro Limited
 * Copyright (c) 2020-2021, Arm Limited
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

#ifndef __KERNEL_THREAD_ARCH_H
#define __KERNEL_THREAD_ARCH_H

#include <config.h>

#define THREAD_EXCP_FOREIGN_INTR	U(0x00000002)
#define THREAD_EXCP_NATIVE_INTR		U(0x00000001)
#define THREAD_EXCP_ALL			U(0x00000007)

struct thread_core_local {
};

struct thread_abort_regs {
};

struct thread_ctx_regs {
};

#endif /*__KERNEL_THREAD_ARCH_H*/
