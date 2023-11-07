/*
 * Copyright (c) 2016-2017, Linaro Limited
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

#include <mm/mobj.h>
#include <stdlib.h>
#include <string.h>

struct mobj *mobj_reg_shm_get_by_cookie(uint64_t cookie)
{
	struct mobj *obj = zalloc(sizeof(struct mobj));
	obj->buffer = (void *)(uintptr_t)cookie;
	return obj;
}

TEE_Result mobj_reg_shm_release_by_cookie(uint64_t cookie)
{
	struct mobj *obj = (struct mobj *)(uintptr_t)cookie;
	if (obj) {
		free(obj);
		obj = NULL;
	}
	return TEE_SUCCESS;
}
