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

#include <stdlib.h>
#include <trace.h>
#include <kernel/user_ta.h>
#include <tee_api_defines.h>
#include <tee/tee_fs.h>
#include <tee/tee_pobj.h>
#include <tee/tee_obj.h>
#include <tee/tee_svc_cryp.h>

void tee_obj_add(struct user_ta_ctx *utc, struct tee_obj *o)
{
	TAILQ_INSERT_TAIL(&utc->objects, o, link);
}

TEE_Result tee_obj_get(struct user_ta_ctx *utc, vaddr_t obj_id,
		       struct tee_obj **obj)
{
	struct tee_obj *o;

	TAILQ_FOREACH(o, &utc->objects, link) {
		if (obj_id == (vaddr_t)o) {
			*obj = o;
			return TEE_SUCCESS;
		}
	}
	return TEE_ERROR_BAD_PARAMETERS;
}

void tee_obj_close(struct user_ta_ctx *utc, struct tee_obj *o)
{
	TAILQ_REMOVE(&utc->objects, o, link);

	if ((o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		o->pobj->fops->close(&o->fh);
		tee_pobj_release(o->pobj);
	}

	tee_obj_free(o);
}

void tee_obj_close_all(struct user_ta_ctx *utc)
{
	struct tee_obj_head *objects = &utc->objects;

	/* CID 69817, USE_AFTER_FREE. No problem */
	while (!TAILQ_EMPTY(objects))
		tee_obj_close(utc, TAILQ_FIRST(objects));
}

TEE_Result tee_obj_verify(struct tee_ta_session *sess __unused, struct tee_obj *o __unused)
{
	return TEE_SUCCESS;
}

struct tee_obj *tee_obj_alloc(void)
{
	return calloc(1, sizeof(struct tee_obj));
}

void tee_obj_free(struct tee_obj *o)
{
	if (o) {
		tee_obj_attr_free(o);
		free(o->attr);
		free(o);
	}
}
