/*
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

#include <stdint.h>
#include <nuttx/spinlock.h>

/*
 * the following __atomic_load_8 is needed by optee_os copy_in_params
 * related operations, but the vela toolchain only provide
 * __atomic_load_1/2/4, do not provide __atomic_load_8, so we need
 * to implement the software version __atomic_load_8
 * the implementation is referred to nuttx/libs/libc/machine/arch_atomic.c
 */
uint64_t __atomic_load_8(FAR const volatile void *ptr, int memorder)
{
	irqstate_t irqstate = spin_lock_irqsave(NULL);

	uint64_t ret = *(FAR uint64_t *)ptr;

	spin_unlock_irqrestore(NULL, irqstate);
	return ret;
}
