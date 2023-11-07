/*
 * Copyright (c) 2016-2017, 2022 Linaro Limited
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

#ifndef __MM_MOBJ_H
#define __MM_MOBJ_H

#include <stdlib.h>
#include <tee_api_types.h>
#include <optee_msg.h>
#include <string_ext.h>
#include <mm/tee_mm.h>

struct mobj {
	size_t size;
	void *buffer;
};

/**
 * mobj_inc_map() - increase map count
 * @mobj:	pointer to a MOBJ
 *
 * Maps the MOBJ if it isn't mapped already and increases the map count
 * Each call to mobj_inc_map() is supposed to be matches by a call to
 * mobj_dec_map().
 *
 * Returns TEE_SUCCESS on success or an error code on failure
 */
static inline TEE_Result mobj_inc_map(struct mobj *mobj)
{
	if (mobj) {
		return TEE_SUCCESS;
	}
	return TEE_ERROR_GENERIC;
}

/**
 * mobj_dec_map() - decrease map count
 * @mobj:	pointer to a MOBJ
 *
 * Decreases the map count and also unmaps the MOBJ if the map count
 * reaches 0.  Each call to mobj_inc_map() is supposed to be matched by a
 * call to mobj_dec_map().
 *
 * Returns TEE_SUCCESS on success or an error code on failure
 */
static inline TEE_Result mobj_dec_map(struct mobj *mobj)
{
	if (mobj) {
		return TEE_SUCCESS;
	}
	return TEE_ERROR_GENERIC;
}

static inline struct mobj *mobj_get(struct mobj *mobj)
{
	return mobj;
}

/*
 * mobj_get_va() - get virtual address of a mapped mobj
 * @mobj:	memory object
 * @offset:	find the va of this offset into @mobj
 * @len:	how many bytes after @offset that must be valid, can be 1 if
 *		the caller knows by other means that the expected buffer is
 *		available.
 *
 * return a virtual address on success or NULL on error
 */
static inline void *mobj_get_va(struct mobj *mobj, size_t offset, size_t len)
{
	if (mobj)
		return mobj->buffer;
	return NULL;
}

static inline TEE_Result mobj_get_pa(struct mobj *mobj, size_t offs,
				     size_t granule, paddr_t *pa)
{
	return TEE_ERROR_GENERIC;
}

/**
 * mobj_put() - put a MOBJ
 * @mobj:	Pointer to a MOBJ or NULL
 *
 * Decreases reference counter of the @mobj and frees it if the counter
 * reaches 0.
 */
static inline void mobj_put(struct mobj *mobj)
{
	if (mobj) {
		free(mobj);
		mobj = NULL;
	}
}

/**
 * mobj_put_wipe() - wipe and put a MOBJ
 * @mobj:	Pointer to a MOBJ or NULL
 *
 * Clears the memory represented by the mobj and then puts it.
 */
static inline void mobj_put_wipe(struct mobj *mobj)
{
	if (mobj) {
		void *buf = mobj_get_va(mobj, 0, mobj->size);

		if (buf)
			memzero_explicit(buf, mobj->size);
		mobj_put(mobj);
	}
}

extern struct mobj mobj_virt;
extern struct mobj *mobj_sec_ddr;
extern struct mobj *mobj_tee_ram_rx;
extern struct mobj *mobj_tee_ram_rw;

struct mobj *mobj_reg_shm_get_by_cookie(uint64_t cookie);

void mobj_reg_shm_unguard(struct mobj *mobj);

TEE_Result mobj_reg_shm_release_by_cookie(uint64_t cookie);

struct mobj *mobj_mm_alloc(struct mobj *mobj_parent, size_t size,
			   tee_mm_pool_t *pool);

#endif /*__MM_MOBJ_H*/
