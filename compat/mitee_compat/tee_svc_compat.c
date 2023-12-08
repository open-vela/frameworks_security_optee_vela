/*
 * Copyright (c) 2016-2022, Linaro Limited
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

#include <string.h>
#include <tee/tee_svc_compat.h>

TEE_Result tee_svc_copy_kaddr_to_uref(uint32_t *uref, void *kaddr)
{
	uint32_t ref = tee_svc_kaddr_to_uref(kaddr);
	return tee_svc_copy_to_user(uref, &ref, sizeof(ref));
}

TEE_Result tee_svc_copy_to_user(void *uaddr, const void *kaddr, size_t len)
{
	memcpy(uaddr, kaddr, len);
	return TEE_SUCCESS;
}
