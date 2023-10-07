/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2020, Linaro Limited
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

#include <kernel/panic.h>
#include <kernel/tee_ta_manager.h>
#include <kernel/thread.h>
#include <kernel/ts_manager.h>
#include <kernel/user_mode_ctx.h>

static TAILQ_HEAD(, ts_session) sess_stack;

void ts_push_current_session(struct ts_session *s)
{
	TAILQ_INSERT_HEAD(&sess_stack, s, link_tsd);
}

struct ts_session *ts_pop_current_session(void)
{
	struct ts_session *s = TAILQ_FIRST(&sess_stack);

	if (s) {
		TAILQ_REMOVE(&sess_stack, s, link_tsd);
	}
	return s;
}

struct ts_session *ts_get_calling_session(void)
{
	return TAILQ_NEXT(ts_get_current_session(), link_tsd);
}

struct ts_session *ts_get_current_session_may_fail(void)
{
	return TAILQ_FIRST(&sess_stack);
}

struct ts_session *ts_get_current_session(void)
{
	struct ts_session *s = ts_get_current_session_may_fail();

	if (!s)
		panic();
	return s;
}
