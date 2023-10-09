/*
 * Copyright (c) 2020-2023, Arm Limited.
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

#ifndef __KERNEL_SECURE_PARTITION_H
#define __KERNEL_SECURE_PARTITION_H

#include <config.h>
#include <kernel/user_mode_ctx_struct.h>

struct sp_ctx {
	struct thread_ctx_regs sp_regs;
	struct sp_session *open_session;
	struct user_mode_ctx uctx;
	struct ts_ctx ts_ctx;
};

#ifdef CFG_SECURE_PARTITION
bool is_sp_ctx(struct ts_ctx *ctx);
#else
static inline bool is_sp_ctx(struct ts_ctx *ctx __unused)
{
	return false;
}
#endif

static inline struct sp_ctx *to_sp_ctx(struct ts_ctx *ctx)
{
	assert(is_sp_ctx(ctx));
	return container_of(ctx, struct sp_ctx, ts_ctx);
}

#endif /* __KERNEL_SECURE_PARTITION_H */
