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

#ifndef TEE_POBJ_H
#define TEE_POBJ_H

#include <stdint.h>
#include <sys/queue.h>
#include <tee_api_types.h>
#include <tee/tee_fs.h>

struct tee_pobj {
	TAILQ_ENTRY(tee_pobj) link;
	uint32_t refcnt;
	TEE_UUID uuid;
	void *obj_id;
	uint32_t obj_id_len;
	uint32_t flags;
	uint32_t storage_id;
	bool temporary;
	bool creating;	/* can only be changed with mutex held */
	/* Filesystem handling this object */
	const struct tee_file_operations *fops;
};

enum tee_pobj_usage {
	TEE_POBJ_USAGE_OPEN,
	TEE_POBJ_USAGE_RENAME,
	TEE_POBJ_USAGE_CREATE,
	TEE_POBJ_USAGE_ENUM,
};

TEE_Result tee_pobj_get(TEE_UUID *uuid, void *obj_id, uint32_t obj_id_len,
			uint32_t flags, enum tee_pobj_usage usage,
			const struct tee_file_operations *fops,
			struct tee_pobj **obj);

TEE_Result tee_pobj_release(struct tee_pobj *obj);

TEE_Result tee_pobj_rename(struct tee_pobj *obj, void *obj_id,
			   uint32_t obj_id_len);

#endif
