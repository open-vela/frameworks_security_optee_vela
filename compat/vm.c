/*
 * Copyright (c) 2016-2021, Linaro Limited
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2021, Arm Limited
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

#include <mm/vm.h>

TEE_Result vm_info_init(struct user_mode_ctx *uctx, struct ts_ctx *ts_ctx)
{
	return TEE_SUCCESS;
}

void vm_clean_param(struct user_mode_ctx *uctx)
{
}

void vm_info_final(struct user_mode_ctx *uctx)
{
}

TEE_Result vm_check_access_rights(const struct user_mode_ctx *uctx,
				  uint32_t flags, uaddr_t uaddr, size_t len)
{
	return TEE_SUCCESS;
}

TEE_Result vm_map_param(struct user_mode_ctx *uctx, struct tee_ta_param *param,
			void *param_va[TEE_NUM_PARAMS])
{
	return TEE_SUCCESS;
}

void vm_set_ctx(struct ts_ctx *ctx)
{
}

/* return true only if buffer fits inside TA private memory */
bool vm_buf_is_inside_um_private(const struct user_mode_ctx *uctx,
				 const void *va, size_t size)
{
	return false;
}

TEE_Result vm_buf_to_mboj_offs(const struct user_mode_ctx *uctx,
			       const void *va, size_t size,
			       struct mobj **mobj, size_t *offs)
{
	return TEE_SUCCESS;
}
