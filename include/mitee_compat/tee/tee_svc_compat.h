/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
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

#ifndef TEE_SVC_COMPAT_H
#define TEE_SVC_COMPAT_H

#include <tee/tee_svc.h>

extern vaddr_t tee_svc_uref_base;

TEE_Result tee_svc_copy_kaddr_to_uref(uint32_t *uref, void *kaddr);
TEE_Result tee_svc_copy_to_user(void *uaddr, const void *kaddr, size_t len);

static inline uint32_t tee_svc_kaddr_to_uref(void *kaddr)
{
	return (vaddr_t)kaddr - tee_svc_uref_base;
}

static inline vaddr_t tee_svc_uref_to_vaddr(uint32_t uref)
{
	return tee_svc_uref_base + uref;
}

#endif /* TEE_SVC_COMPAT_H */
