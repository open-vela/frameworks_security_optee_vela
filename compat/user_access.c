/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2015-2020, 2022 Linaro Limited
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

#include <kernel/user_access.h>
#include <mm/vm.h>
#include <string.h>
#include <tee_api_types.h>
#include <types_ext.h>

TEE_Result copy_from_user(void *kaddr, const void *uaddr, size_t len)
{
	memcpy(kaddr, uaddr, len);
	return TEE_SUCCESS;
}

TEE_Result copy_to_user(void *uaddr, const void *kaddr, size_t len)
{
	memcpy(uaddr, kaddr, len);
	return TEE_SUCCESS;
}

TEE_Result copy_from_user_private(void *kaddr, const void *uaddr, size_t len)
{
	memcpy(kaddr, uaddr, len);
	return TEE_SUCCESS;
}

TEE_Result copy_to_user_private(void *uaddr, const void *kaddr, size_t len)
{
	memcpy(uaddr, kaddr, len);
	return TEE_SUCCESS;
}

TEE_Result copy_kaddr_to_uref(uint32_t *uref, void *kaddr)
{
	uint32_t ref = kaddr_to_uref(kaddr);

	return copy_to_user_private(uref, &ref, sizeof(ref));
}

uint32_t kaddr_to_uref(void *kaddr)
{
	return (vaddr_t)kaddr;
}

vaddr_t uref_to_vaddr(uint32_t uref)
{
	return uref;
}
