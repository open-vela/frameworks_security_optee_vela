/*
 * Copyright (c) 2019-2020, Linaro Limited
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

#ifndef __KERNEL_STMM_SP_H
#define __KERNEL_STMM_SP_H

#include <kernel/user_mode_ctx_struct.h>

struct stmm_ctx {
	struct user_mode_ctx uctx;
	struct tee_ta_ctx ta_ctx;
	struct thread_ctx_regs regs;
	vaddr_t ns_comm_buf_addr;
	unsigned int ns_comm_buf_size;
	bool is_initializing;
};

extern const struct ts_ops stmm_sp_ops;

static inline bool is_stmm_ctx(struct ts_ctx *ctx __maybe_unused)
{
	return IS_ENABLED(CFG_WITH_STMM_SP) && ctx && ctx->ops == &stmm_sp_ops;
}

static inline struct stmm_ctx *to_stmm_ctx(struct ts_ctx *ctx)
{
	assert(is_stmm_ctx(ctx));
	return container_of(ctx, struct stmm_ctx, ta_ctx.ts_ctx);
}

#ifdef CFG_WITH_STMM_SP
TEE_Result stmm_init_session(const TEE_UUID *uuid,
			     struct tee_ta_session *s);
#else
static inline TEE_Result
stmm_init_session(const TEE_UUID *uuid __unused,
		  struct tee_ta_session *s __unused)
{
	return TEE_ERROR_ITEM_NOT_FOUND;
}
#endif

#endif /*__KERNEL_STMM_SP_H*/
