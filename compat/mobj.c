/*
 * Copyright (c) 2016-2022, Linaro Limited
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
#include <mm/vm.h>
#include <optee_msg.h>
#include <stdlib.h>
#include <tee_api_types.h>
#include <types_ext.h>
#include <util.h>

struct mobj *mobj_sec_ddr;
struct mobj *mobj_tee_ram_rx;
struct mobj *mobj_tee_ram_rw;

/*
 * mobj_mm implementation
 */
struct mobj_mm {
	tee_mm_entry_t *mm;
	struct mobj *parent_mobj;
	struct mobj mobj;
};

struct mobj mobj_virt = { .size = SIZE_MAX };

static struct mobj_mm *to_mobj_mm(struct mobj *mobj)
{
	return container_of(mobj, struct mobj_mm, mobj);
}

static void mobj_mm_free(struct mobj *mobj)
{
	struct mobj_mm *m = to_mobj_mm(mobj);

	free(m->mm);
	free(m);
}

struct mobj *mobj_mm_alloc(struct mobj *mobj_parent, size_t size,
			      tee_mm_pool_t *pool)
{
	struct mobj_mm *m = calloc(1, sizeof(*m));

	if (!m)
		return NULL;

	m->mm = malloc(size);
	if (!m->mm) {
		free(m);
		return NULL;
	}

	m->parent_mobj = mobj_parent;
	m->mobj.size = size;
	return &m->mobj;
}
