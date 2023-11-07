/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2015-2020, 2022 Linaro Limited
 * Copyright (c) 2020-2023, Arm Limited
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

#include <ldelf.h>
#include <kernel/user_mode_ctx_struct.h>

TEE_Result ldelf_init_with_ldelf(struct ts_session *sess,
				 struct user_mode_ctx *uctx)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

TEE_Result ldelf_load_ldelf(struct user_mode_ctx *uctx)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

TEE_Result ldelf_dump_state(struct user_mode_ctx *uctx)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
