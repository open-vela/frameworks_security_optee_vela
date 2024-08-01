/*
 * Copyright (C) 2020-2022 Xiaomi Corporation
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

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <tee_api_types.h>
#include <tee_api.h>
#include <tee_api_defines.h>
#include <tee_internal_api.h>
#include <trace.h>

#include "wasm_export.h"

void
wasm_runtime_set_exception(wasm_module_inst_t module, const char *exception);

uint32_t
wasm_runtime_get_temp_ret(wasm_module_inst_t module);

void
wasm_runtime_set_temp_ret(wasm_module_inst_t module, uint32_t temp_ret);

uint32_t
wasm_runtime_get_llvm_stack(wasm_module_inst_t module);

void
wasm_runtime_set_llvm_stack(wasm_module_inst_t module, uint32_t llvm_stack);

uint64_t
wasm_runtime_module_realloc(wasm_module_inst_t module, uint64_t ptr, uint64_t size,
				void **p_native_addr);

#define get_module_inst(exec_env) \
	wasm_runtime_get_module_inst(exec_env)

#define validate_app_addr(offset, size) \
	wasm_runtime_validate_app_addr(module_inst, offset, size)

#define validate_app_str_addr(offset) \
	wasm_runtime_validate_app_str_addr(module_inst, offset)

#define validate_native_addr(addr, size) \
	wasm_runtime_validate_native_addr(module_inst, addr, size)

#define addr_app_to_native(offset) \
	wasm_runtime_addr_app_to_native(module_inst, offset)

#define addr_native_to_app(ptr) \
	wasm_runtime_addr_native_to_app(module_inst, ptr)

#define module_malloc(size, p_native_addr) \
	wasm_runtime_module_malloc(module_inst, size, p_native_addr)

#define module_free(offset) \
	wasm_runtime_module_free(module_inst, offset)

typedef int (*out_func_t)(int c, void *ctx);

enum pad_type {
	PAD_NONE,
	PAD_ZERO_BEFORE,
	PAD_SPACE_BEFORE,
	PAD_SPACE_AFTER,
};

typedef char *_va_list;
#define _INTSIZEOF(n)		\
	(((uint32_t)sizeof(n) +  3) & (uint32_t)~3)
#define _va_arg(ap, t)		\
	(*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))

#define CHECK_VA_ARG(ap, t) do {						\
	if ((uint8_t*)ap + _INTSIZEOF(t) > native_end_addr)	\
		goto fail;										\
} while (0)

/* 4.11.4 */
static uint32_t
TEE_Malloc_wrapper(wasm_exec_env_t exec_env,
	uint32_t size, uint32_t hint)
{
	DMSG("wasm.libtee.%s: size: %ld, hint: 0x%08lx\n", __func__, size, hint);
	uint32_t ret_offset = 0;
	uint8_t *ret_ptr;

	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (size >= UINT32_MAX) {
		return 0;
	}

	ret_offset = module_malloc(size, (void **)&ret_ptr);
	if ((hint == TEE_MALLOC_FILL_ZERO) && (ret_offset)) {
		memset(ret_ptr, 0, size);
	}

	DMSG("wasm.libtee.%s: app_ptr: 0x%08lx, native_ptr: 0x%08lx\n", __func__, ret_offset, (uint32_t)ret_ptr);
	return ret_offset;
}

/* 4.11.5 */
static uint32_t
TEE_Realloc_wrapper(wasm_exec_env_t exec_env,
	uint32_t buffer, uint32_t newSize)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);
	return wasm_runtime_module_realloc(module_inst, buffer, newSize, NULL);
}

/* 4.11.6 */
static void
TEE_Free_wrapper(wasm_exec_env_t exec_env,
	void *buffer)
{
	DMSG("wasm.libtee.%s: buffer: 0x%08lx\n", __func__, (uint32_t)buffer);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);
	if (!validate_native_addr(buffer, sizeof(uint32_t))) {
		return;
	}
	module_free(addr_native_to_app(buffer));
}

/* 4.11.7 */
static void
TEE_MemMove_wrapper(wasm_exec_env_t exec_env,
	void *dst, const void *src, uint32_t size)
{
	DMSG("wasm.libtee.%s: size=%ld\n", __func__, size);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (size == 0)
		return;

	/* dst has been checked by runtime */
	if (!validate_native_addr(dst, size))
		return;

	/* src has been checked by runtime */
	if (!validate_native_addr((void *)src, size))
		return;

	TEE_MemMove(dst, src, size);
}

/* 4.11.8 */
static int32_t
TEE_MemCompare_wrapper(wasm_exec_env_t exec_env,
	const void *s1, const void *s2, uint32_t size)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* s1 has been checked by runtime */
	if (!validate_native_addr((void*)s1, size))
		return 0;

	/* s2 has been checked by runtime */
	if (!validate_native_addr((void*)s2, size))
		return 0;

	return TEE_MemCompare(s1, s2, size);
}

/* 4.11.9 */
static void
TEE_MemFill_wrapper(wasm_exec_env_t exec_env,
	void *buffer, uint8_t x, size_t size)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (!validate_native_addr(buffer, size))
		return;
	memset(buffer, x, size);
}

static void
object_info_native2app(TEE_ObjectInfo* obj_native, TEE_ObjectInfo* obj_app)
{
	obj_app->objectType = obj_native->objectType;
	obj_app->objectSize = obj_native->objectSize;
	obj_app->maxObjectSize = obj_native->maxObjectSize;
	obj_app->objectUsage = obj_native->objectUsage;
	obj_app->dataSize = obj_native->dataSize;
	obj_app->dataPosition = obj_native->dataPosition;
	obj_app->handleFlags = obj_native->handleFlags;
}

/* 5.5.1 */
static TEE_Result
TEE_GetObjectInfo1_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	TEE_ObjectInfo *objectInfo_app)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);
	TEE_Result ret;
	TEE_ObjectInfo objectInfo_native;

	if (!validate_native_addr((void*)objectInfo_app, sizeof(TEE_ObjectInfo)))
		return TEE_ERROR_BAD_PARAMETERS;

	ret = TEE_GetObjectInfo1(object, &objectInfo_native);
	if(ret == TEE_SUCCESS)
		object_info_native2app(&objectInfo_native, objectInfo_app);
	return ret;
}

/* 5.5.5 */
static void
TEE_CloseObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_CloseObject(object);
}

/* 5.7.1 */
static TEE_Result
TEE_OpenPersistentObject_wrapper(wasm_exec_env_t exec_env,
	uint32_t storageID,
	const void *objectID, uint32_t objectIDLen,
	uint32_t flags,
	TEE_ObjectHandle *object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* objectID has been checked by runtime */
	if (!validate_native_addr((void*)objectID, objectIDLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* object has been checked by runtime */
	if (!validate_native_addr((void*)object, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_OpenPersistentObject(storageID, objectID, objectIDLen, flags, object);
}

/* 5.7.2 */
static TEE_Result
TEE_CreatePersistentObject_wrapper(wasm_exec_env_t exec_env,
	uint32_t storageID,
	const void *objectID, uint32_t objectIDLen,
	uint32_t flags,
	TEE_ObjectHandle attributes,
	const void *initialData, uint32_t initialDataLen,
	TEE_ObjectHandle *object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* objectID has been checked by runtime */
	if (!validate_native_addr((void*)objectID, objectIDLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* initialData has been checked by runtime */
	if (!validate_native_addr((void*)initialData, initialDataLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* object has been checked by runtime */
	if (!validate_native_addr((void*)object, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_CreatePersistentObject(storageID, objectID, objectIDLen, flags, attributes,
		initialData, initialDataLen, object);
}

/* 5.7.4 */
static TEE_Result
TEE_CloseAndDeletePersistentObject1_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_CloseAndDeletePersistentObject1(object);
}

/* 5.7.5 */
static TEE_Result
TEE_RenamePersistentObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	void* newObjectID, size_t newObjectIDLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* newobjectID has been checked by runtime */
	if (!validate_native_addr((void*)newObjectID, newObjectIDLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_RenamePersistentObject(object, newObjectID, newObjectIDLen);
}

/* 5.9.1 */
static TEE_Result
TEE_ReadObjectData_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	void *buffer, size_t size,
	size_t *count)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)buffer, size))
		return TEE_ERROR_BAD_PARAMETERS;

	/* count has been checked by runtime */
	if (!validate_native_addr((void*)count, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_ReadObjectData(object, buffer, size, count);
}

/* 5.9.2 */
static TEE_Result
TEE_WriteObjectData_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	const void *buffer, uint32_t size)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)buffer, size))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_WriteObjectData(object, buffer, size);
}

/* 5.9.3 */
static TEE_Result
TEE_TruncateObjectData_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object, uint32_t size)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_TruncateObjectData(object, size);
}

/* 5.9.4 */
static TEE_Result
TEE_SeekObjectData_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	int64_t offset,
	TEE_Whence whence)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_SeekObjectData(object, offset, whence);
}

static TEE_Result TEE_AllocatePersistentObjectEnumerator_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectEnumHandle *objectEnumerator)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (!validate_native_addr((void*)objectEnumerator, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AllocatePersistentObjectEnumerator(objectEnumerator);
}

static void TEE_FreePersistentObjectEnumerator_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectEnumHandle objectEnumerator)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_FreePersistentObjectEnumerator(objectEnumerator);
}

static void TEE_ResetPersistentObjectEnumerator_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectEnumHandle objectEnumerator)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_ResetPersistentObjectEnumerator(objectEnumerator);
}

static TEE_Result TEE_StartPersistentObjectEnumerator_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectEnumHandle objectEnumerator,
	uint32_t storageID)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_StartPersistentObjectEnumerator(objectEnumerator, storageID);
}

static TEE_Result TEE_GetNextPersistentObject_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectEnumHandle objectEnumerator,
	TEE_ObjectInfo *objectInfo,
	void *objectID,
	size_t *objectIDLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (!validate_native_addr((void*)objectInfo, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	if (!validate_native_addr((void*)objectID, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	if (!validate_native_addr((void*)objectIDLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_GetNextPersistentObject(objectEnumerator, objectInfo, objectID, objectIDLen);
}

static void TEE_GetObjectInfo_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	TEE_ObjectInfo *objectInfo)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (!validate_native_addr((void*)objectInfo, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_GetObjectInfo(object, objectInfo);
}

static TEE_Result TEE_RestrictObjectUsage1_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectHandle object,
	uint32_t objectUsage)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_RestrictObjectUsage1(object, objectUsage);
}

static void TEE_ResetTransientObject_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectHandle object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_ResetTransientObject(object);
}

static void TEE_Panic_wrapper(
	wasm_exec_env_t exec_env,
	TEE_Result panicCode)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_Panic(panicCode);
}

/* libtomcrypt/include/tomcrypt_hash.h
 * libtomcrypt/src/misc/crypt/crypt_find_hash.c:51
 */
extern int find_hash(const char *name);

static int
find_hash_wrapper(wasm_exec_env_t exec_env,
	const char *name)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)name, 1))
		return TEE_ERROR_BAD_PARAMETERS;

	return find_hash(name);
}

/* libtomcrypt/include/tomcrypt_mac.h
 * crypto/libtomcrypt/src/mac/hmac/hmac_memory.c:59
 */
extern int hmac_memory(int hash,
			const unsigned char* key, uint32_t keylen,
			const unsigned char* in, uint32_t inlen,
			unsigned char* out, uint32_t* outlen);

static int
hmac_memory_wrapper(wasm_exec_env_t exec_env,
	int hash,
	const unsigned char *key,  unsigned long keylen,
	const unsigned char *in,   unsigned long inlen,
	unsigned char *out,  unsigned long *outlen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)key, keylen))
		return -1;

	if (!validate_native_addr((void*)in, inlen))
		return -1;

	if (!validate_native_addr((void*)outlen, sizeof(unsigned long)))
		return -1;

	if (!validate_native_addr((void*)out, *outlen))
		return -1;

	return hmac_memory(hash, key, keylen, in, inlen, out, outlen);
}

/**
 * @brief Output an unsigned int in hex format
 *
 * Output an unsigned int on output installed by platform at init time. Should
 * be able to handle an unsigned int of any size, 32 or 64 bit.
 * @param num Number to output
 *
 * @return N/A
 */
static void
_printf_hex_uint(out_func_t out, void *ctx, const uint64_t num, bool is_u64,
	enum pad_type padding, int min_width)
{
	int shift = sizeof(num) * 8;
	int found_largest_digit = 0;
	int remaining = 16; /* 16 digits max */
	int digits = 0;
	char nibble;

	while (shift >= 4) {
		shift -= 4;
		nibble = (num >> shift) & 0xf;

		if (nibble || found_largest_digit || shift == 0) {
			found_largest_digit = 1;
			nibble = (char)(nibble + (nibble > 9 ? 87 : 48));
			out((int)nibble, ctx);
			digits++;
			continue;
		}

		if (remaining-- <= min_width) {
			if (padding == PAD_ZERO_BEFORE) {
				out('0', ctx);
			} else if (padding == PAD_SPACE_BEFORE) {
				out(' ', ctx);
			}
		}
	}

	if (padding == PAD_SPACE_AFTER) {
		remaining = min_width * 2 - digits;
		while (remaining-- > 0) {
			out(' ', ctx);
		}
	}
}

/**
 * @brief Output an unsigned int in decimal format
 *
 * Output an unsigned int on output installed by platform at init time. Only
 * works with 32-bit values.
 * @param num Number to output
 *
 * @return N/A
 */
static void
_printf_dec_uint(out_func_t out, void *ctx, const uint32_t num,
	enum pad_type padding, int min_width)
{
	uint32_t pos = 999999999;
	uint32_t remainder = num;
	int found_largest_digit = 0;
	int remaining = 10; /* 10 digits max */
	int digits = 1;

	/* make sure we don't skip if value is zero */
	if (min_width <= 0) {
		min_width = 1;
	}

	while (pos >= 9) {
		if (found_largest_digit || remainder > pos) {
			found_largest_digit = 1;
			out((int)((remainder / (pos + 1)) + 48), ctx);
			digits++;
		} else if (remaining <= min_width && padding < PAD_SPACE_AFTER) {
			out((int)(padding == PAD_ZERO_BEFORE ? '0' : ' '), ctx);
			digits++;
		}
		remaining--;
		remainder %= (pos + 1);
		 pos /= 10;
	}
	out((int)(remainder + 48), ctx);

	if (padding == PAD_SPACE_AFTER) {
		remaining = min_width - digits;
		while (remaining-- > 0) {
			out(' ', ctx);
		}
	}
}

static void
print_err(out_func_t out, void *ctx)
{
	out('E', ctx);
	out('R', ctx);
	out('R', ctx);
}

static bool
_vprintf_wa(out_func_t out, void *ctx, const char *fmt, _va_list ap,
	wasm_module_inst_t module_inst)
{
	int might_format = 0; /* 1 if encountered a '%' */
	enum pad_type padding = PAD_NONE;
	int min_width = -1;
	int long_ctr = 0;
	uint8_t *native_end_addr;

	if (!wasm_runtime_get_native_addr_range(module_inst, (uint8_t *)ap, NULL,
		&native_end_addr)) {
		goto fail;
	}

	/* fmt has already been adjusted if needed */
	while (*fmt) {
		if (!might_format) {
			if (*fmt != '%') {
				out((int)*fmt, ctx);
			} else {
				might_format = 1;
				min_width = -1;
				padding = PAD_NONE;
				long_ctr = 0;
			}
		} else {
			switch (*fmt) {
				case '-':
					padding = PAD_SPACE_AFTER;
					goto still_might_format;

				case '0':
					if (min_width < 0 && padding == PAD_NONE) {
						padding = PAD_ZERO_BEFORE;
						goto still_might_format;
					}
					goto handle_1_to_9;
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
handle_1_to_9:
					if (min_width < 0) {
						min_width = *fmt - '0';
					} else {
						min_width = 10 * min_width + *fmt - '0';
					}

					if (padding == PAD_NONE) {
						padding = PAD_SPACE_BEFORE;
					}
					goto still_might_format;

				case 'l':
					long_ctr++;
					/* Fall through */
				case 'z':
				case 'h':
					/* FIXME: do nothing for these modifiers */
					goto still_might_format;

				case 'd':
				case 'i':
				{
					int32_t d;
					if (long_ctr < 2) {
						CHECK_VA_ARG(ap, int32_t);
						d = _va_arg(ap, int32_t);
					} else {
						int64_t lld;
						CHECK_VA_ARG(ap, int64_t);
						lld = _va_arg(ap, int64_t);
						if (lld > INT32_MAX || lld < INT32_MIN) {
							print_err(out, ctx);
							break;
						}
						d = (int32_t)lld;
					}

					if (d < 0) {
						out((int)'-', ctx);
						d = -d;
						min_width--;
					}
					_printf_dec_uint(out, ctx, (uint32_t)d, padding, min_width);
					break;
				}
				case 'u':
				{
					uint32_t u;

					if (long_ctr < 2) {
						CHECK_VA_ARG(ap, uint32_t);
						u = _va_arg(ap, uint32_t);
					} else {
						uint64_t llu;
						CHECK_VA_ARG(ap, uint64_t);
						llu = _va_arg(ap, uint64_t);
						if (llu > INT32_MAX) {
							print_err(out, ctx);
							break;
						}
						u = (uint32_t)llu;
					}
					_printf_dec_uint(out, ctx, u, padding, min_width);
					break;
				}
				case 'p':
					out('0', ctx);
					out('x', ctx);
					/* left-pad pointers with zeros */
					padding = PAD_ZERO_BEFORE;
					min_width = 8;
					/* Fall through */
				case 'x':
				case 'X':
				{
					uint64_t x;
					bool is_ptr = (*fmt == 'p') ? true : false;

					if (long_ctr < 2) {
						CHECK_VA_ARG(ap, uint32_t);
						x = _va_arg(ap, uint32_t);
					} else {
						CHECK_VA_ARG(ap, uint64_t);
						x = _va_arg(ap, uint64_t);
					}
					_printf_hex_uint(out, ctx, x, !is_ptr, padding, min_width);
					break;
				}
				case 's':
				{
					char *s;
					char *start;
					uint32_t s_offset;

					CHECK_VA_ARG(ap, int32_t);
					s_offset = _va_arg(ap, uint32_t);

					if (!validate_app_str_addr(s_offset)) {
						return false;
					}

					s = start = addr_app_to_native(s_offset);

					while (*s)
						out((int)(*s++), ctx);

					if (padding == PAD_SPACE_AFTER) {
						int remaining = min_width - (int32_t)(s - start);
						while (remaining-- > 0) {
							out(' ', ctx);
						}
					}
					break;
				}
				case 'c':
				{
					int c;
					CHECK_VA_ARG(ap, int);
					c = _va_arg(ap, int);
					out(c, ctx);
					break;
				}
				case '%':
				{
					out((int)'%', ctx);
					break;
				}
				case 'f':
				{
					double f64;
					char buf[16], *s;

					/* Make 8-byte aligned */
					ap = (_va_list)(((uintptr_t)ap + 7) & ~(uintptr_t)7);
					CHECK_VA_ARG(ap, double);
					f64 = _va_arg(ap, double);
					snprintf(buf, sizeof(buf), "%f", f64);
					s = buf;
					while (*s)
						out((int)(*s++), ctx);
					break;
				}
				default:
					out((int)'%', ctx);
					out((int)*fmt, ctx);
					break;
			} //end switch
			might_format = 0;
		} //end else
still_might_format:
		++fmt;
	} //end while(*fmt)
	return true;
fail:
	wasm_runtime_set_exception(module_inst, "out of bounds memory access");
	return false;
}

struct str_context {
	char *str;
	uint32_t max;
	uint32_t count;
};

static char print_buf[128] = { 0 };
static int print_buf_size = 0;

static int
printf_out(int c, struct str_context *ctx)
{
	if (c == '\n') {
		print_buf[print_buf_size] = '\0';
		syslog(LOG_INFO, "%s\n", print_buf);
		print_buf_size = 0;
	} else if (print_buf_size >= sizeof(print_buf) - 2) {
		print_buf[print_buf_size++] = (char)c;
		print_buf[print_buf_size] = '\0';
		syslog(LOG_INFO, "%s\n", print_buf);
		print_buf_size = 0;
	} else {
		print_buf[print_buf_size++] = (char)c;
	}
	ctx->count++;
	return c;
}

static char
trace_level_to_string(int level, bool level_ok)
{
	/*
	 * U = Unused
	 * E = Error
	 * I = Information
	 * D = Debug
	 * F = Flow
	 */
	static const char lvl_strs[] = { 'U', 'E', 'I', 'D', 'F' };
	int l = 0;

	if (!level_ok)
		return 'M';

	if ((level >= TRACE_MIN) && (level <= TRACE_MAX))
		l = level;

	return lvl_strs[l];
}

static void
trace_printf_wrapper(wasm_exec_env_t exec_env,
	const char *function, int line, int level, bool level_ok,
	const char *fmt, _va_list va_args)
{
	wasm_module_inst_t module_inst = get_module_inst(exec_env);
	struct str_context ctx = { NULL, 0, 0 };
	char buf[MAX_PRINT_SIZE];
	size_t boffs = 0;
	int res;

	/* format has been checked by runtime */
	if (!validate_native_addr(va_args, sizeof(int32_t)))
		return;

	res = snprintf(buf, sizeof(buf), "[%s]", "");
	if (res < 0)
		return;
	boffs += res;
	if (boffs >= sizeof(buf)) {
		goto out_put;
	}

	res = snprintf(buf + boffs, sizeof(buf) - boffs, "[%c:",
		trace_level_to_string(level, level_ok));
	if (res < 0)
		return;
	boffs += res;
	if (boffs >= sizeof(buf)) {
		goto out_put;
	}

	res = snprintf(buf + boffs, sizeof(buf) - boffs, "%s:%d] ", function, line);
	if (res < 0)
		return;
	boffs += res;

out_put:
	for( int i = 0; i < boffs; i++) {
		printf_out(buf[i], &ctx);
	}

	if (!_vprintf_wa((out_func_t)printf_out, &ctx, fmt, va_args, module_inst)) {
		EMSG("%08x\n", TEE_ERROR_GENERIC);
	}
}

/* 5.6.1 */
static TEE_Result
TEE_AllocateTransientObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectType objectType, uint32_t maxKeySize, TEE_ObjectHandle *object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* object has been checked by runtime */
	if (!validate_native_addr((void*)object, sizeof(TEE_ObjectHandle)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AllocateTransientObject(objectType, maxKeySize, object);
}

/* 5.6.2 */
static void
TEE_FreeTransientObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_FreeTransientObject(object);
}

static bool
wasm_validate_native_addr_test(struct WASMModuleInstanceCommon *module_inst,
							   void *native_ptr, uint32_t size)
{
	if (wasm_runtime_addr_native_to_app(module_inst, native_ptr) == 0) {
		return false;
	}
	if (wasm_runtime_addr_native_to_app(module_inst, native_ptr + size) == 0) {
		return false;
	}
	return true;
}

static void
tee_attribute_wasm2native(wasm_module_inst_t module_inst,
	TEE_Attribute *attr_app,
	TEE_Attribute *attr_native)
{
	attr_native->attributeID = attr_app->attributeID;

	if ((attr_native->attributeID & (1<<29)) != 0) {
		attr_native->content.value.a = attr_app->content.value.a;
		attr_native->content.value.b = attr_app->content.value.b;
	} else {
		if ((attr_app->content.ref.buffer)
			&& (!wasm_validate_native_addr_test(module_inst,
				attr_app->content.ref.buffer, attr_app->content.ref.length))) {
			attr_native->content.ref.buffer = addr_app_to_native((uint32_t)(attr_app->content.ref.buffer));
			DMSG("convert app address 0x%08lx to native address 0x%08lx\n",
				(uint32_t)attr_app->content.ref.buffer, (uint32_t)attr_native->content.ref.buffer);
		} else {
			attr_native->content.ref.buffer = attr_app->content.ref.buffer;
		}
		attr_native->content.ref.length = attr_app->content.ref.length;
	}
}

static void
tee_attribute_native2wasm(wasm_module_inst_t module_inst,
	TEE_Attribute *attr_native,
	TEE_Attribute *attr_app)
{
	attr_app->attributeID = attr_native->attributeID;

	/* This is determined by bit [29] of the attribute identifier. If this
	 * bit is set to 0, then the attribute is a buffer attribute and the
	 * field ref SHALL be selected. If the bit is set to 1, then it is a
	 * value attribute and the field value SHALL be selected.
	 */
	if ((attr_native->attributeID & (1<<29)) != 0) {
		attr_app->content.value.a = attr_native->content.value.a;
		attr_app->content.value.b = attr_native->content.value.b;
	} else {
		if (attr_native->content.ref.buffer) {
			attr_app->content.ref.buffer = (void *)(uintptr_t)addr_native_to_app(attr_native->content.ref.buffer);
			DMSG("convert native address 0x%08lx to app address 0x%08lx\n",
				(uint32_t)attr_native->content.ref.buffer, (uint32_t)attr_app->content.ref.buffer);
		} else {
			attr_app->content.ref.buffer = attr_native->content.ref.buffer;
		}
		attr_app->content.ref.length = attr_native->content.ref.length;
	}

}

/* 5.6.6 */
static void
TEE_InitRefAttribute_wrapper(wasm_exec_env_t exec_env,
	TEE_Attribute *attr_app, uint32_t attributeID,
	const void *buffer, uint32_t length)
{
	DMSG("wasm.libtee.%s\n", __func__);
	TEE_Attribute attr_native;
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	if (attr_app == NULL)
		return;

	/* convert wasm TEE_Attribute to native */
	tee_attribute_wasm2native(module_inst, attr_app, &attr_native);

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)buffer, length))
		return;

	TEE_InitRefAttribute((TEE_Attribute *)&attr_native, attributeID, buffer, length);

	/* convert native TEE_Attribute to wasm */
	tee_attribute_native2wasm(module_inst, &attr_native, attr_app);
}

/* 5.6.4 */
static TEE_Result
TEE_PopulateTransientObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object, TEE_Attribute *attrs_app, uint32_t attrCount)
{
	DMSG("wasm.libtee.%s\n", __func__);
	TEE_Result res;
	wasm_module_inst_t module_inst = get_module_inst(exec_env);
	TEE_Attribute *attrs_native = NULL;

	if ((attrs_app == NULL) || (attrCount == 0)) {
		EMSG("%08x : %p, %lu\n", TEE_ERROR_BAD_PARAMETERS, attrs_app, attrCount);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* convert wasm TEE_Attribute to native */
	attrs_native =	(TEE_Attribute *)malloc(sizeof(TEE_Attribute)*attrCount);
	if (!attrs_native) {
		EMSG("%08x : %lu\n", TEE_ERROR_OUT_OF_MEMORY, sizeof(TEE_Attribute)*attrCount);
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	for (uint32_t i = 0; i < attrCount; i++) {
		tee_attribute_wasm2native(module_inst, attrs_app + i, attrs_native + i);
	}

	res = TEE_PopulateTransientObject(object, (const TEE_Attribute *)attrs_native, attrCount);

	free(attrs_native);

	return res;
}

/* 6.2.1 */
static TEE_Result
TEE_AllocateOperation_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle *operation,
	uint32_t algorithm, uint32_t mode, uint32_t maxKeySize)
{
	TEE_Result res;
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* operation has been checked by runtime */
	if (!validate_native_addr((void*)operation, sizeof(TEE_OperationHandle)))
		return TEE_ERROR_BAD_PARAMETERS;

	DMSG("algorithm: 0x%08lx, mode: 0x%08lx, maxKeySize: %ld\n", algorithm, mode, maxKeySize);
	res = TEE_AllocateOperation(operation, algorithm, mode, maxKeySize);
	return res;
}

/* 6.2.2 */
static void TEE_FreeOperation_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_FreeOperation(operation);
}

/* 6.2.6 */
static TEE_Result
TEE_SetOperationKey_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, TEE_ObjectHandle key)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_SetOperationKey(operation, key);
}

/* 6.2.8 */
static void
TEE_CopyOperation_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle dstOperation, TEE_OperationHandle srcOperation)
{
	DMSG("wasm.libtee.%s\n", __func__);
	TEE_CopyOperation(dstOperation, srcOperation);
}

/* 6.5.1 */
static void
TEE_MACInit_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *IV, uint32_t IVLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* IV has been checked by runtime */
	if (!validate_native_addr((void*)IV, IVLen))
		return;

	TEE_MACInit(operation, IV, IVLen);
}

/* 6.5.2 */
static void
TEE_MACUpdate_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *chunk, uint32_t chunkSize)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* chunk has been checked by runtime */
	if (!validate_native_addr((void*)chunk, chunkSize))
		return;

	TEE_MACUpdate(operation, chunk, chunkSize);
}

/* 6.6.3 */
static TEE_Result
TEE_MACComputeFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *message, size_t messageLen,
	void *mac, size_t *macLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* messageLen has been checked by runtime */
	if (!validate_native_addr((void*)message, messageLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* CID 209905, SIZEOF_MISMATCH. No problem. */
	/* mac has been checked by runtime */
	if (!validate_native_addr((void*)macLen, sizeof(uint32_t *)))
		return TEE_ERROR_BAD_PARAMETERS;
	if (!validate_native_addr((void*)mac, *macLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_MACComputeFinal(operation, message, messageLen, mac, macLen);
}

/* 6.5.4 */
static TEE_Result
TEE_MACCompareFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *message, uint32_t messageLen,
	const void *mac, uint32_t macLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* messageLen has been checked by runtime */
	if (!validate_native_addr((void*)message, messageLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* mac has been checked by runtime */
	if (!validate_native_addr((void*)mac, macLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_MACCompareFinal(operation, message, messageLen, mac, macLen);
}

/* 6.3.1 */
static void
TEE_DigestUpdate_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, void *chunk, uint32_t chunkSize)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* chunk has been checked by runtime */
	if (!validate_native_addr((void*)chunk, chunkSize))
		return;

	TEE_DigestUpdate(operation, chunk, chunkSize);
}

/* 6.3.2 */
static TEE_Result
TEE_DigestDoFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, void *chunk, size_t chunkLen,
	void *hash, size_t *hashLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* chunk has been checked by runtime */
	if (!validate_native_addr((void*)chunk, chunkLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* hashLen has been checked by runtime */
	if (!validate_native_addr((void*)hashLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	/* hash has been checked by runtime */
	if (!validate_native_addr((void*)hash, *hashLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_DigestDoFinal(operation, chunk, chunkLen, hash, hashLen);
}

static TEE_Result
TEE_DigestExtract_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, void *hash, size_t *hashLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst = get_module_inst(exec_env);

	/* chunk has been checked by runtime */
	if (!validate_native_addr((void*)hash, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	/* hashLen has been checked by runtime */
	if (!validate_native_addr((void*)hashLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_DigestExtract(operation, hash, hashLen);
}

/* 6.2.5 */
static void
TEE_ResetOperation_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_ResetOperation(operation);
}

static TEE_Result
TEE_IsAlgorithmSupported_wrapper(wasm_exec_env_t exec_env,
	uint32_t algId, uint32_t element)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_IsAlgorithmSupported(algId, element);
}

static void
TEE_GetSystemTime_wrapper(wasm_exec_env_t exec_env,
	TEE_Time *time)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* time has been checked by runtime */
	if (!validate_native_addr((void*)time, sizeof(TEE_Time))) {
		EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, time);
		return;
	}

	TEE_GetSystemTime(time);
}

static TEE_Result
TEE_GenerateKey_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object, uint32_t keySize,
	const TEE_Attribute *params, uint32_t paramCount)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* params has been checked by runtime */
	if (!validate_native_addr((void*)params, sizeof(TEE_Attribute)*paramCount))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_GenerateKey(object, keySize, params, paramCount);
}

static TEE_Result
TEE_AEInit_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *nonce,
	uint32_t nonceLen, uint32_t tagLen, uint32_t AADLen,
	uint32_t payloadLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* nonce has been checked by runtime */
	if (!validate_native_addr((void*)nonce, nonceLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AEInit(operation, nonce, nonceLen, tagLen, AADLen, payloadLen);
}

static TEE_Result
TEE_AEEncryptFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const void *srcData, size_t srcLen,
	void *destData, size_t *destLen,
	void *tag, size_t *tagLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* srcData has been checked by runtime */
	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* destLen has been checked by runtime */
	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	/* destData has been checked by runtime */
	if (!validate_native_addr((void*)destData, *destLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* tagLen has been checked by runtime */
	if (!validate_native_addr((void*)tagLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	/* tag has been checked by runtime */
	if (!validate_native_addr((void*)tag, *tagLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AEEncryptFinal(operation, srcData, srcLen,
		destData, destLen, tag, tagLen);
}

static TEE_Result
TEE_AEDecryptFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const void *srcData, size_t srcLen,
	void *destData, size_t *destLen,
	void *tag, size_t tagLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* srcData has been checked by runtime */
	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* destLen has been checked by runtime */
	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	/* destData has been checked by runtime */
	if (!validate_native_addr((void*)destData, *destLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* tag has been checked by runtime */
	if (!validate_native_addr((void*)tag, tagLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AEDecryptFinal(operation, srcData, srcLen,
		destData, destLen, tag, tagLen);
}

static TEE_Result
TEE_GetObjectBufferAttribute_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object, uint32_t attributeID,
	void *buffer, size_t *size)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* size has been checked by runtime */
	if (!validate_native_addr((void*)size, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	/* buffer has been checked by runtime */
	if (!validate_native_addr((void*)buffer, *size))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_GetObjectBufferAttribute(object, attributeID, buffer, size);
}

static TEE_Result
TEE_GetObjectValueAttribute_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object, uint32_t attributeID, uint32_t *a,
	uint32_t *b)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* a has been checked by runtime */
	if (!validate_native_addr((void*)a, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	/* b has been checked by runtime */
	if (!validate_native_addr((void*)b, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_GetObjectValueAttribute(object, attributeID, a, b);
}

static void
TEE_GenerateRandom_wrapper(wasm_exec_env_t exec_env,
	void *randomBuffer, uint32_t randomBufferLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* randomBuffer has been checked by runtime */
	if (!validate_native_addr((void*)randomBuffer, randomBufferLen)) {
		EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, randomBuffer);
		return;
	}

	TEE_GenerateRandom(randomBuffer, randomBufferLen);
}

static void
TEE_CipherInit_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *IV,
	uint32_t IVLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* IV has been checked by runtime */
	if (!validate_native_addr((void*)IV, IVLen)) {
		EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, IV);
		return;
	}

	TEE_CipherInit(operation, IV, IVLen);
}

static TEE_Result
TEE_CipherUpdate_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *srcData,
	size_t srcLen, void *destData, size_t *destLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* srcData has been checked by runtime */
	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* destLen has been checked by runtime */
	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	/* destData has been checked by runtime */
	if (!validate_native_addr((void*)destData, *destLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_CipherUpdate(operation, srcData, srcLen, destData, destLen);
}

static TEE_Result
TEE_CipherDoFinal_wrapper(wasm_exec_env_t exec_env,
	TEE_OperationHandle operation, const void *srcData,
	size_t srcLen, void *destData, size_t *destLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* srcData has been checked by runtime */
	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	/* destLen has been checked by runtime */
	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	/* destData has been checked by runtime */
	if (!validate_native_addr((void*)destData, *destLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_CipherDoFinal(operation, srcData, srcLen, destData, destLen);
}

static void
TEE_InitValueAttribute_wrapper(wasm_exec_env_t exec_env,
	TEE_Attribute *attr, uint32_t attributeID,
	uint32_t a, uint32_t b)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* attr has been checked by runtime */
	if (!validate_native_addr((void*)attr, sizeof(TEE_Attribute))) {
		EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, attr);
		return;
	}

	TEE_InitValueAttribute(attr, attributeID, a, b);
}

static void
TEE_CloseAndDeletePersistentObject_wrapper(wasm_exec_env_t exec_env,
	TEE_ObjectHandle object)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_CloseAndDeletePersistentObject(object);
}

/* FOR test */
static uint32_t
sleep_wrapper(wasm_exec_env_t exec_env, uint32_t timeout_s)
{
	if (timeout_s > 0) {
		sleep(timeout_s);
	}
	return 0;
}

static TEE_Result TEE_SetOperationKey2_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	TEE_ObjectHandle key1,
	TEE_ObjectHandle key2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_SetOperationKey2(operation, key1, key2);
}

static TEE_Result TEE_CopyObjectAttributes1_wrapper(
	wasm_exec_env_t exec_env,
	TEE_ObjectHandle destObject,
	TEE_ObjectHandle srcObject)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_CopyObjectAttributes1(destObject, srcObject);
}

static TEE_Result TEE_AsymmetricEncrypt_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const TEE_Attribute *params, uint32_t paramCount,
	const void *srcData, size_t srcLen,
	void *destData, size_t *destLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, paramCount))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destData, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AsymmetricEncrypt(operation,
				params, paramCount,
				srcData, srcLen,
				destData, destLen);
}

static TEE_Result TEE_AsymmetricDecrypt_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const TEE_Attribute *params, uint32_t paramCount,
	const void *srcData, size_t srcLen,
	void *destData, size_t *destLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, paramCount))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destData, *destLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AsymmetricDecrypt(operation,
				params, paramCount,
				srcData, srcLen,
				destData, destLen);
}

static TEE_Result TEE_AsymmetricSignDigest_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const TEE_Attribute *params, uint32_t paramCount,
	const void *digest,	size_t digestLen,
	void *signature, size_t *signatureLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, paramCount))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)digest, digestLen))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)signature, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)signature, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AsymmetricSignDigest(operation,
				params, paramCount,
				digest, digestLen,
				signature, signatureLen);
}

static TEE_Result TEE_AsymmetricVerifyDigest_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const TEE_Attribute *params, uint32_t paramCount,
	const void *digest, size_t digestLen,
	const void *signature, size_t signatureLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, paramCount))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)digest, digestLen))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)signature, signatureLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AsymmetricVerifyDigest(operation,
					params, paramCount,
					digest, digestLen,
					signature, signatureLen);
}

static void TEE_DeriveKey_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const TEE_Attribute *params, uint32_t paramCount,
	TEE_ObjectHandle derivedKey)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, paramCount))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_DeriveKey(operation, params, paramCount, derivedKey);
}

static void TEE_AEUpdateAAD_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const void *AADdata, size_t AADdataLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	/* srcData has been checked by runtime */
	if (!validate_native_addr((void*)AADdata, AADdataLen))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_AEUpdateAAD(operation, AADdata, AADdataLen);
}

static TEE_Result TEE_AEUpdate_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	const void *srcData, size_t srcLen,
	void *destData, size_t *destLen)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)srcData, srcLen))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destData, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)destLen, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_AEUpdate(operation, srcData, srcLen, destData, destLen);
}

static void TEE_GetOperationInfo_wrapper(
	wasm_exec_env_t exec_env,
	TEE_OperationHandle operation,
	TEE_OperationInfo *operationInfo)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)operationInfo, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	return TEE_GetOperationInfo(operation, operationInfo);
}

static TEE_Result TEE_OpenTASession_wrapper(
	wasm_exec_env_t exec_env,
	const TEE_UUID *destination,
	uint32_t cancellationRequestTimeout,
	uint32_t paramTypes,
	TEE_Param params[TEE_NUM_PARAMS],
	TEE_TASessionHandle *session,
	uint32_t *returnOrigin)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)destination, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)params, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)session, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)returnOrigin, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_OpenTASession(destination, cancellationRequestTimeout,
				paramTypes, params, session, returnOrigin);
}

static TEE_Result TEE_InvokeTACommand_wrapper(
	wasm_exec_env_t exec_env,
	TEE_TASessionHandle session,
	uint32_t cancellationRequestTimeout,
	uint32_t commandID, uint32_t paramTypes,
	TEE_Param params[TEE_NUM_PARAMS],
	uint32_t *returnOrigin)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)params, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)returnOrigin, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_InvokeTACommand(session, cancellationRequestTimeout,
				commandID, paramTypes, params, returnOrigin);
}

static void TEE_CloseTASession_wrapper(
	wasm_exec_env_t exec_env,
	TEE_TASessionHandle session)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	TEE_CloseTASession(session);
}

static void TEE_BigIntInit_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *bigInt, size_t len)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)bigInt, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntInit(bigInt, len);
}

static size_t TEE_BigIntFMMContextSizeInU32_wrapper(
	wasm_exec_env_t exec_env,
	size_t modulusSizeInBits)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_BigIntFMMContextSizeInU32(modulusSizeInBits);
}

static void TEE_BigIntInitFMMContext_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigIntFMMContext *context, size_t len,
	const TEE_BigInt *modulus)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)context, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)modulus, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntInitFMMContext(context, len, modulus);
}

static size_t TEE_BigIntFMMSizeInU32_wrapper(
	wasm_exec_env_t exec_env, size_t modulusSizeInBits)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	return TEE_BigIntFMMSizeInU32(modulusSizeInBits);
}

static void TEE_BigIntInitFMM_wrapper(
	wasm_exec_env_t exec_env, TEE_BigIntFMM *bigIntFMM, size_t len)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)bigIntFMM, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntInitFMM(bigIntFMM, len);
}

static TEE_Result TEE_BigIntConvertFromOctetString_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const uint8_t *buffer, size_t bufferLen,
	int32_t sign)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)buffer, bufferLen))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntConvertFromOctetString(dest, buffer, bufferLen, sign);
}

static void TEE_BigIntConvertFromS32_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest, int32_t shortVal)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntConvertFromS32(dest, shortVal);
}

static int32_t TEE_BigIntCmpS32_wrapper(
	wasm_exec_env_t exec_env, const TEE_BigInt *op, int32_t shortVal)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntCmpS32(op, shortVal);
}

static TEE_Result TEE_BigIntConvertToOctetString_wrapper(
	wasm_exec_env_t exec_env,
	uint8_t *buffer, size_t *bufferLen, const TEE_BigInt *bigInt)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)buffer, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)bigInt, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntConvertToOctetString(buffer, bufferLen, bigInt);
}

static TEE_Result TEE_BigIntConvertToS32_wrapper(
	wasm_exec_env_t exec_env, int32_t *dest, const TEE_BigInt *src)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntConvertToS32(dest, src);
}

static bool TEE_BigIntGetBit_wrapper(
	wasm_exec_env_t exec_env, const TEE_BigInt *src, uint32_t bitIndex)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntGetBit(src, bitIndex);
}

static uint32_t TEE_BigIntGetBitCount_wrapper(
	wasm_exec_env_t exec_env, const TEE_BigInt *src)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntGetBitCount(src);
}

static TEE_Result TEE_BigIntSetBit_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *op,
	uint32_t bitIndex, bool value)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntSetBit(op, bitIndex, value);
}

static void TEE_BigIntShiftRight_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op, size_t bits)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntShiftRight(dest, op, bits);
}

static int32_t TEE_BigIntCmp_wrapper(
	wasm_exec_env_t exec_env,
	const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntCmp(op1, op2);
}

static void TEE_BigIntAdd_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntAdd(dest, op1, op2);
}

static void TEE_BigIntSub_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntSub(dest, op1, op2);
}

static void TEE_BigIntMul_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntMul(dest, op1, op2);
}

static void TEE_BigIntNeg_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest, const TEE_BigInt *op)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	return TEE_BigIntNeg(dest, op);
}

static TEE_Result TEE_BigIntAssign_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const TEE_BigInt *src)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntAssign(dest, src);
}

static TEE_Result TEE_BigIntAbs_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest, const TEE_BigInt *src)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntAbs(dest, src);
}

static void TEE_BigIntSquare_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest, const TEE_BigInt *op)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntSquare(dest, op);
}

static void TEE_BigIntDiv_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest_q, TEE_BigInt *dest_r,
	const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest_q, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)dest_r, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntDiv(dest_q, dest_r, op1, op2);
}

static void TEE_BigIntMod_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntMod(dest, op, n);
}

static void TEE_BigIntAddMod_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntAddMod(dest, op1, op2, n);
}

static void TEE_BigIntSubMod_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntSubMod(dest, op1, op2, n);
}

static void TEE_BigIntMulMod_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntMulMod(dest, op1, op2, n);
}

static void TEE_BigIntSquareMod_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const TEE_BigInt *op, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntSquareMod(dest, op, n);
}

static void TEE_BigIntInvMod_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const TEE_BigInt *op, const TEE_BigInt *n)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntInvMod(dest, op, n);
}

static TEE_Result TEE_BigIntExpMod_wrapper(
	wasm_exec_env_t exec_env, TEE_BigInt *dest,
	const TEE_BigInt *op1, const TEE_BigInt *op2,
	const TEE_BigInt *n, const TEE_BigIntFMMContext *context)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)context, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntExpMod(dest, op1, op2, n, context);
}

static bool TEE_BigIntRelativePrime_wrapper(
	wasm_exec_env_t exec_env, const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntRelativePrime(op1, op2);
}

static void TEE_BigIntComputeExtendedGcd_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *gcd, TEE_BigInt *u, TEE_BigInt *v,
	const TEE_BigInt *op1, const TEE_BigInt *op2)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)gcd, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)u, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)v, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntComputeExtendedGcd(gcd, u, v, op1, op2);
}

static int32_t TEE_BigIntIsProbablePrime_wrapper(
	wasm_exec_env_t exec_env, const TEE_BigInt *op, uint32_t confidenceLevel)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)op, sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_BigIntIsProbablePrime(op, confidenceLevel);
}

static void TEE_BigIntConvertToFMM_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigIntFMM *dest, const TEE_BigInt *src,
	const TEE_BigInt *n, const TEE_BigIntFMMContext *context)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)context, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntConvertToFMM(dest, src, n, context);
}

static void TEE_BigIntConvertFromFMM_wrapper(
	wasm_exec_env_t exec_env,
	TEE_BigInt *dest, const TEE_BigIntFMM *src,
	const TEE_BigInt *n, const TEE_BigIntFMMContext *context)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)src, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)context, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntConvertFromFMM(dest, src, n, context);
}

static void TEE_BigIntComputeFMM_wrapper(
	wasm_exec_env_t exec_env, TEE_BigIntFMM *dest,
	const TEE_BigIntFMM *op1, const TEE_BigIntFMM *op2,
	const TEE_BigInt *n, const TEE_BigIntFMMContext *context)
{
	DMSG("wasm.libtee.%s\n", __func__);
	wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

	if (!validate_native_addr((void*)dest, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op1, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)op2, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)n, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	if (!validate_native_addr((void*)context, sizeof(uint32_t)))
		TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

	TEE_BigIntComputeFMM(dest, op1, op2, n, context);
}

#define REG_NATIVE_FUNC(func_name, signature)  \
	{ #func_name, func_name##_wrapper, signature, NULL }

static NativeSymbol native_symbols_libtee_builtin[] = {
	REG_NATIVE_FUNC(TEE_Malloc, "(ii)i"),
	REG_NATIVE_FUNC(TEE_Realloc, "(ii)i"),
	REG_NATIVE_FUNC(TEE_Free, "(*)"),
	REG_NATIVE_FUNC(TEE_MemMove, "(**~)i"),
	REG_NATIVE_FUNC(TEE_MemCompare, "(**~)i"),
	REG_NATIVE_FUNC(TEE_MemFill, "(*ii)"),
	REG_NATIVE_FUNC(TEE_GetObjectInfo1, "(i*)i"),
	REG_NATIVE_FUNC(TEE_CloseObject, "(i)"),
	REG_NATIVE_FUNC(TEE_OpenPersistentObject, "(i*~i*)i"),
	REG_NATIVE_FUNC(TEE_CreatePersistentObject, "(i*~ii*~*)i"),
	REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject1, "(i)i"),
	REG_NATIVE_FUNC(TEE_RenamePersistentObject, "(i*~)i"),
	REG_NATIVE_FUNC(TEE_ReadObjectData, "(i*~*)i"),
	REG_NATIVE_FUNC(TEE_WriteObjectData, "(i*~)i"),
	REG_NATIVE_FUNC(TEE_TruncateObjectData, "(ii)i"),
	REG_NATIVE_FUNC(TEE_SeekObjectData, "(iIi)i"),
	REG_NATIVE_FUNC(find_hash, "(*)i"),
	REG_NATIVE_FUNC(hmac_memory, "(i*~*~**)i"),
	REG_NATIVE_FUNC(trace_printf, "($iii$*)"),

	REG_NATIVE_FUNC(TEE_AllocateTransientObject, "(ii*)i"),
	REG_NATIVE_FUNC(TEE_FreeTransientObject, "(i)"),
	REG_NATIVE_FUNC(TEE_InitRefAttribute, "(*i*~)"),
	REG_NATIVE_FUNC(TEE_PopulateTransientObject, "(i*i)i"),
	REG_NATIVE_FUNC(TEE_AllocateOperation, "(*iii)i"),
	REG_NATIVE_FUNC(TEE_FreeOperation, "(i)"),
	REG_NATIVE_FUNC(TEE_SetOperationKey, "(ii)i"),
	REG_NATIVE_FUNC(TEE_CopyOperation, "(ii)"),
	REG_NATIVE_FUNC(TEE_MACInit, "(i*~)"),
	REG_NATIVE_FUNC(TEE_MACUpdate, "(i*~)"),
	REG_NATIVE_FUNC(TEE_MACComputeFinal, "(i*~**)i"),
	REG_NATIVE_FUNC(TEE_MACCompareFinal, "(i*~*~)i"),

	REG_NATIVE_FUNC(TEE_DigestUpdate, "(i*~)"),
	REG_NATIVE_FUNC(TEE_DigestDoFinal, "(i*~**)i"),
	REG_NATIVE_FUNC(TEE_DigestExtract, "(i**)i"),
	REG_NATIVE_FUNC(TEE_ResetOperation, "(i)"),
	REG_NATIVE_FUNC(TEE_IsAlgorithmSupported, "(ii)i"),

	REG_NATIVE_FUNC(sleep, "(i)i"),
	REG_NATIVE_FUNC(TEE_GetSystemTime, "(*)"),
	REG_NATIVE_FUNC(TEE_GenerateKey, "(ii*i)i"),
	REG_NATIVE_FUNC(TEE_AEInit, "(i*~iii)i"),
	REG_NATIVE_FUNC(TEE_AEEncryptFinal, "(i*~****)i"),
	REG_NATIVE_FUNC(TEE_AEDecryptFinal, "(i*~***~)i"),
	REG_NATIVE_FUNC(TEE_GetObjectBufferAttribute, "(ii**)i"),
	REG_NATIVE_FUNC(TEE_GetObjectValueAttribute, "(ii**)i"),
	REG_NATIVE_FUNC(TEE_GenerateRandom, "(*~)"),
	REG_NATIVE_FUNC(TEE_CipherInit, "(i*~)"),
	REG_NATIVE_FUNC(TEE_CipherUpdate, "(i*~**)i"),
	REG_NATIVE_FUNC(TEE_CipherDoFinal, "(i*~**)i"),
	REG_NATIVE_FUNC(TEE_InitValueAttribute, "(*iii)"),
	REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject, "(i)"),

	REG_NATIVE_FUNC(TEE_AllocatePersistentObjectEnumerator, "(*)i"),
	REG_NATIVE_FUNC(TEE_FreePersistentObjectEnumerator, "(i)"),
	REG_NATIVE_FUNC(TEE_ResetPersistentObjectEnumerator, "(i)"),
	REG_NATIVE_FUNC(TEE_StartPersistentObjectEnumerator, "(ii)i"),
	REG_NATIVE_FUNC(TEE_GetNextPersistentObject, "(i***)i"),
	REG_NATIVE_FUNC(TEE_GetObjectInfo, "(i*)"),
	REG_NATIVE_FUNC(TEE_RestrictObjectUsage1, "(ii)i"),
	REG_NATIVE_FUNC(TEE_ResetTransientObject, "(i)"),
	REG_NATIVE_FUNC(TEE_Panic, "(i)"),
	REG_NATIVE_FUNC(TEE_SetOperationKey2, "(iii)i"),
	REG_NATIVE_FUNC(TEE_CopyObjectAttributes1, "(ii)i"),
	REG_NATIVE_FUNC(TEE_AsymmetricEncrypt, "(i*~*~**)i"),
	REG_NATIVE_FUNC(TEE_AsymmetricDecrypt, "(i*~*~**)i"),
	REG_NATIVE_FUNC(TEE_AsymmetricSignDigest, "(i*~*~**)i"),
	REG_NATIVE_FUNC(TEE_AsymmetricVerifyDigest, "(i*~*~*~)i"),
	REG_NATIVE_FUNC(TEE_DeriveKey, "(i*~i)"),
	REG_NATIVE_FUNC(TEE_AEUpdateAAD, "(i*~)"),
	REG_NATIVE_FUNC(TEE_AEUpdate, "(i*~**)i"),
	REG_NATIVE_FUNC(TEE_GetOperationInfo, "(i*)"),
	REG_NATIVE_FUNC(TEE_OpenTASession, "(*ii***)i"),
	REG_NATIVE_FUNC(TEE_InvokeTACommand, "(iiii**)i"),
	REG_NATIVE_FUNC(TEE_CloseTASession, "(i)"),
	REG_NATIVE_FUNC(TEE_BigIntInit, "(*i)"),
	REG_NATIVE_FUNC(TEE_BigIntFMMContextSizeInU32, "(i)i"),
	REG_NATIVE_FUNC(TEE_BigIntInitFMMContext, "(*i*)"),
	REG_NATIVE_FUNC(TEE_BigIntFMMSizeInU32, "(i)i"),
	REG_NATIVE_FUNC(TEE_BigIntInitFMM, "(*i)"),
	REG_NATIVE_FUNC(TEE_BigIntConvertFromOctetString, "(**~i)i"),
	REG_NATIVE_FUNC(TEE_BigIntConvertFromS32, "(*i)"),
	REG_NATIVE_FUNC(TEE_BigIntCmpS32, "(*i)i"),
	REG_NATIVE_FUNC(TEE_BigIntConvertToOctetString, "(***)i"),
	REG_NATIVE_FUNC(TEE_BigIntConvertToS32, "(**)i"),
	REG_NATIVE_FUNC(TEE_BigIntGetBit, "(*i)i"),
	REG_NATIVE_FUNC(TEE_BigIntSetBit, "(*ii)i"),
	REG_NATIVE_FUNC(TEE_BigIntShiftRight, "(**i)"),
	REG_NATIVE_FUNC(TEE_BigIntCmp, "(**)i"),
	REG_NATIVE_FUNC(TEE_BigIntAdd, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntSub, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntMul, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntNeg, "(**)"),
	REG_NATIVE_FUNC(TEE_BigIntAssign, "(**)i"),
	REG_NATIVE_FUNC(TEE_BigIntAbs, "(**)i"),
	REG_NATIVE_FUNC(TEE_BigIntSquare, "(**)"),
	REG_NATIVE_FUNC(TEE_BigIntDiv, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntMod, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntAddMod, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntSubMod, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntMulMod, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntSquareMod, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntInvMod, "(***)"),
	REG_NATIVE_FUNC(TEE_BigIntExpMod, "(*****)i"),
	REG_NATIVE_FUNC(TEE_BigIntRelativePrime, "(**)i"),
	REG_NATIVE_FUNC(TEE_BigIntComputeExtendedGcd, "(*****)"),
	REG_NATIVE_FUNC(TEE_BigIntIsProbablePrime, "(*i)i"),
	REG_NATIVE_FUNC(TEE_BigIntConvertToFMM, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntConvertFromFMM, "(****)"),
	REG_NATIVE_FUNC(TEE_BigIntComputeFMM, "(*****)"),
	REG_NATIVE_FUNC(TEE_BigIntGetBitCount, "(*)i"),
};

uint32_t
get_libtee_builtin_export_apis(NativeSymbol **p_libtee_builtin_apis)
{
	*p_libtee_builtin_apis = native_symbols_libtee_builtin;
	return sizeof(native_symbols_libtee_builtin) / sizeof(NativeSymbol);
}
