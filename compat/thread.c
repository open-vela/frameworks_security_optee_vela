/*
 * Copyright (c) 2016-2022, Linaro Limited
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2020-2021, Arm Limited
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

#include <string.h>
#include <sys/types.h>
#include <kernel/thread.h>
#include <nuttx/irq.h>
#include <util.h>
#include <mm/mobj.h>
#include <kernel/thread_private.h>

static struct thread_specific_data tsd;
/* the "struct thread_shm_cache" is a single linked list
 * consists of thread_shm_cache_entry
 */
static struct thread_shm_cache shm_cache;

uint32_t thread_enter_user_mode(unsigned long a0, unsigned long a1,
		unsigned long a2, unsigned long a3, unsigned long user_sp,
		unsigned long entry_func, bool is_32bit,
		uint32_t *exit_status0, uint32_t *exit_status1)
{
	return TEE_SUCCESS;
}

uint32_t thread_get_exceptions(void)
{
	return THREAD_EXCP_FOREIGN_INTR;
}

uint32_t thread_mask_exceptions(uint32_t exceptions)
{
	return up_irq_save();
}

void thread_unmask_exceptions(uint32_t state)
{
	up_irq_restore(state);
}

short int thread_get_id(void)
{
	return gettid();
}

short int thread_get_id_may_fail(void)
{
	return gettid();
}

bool thread_is_in_normal_mode(void)
{
	return true;
}

struct thread_specific_data *thread_get_tsd(void)
{
	return &tsd;
}

void thread_set_foreign_intr(bool enable)
{
}

static void free_payload(struct mobj *mobj)
{
	if (mobj && mobj->buffer) {
		free(mobj->buffer);
		mobj->buffer = NULL;
	}
	mobj_put(mobj);
}

static void clear_shm_cache_entry(struct thread_shm_cache_entry *ce)
{
	if (ce->mobj) {
		switch (ce->type) {
		case THREAD_SHM_TYPE_APPLICATION:
		case THREAD_SHM_TYPE_KERNEL_PRIVATE:
		case THREAD_SHM_TYPE_GLOBAL:
			free_payload(ce->mobj);
			break;
		default:
			assert(0); /* "can't happen" */
			break;
		}
	}
	ce->mobj = NULL;
	ce->size = 0;
}

static struct thread_shm_cache_entry *
get_shm_cache_entry(enum thread_shm_cache_user user)
{
	struct thread_shm_cache *cache = &shm_cache;
	struct thread_shm_cache_entry *ce = NULL;

	SLIST_FOREACH(ce, cache, link)
		if (ce->user == user)
			return ce;

	ce = calloc(1, sizeof(*ce));
	if (ce) {
		ce->user = user;
		SLIST_INSERT_HEAD(cache, ce, link);
	}

	return ce;
}

static struct mobj *alloc_shm(enum thread_shm_type shm_type, size_t size)
{
	struct mobj *allocated_mobj = NULL;
	switch (shm_type) {
	case THREAD_SHM_TYPE_APPLICATION:
	case THREAD_SHM_TYPE_KERNEL_PRIVATE:
	case THREAD_SHM_TYPE_GLOBAL:
		allocated_mobj = malloc(sizeof(struct mobj));
		if (!allocated_mobj) {
			return NULL;
		}
		memset(allocated_mobj, 0, sizeof(struct mobj));
		allocated_mobj->size = size;
		allocated_mobj->buffer = malloc(size);
		if (!allocated_mobj->buffer) {
			free(allocated_mobj);
			return NULL;
		}
		memset(allocated_mobj->buffer, 0, size);
		return allocated_mobj;
	default:
		return NULL;
	}
}

void *thread_rpc_shm_cache_alloc(enum thread_shm_cache_user user,
				 enum thread_shm_type shm_type,
				 size_t size, struct mobj **mobj)
{
	struct thread_shm_cache_entry *ce = NULL;
	size_t sz = size;
	void *va = NULL;

	if (!size)
		return NULL;

	ce = get_shm_cache_entry(user);
	if (!ce)
		return NULL;

	/*
	 * Always allocate in page chunks as normal world allocates payload
	 * memory as complete pages.
	 */
	sz = ROUNDUP(size, SMALL_PAGE_SIZE);

	if (ce->type != shm_type || sz > ce->size) {
		clear_shm_cache_entry(ce);

		ce->mobj = alloc_shm(shm_type, sz);
		if (!ce->mobj)
			return NULL;

		va = mobj_get_va(ce->mobj, 0, sz);
		if (!va)
			goto err;

		ce->size = sz;
		ce->type = shm_type;
	} else {
		va = mobj_get_va(ce->mobj, 0, sz);
		if (!va)
			goto err;
	}
	*mobj = ce->mobj;

	return va;
err:
	clear_shm_cache_entry(ce);
	return NULL;
}

void thread_rpc_shm_cache_clear(struct thread_shm_cache *cache)
{
	while (true) {
		struct thread_shm_cache_entry *ce = SLIST_FIRST(cache);

		if (!ce)
			break;
		SLIST_REMOVE_HEAD(cache, link);
		clear_shm_cache_entry(ce);
		free(ce);
	}
}
