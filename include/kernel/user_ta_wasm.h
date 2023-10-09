/*
 * Copyright (c) 2015, Linaro Limited
 * Copyright (c) 2020, Arm Limited
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
#ifndef KERNEL_USER_TA_WASM_H
#define KERNEL_USER_TA_WASM_H

#include <kernel/user_ta.h>
#include "wasm_export.h"

/*
 * struct user_ta_ctx - user TA context
 * @open_sessions:	List of sessions opened by this TA
 * @cryp_states:	List of cryp states created by this TA
 * @objects:		List of storage objects opened by this TA
 * @storage_enums:	List of storage enumerators opened by this TA
 * @ta_time_offs:	Time reference used by the TA
 * @uctx:		Generic user mode context
 * @ctx:		Generic TA context
 */
struct user_ta_wasm_ctx {
	struct tee_ta_session_head open_sessions;
	struct tee_cryp_state_head cryp_states;
	struct tee_obj_head objects;
	struct tee_storage_enum_head storage_enums;
	void *ta_time_offs;
	struct user_mode_ctx uctx;
	struct tee_ta_ctx ta_ctx;
	/* the following fileds are for wasm ta implementation */
	wasm_module_t wasm_module;
	wasm_module_inst_t wasm_module_inst;
	wasm_function_inst_t func;
	wasm_exec_env_t exec_env;
	uint8_t *wasm_file_buffer;
	uint32_t wasm_file_size;
	uint32_t stack_size;
	bool is_xip_file;
};

static inline struct user_ta_wasm_ctx *to_user_ta_wasm_ctx(struct ts_ctx *ctx)
{
	assert(is_user_ta_ctx(ctx));
	return container_of(ctx, struct user_ta_wasm_ctx, ta_ctx.ts_ctx);
}

#endif /* KERNEL_USER_TA_WASM_H */
