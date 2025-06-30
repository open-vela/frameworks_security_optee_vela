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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <tee_api.h>
#include <tee_api_defines.h>
#include <tee_api_types.h>
#include <tee_internal_api.h>
#include <trace.h>

#include "wasm_export.h"

void wasm_runtime_set_exception(wasm_module_inst_t module, const char* exception);

uint32_t
wasm_runtime_get_temp_ret(wasm_module_inst_t module);

void wasm_runtime_set_temp_ret(wasm_module_inst_t module, uint32_t temp_ret);

uint32_t
wasm_runtime_get_llvm_stack(wasm_module_inst_t module);

void wasm_runtime_set_llvm_stack(wasm_module_inst_t module, uint32_t llvm_stack);

uint64_t
wasm_runtime_module_realloc(wasm_module_inst_t module, uint64_t ptr, uint64_t size,
    void** p_native_addr);

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

typedef int (*out_func_t)(int c, void* ctx);

enum pad_type {
    PAD_NONE,
    PAD_ZERO_BEFORE,
    PAD_SPACE_BEFORE,
    PAD_SPACE_AFTER,
};

typedef char* _va_list;
#define _INTSIZEOF(n) \
    (((uint32_t)sizeof(n) + 3) & (uint32_t)~3)
#define _va_arg(ap, t) \
    (*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))

#define CHECK_VA_ARG(ap, t)                                 \
    do {                                                    \
        if ((uint8_t*)ap + _INTSIZEOF(t) > native_end_addr) \
            goto fail;                                      \
    } while (0)

/* 4.11.4 */
static void*
TEE_Malloc_wrapper(wasm_exec_env_t exec_env,
    size_t size, uint32_t hint)
{
    DMSG("wasm.libtee.%s: size: %zu, hint: 0x%" PRIx32 "\n", __func__, size, hint);
    uintptr_t ret_offset = 0;
    uint8_t* ret_ptr;

    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (size >= UINT32_MAX) {
        return 0;
    }

    ret_offset = module_malloc(size, (void**)&ret_ptr);
    if ((hint == TEE_MALLOC_FILL_ZERO) && (ret_offset)) {
        memset(ret_ptr, 0, size);
    }

    DMSG("wasm.libtee.%s: app_ptr: 0x%" PRIXPTR ", native_ptr: 0x%p", __func__, ret_offset, ret_ptr);
    return (void*)ret_offset;
}

/* 4.11.5 */
static void*
TEE_Realloc_wrapper(wasm_exec_env_t exec_env,
    void* buffer, size_t newSize)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_app_addr((uintptr_t)buffer, sizeof(uintptr_t))) {
        return NULL;
    }
    return (void*)(uintptr_t)wasm_runtime_module_realloc(module_inst, (uintptr_t)buffer, newSize, NULL);
}

/* 4.11.6 */
static void
TEE_Free_wrapper(wasm_exec_env_t exec_env,
    void* buffer)
{
    DMSG("wasm.libtee.%s: buffer: 0x%" PRIXPTR "\n", __func__, (uintptr_t)buffer);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)buffer, sizeof(uintptr_t))) {
        return;
    }

    void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);
    module_free(addr_native_to_app(buffer_ptr));
}

/* 4.11.7 */
static void*
TEE_MemMove_wrapper(wasm_exec_env_t exec_env,
    void* dst, const void* src, size_t size)
{
    DMSG("wasm.libtee.%s: size=%zu\n", __func__, size);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (size == 0)
        return NULL;

    /* dst has been checked by runtime */
    if (!validate_app_addr((uintptr_t)dst, size))
        return NULL;

    void* dst_ptr = addr_app_to_native((uintptr_t)dst);

    /* src has been checked by runtime */
    if (!validate_app_addr((uintptr_t)src, size))
        return NULL;

    void* src_ptr = addr_app_to_native((uintptr_t)src);

    return TEE_MemMove(dst_ptr, src_ptr, size);
}

/* 4.11.8 */
static int32_t
TEE_MemCompare_wrapper(wasm_exec_env_t exec_env,
    const void* s1, const void* s2, size_t size)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* s1 has been checked by runtime */
    if (!validate_app_addr((uintptr_t)s1, size))
        return 0;

    const void* s1_ptr = addr_app_to_native((uintptr_t)s1);
    /* s2 has been checked by runtime */
    if (!validate_app_addr((uintptr_t)s2, size))
        return 0;

    const void* s2_ptr = addr_app_to_native((uintptr_t)s2);
    return TEE_MemCompare(s1_ptr, s2_ptr, size);
}

/* 4.11.9 */
static void
TEE_MemFill_wrapper(wasm_exec_env_t exec_env,
    void* buffer, uint32_t x, size_t size)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)buffer, size))
        return;
    void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);
    memset(buffer_ptr, x, size);
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
    TEE_ObjectInfo* objectInfo_app)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    TEE_Result ret;
    TEE_ObjectInfo objectInfo_native;

    if (!validate_app_addr((uintptr_t)objectInfo_app, sizeof(TEE_ObjectInfo)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectInfo* objectInfo_app_ptr = addr_app_to_native((uintptr_t)objectInfo_app);
    ret = TEE_GetObjectInfo1(object, &objectInfo_native);
    if (ret == TEE_SUCCESS)
        object_info_native2app(&objectInfo_native, objectInfo_app_ptr);
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
    const void* objectID, size_t objectIDLen,
    uint32_t flags,
    TEE_ObjectHandle* object)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* objectID has been checked by runtime */
    if (!validate_app_addr((uintptr_t)objectID, objectIDLen))
        return TEE_ERROR_BAD_PARAMETERS;

    void* objectID_ptr = addr_app_to_native((uintptr_t)objectID);

    /* object has been checked by runtime */
    if (!validate_app_addr((uintptr_t)object, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectHandle* object_ptr = addr_app_to_native((uintptr_t)object);
    return TEE_OpenPersistentObject(storageID, objectID_ptr, objectIDLen, flags, object_ptr);
}

/* 5.7.2 */
static TEE_Result
TEE_CreatePersistentObject_wrapper(wasm_exec_env_t exec_env,
    uint32_t storageID,
    const void* objectID, size_t objectIDLen,
    uint32_t flags,
    TEE_ObjectHandle attributes,
    const void* initialData, size_t initialDataLen,
    TEE_ObjectHandle* object)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* objectID has been checked by runtime */
    if (!validate_app_addr((uintptr_t)objectID, objectIDLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* objectID_ptr = addr_app_to_native((uintptr_t)objectID);

    /* initialData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)initialData, initialDataLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* initialData_ptr = addr_app_to_native((uintptr_t)initialData);

    /* object has been checked by runtime */
    if (!validate_app_addr((uintptr_t)object, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectHandle* object_ptr = addr_app_to_native((uintptr_t)object);

    TEE_Result ret = TEE_CreatePersistentObject(storageID, objectID_ptr, objectIDLen, flags, attributes,
        initialData_ptr, initialDataLen, object_ptr);

    return ret;
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
    const void* newObjectID, size_t newObjectIDLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* newobjectID has been checked by runtime */
    if (!validate_app_addr((uintptr_t)newObjectID, newObjectIDLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* newObjectID_ptr = addr_app_to_native((uintptr_t)newObjectID);

    return TEE_RenamePersistentObject(object, newObjectID_ptr, newObjectIDLen);
}

/* 5.9.1 */
static TEE_Result
TEE_ReadObjectData_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object,
    void* buffer, size_t size,
    size_t* count)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)buffer, size))
        return TEE_ERROR_BAD_PARAMETERS;

    void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);

    /* count has been checked by runtime */
    if (!validate_app_addr((uintptr_t)count, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* count_ptr = addr_app_to_native((uintptr_t)count);

    return TEE_ReadObjectData(object, buffer_ptr, size, count_ptr);
}

/* 5.9.2 */
static TEE_Result
TEE_WriteObjectData_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object,
    const void* buffer, size_t size)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)buffer, size))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);

    return TEE_WriteObjectData(object, buffer_ptr, size);
}

/* 5.9.3 */
static TEE_Result
TEE_TruncateObjectData_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object, size_t size)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    return TEE_TruncateObjectData(object, size);
}

/* 5.9.4 */
static TEE_Result
TEE_SeekObjectData_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object,
    intmax_t offset,
    TEE_Whence whence)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    return TEE_SeekObjectData(object, offset, whence);
}

static TEE_Result TEE_AllocatePersistentObjectEnumerator_wrapper(
    wasm_exec_env_t exec_env,
    TEE_ObjectEnumHandle* objectEnumerator)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)objectEnumerator, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectEnumHandle* objectEnumerator_ptr = addr_app_to_native((uintptr_t)objectEnumerator);

    return TEE_AllocatePersistentObjectEnumerator(objectEnumerator_ptr);
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
    TEE_ObjectInfo* objectInfo,
    void* objectID,
    size_t* objectIDLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)objectInfo, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectInfo* objectInfo_ptr = addr_app_to_native((uintptr_t)objectInfo);

    if (!validate_app_addr((uintptr_t)objectID, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* objectID_ptr = addr_app_to_native((uintptr_t)objectID);

    if (!validate_app_addr((uintptr_t)objectIDLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* objectIDLen_ptr = addr_app_to_native((uintptr_t)objectIDLen);

    return TEE_GetNextPersistentObject(objectEnumerator, objectInfo_ptr, objectID_ptr, objectIDLen_ptr);
}

static void TEE_GetObjectInfo_wrapper(
    wasm_exec_env_t exec_env,
    TEE_ObjectHandle object,
    TEE_ObjectInfo* objectInfo)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)objectInfo, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_ObjectInfo* objectInfo_ptr = addr_app_to_native((uintptr_t)objectInfo);

    TEE_GetObjectInfo(object, objectInfo_ptr);
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
extern int find_hash(const char* name);

static int
find_hash_wrapper(wasm_exec_env_t exec_env,
    const char* name)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)name, 1))
        return TEE_ERROR_BAD_PARAMETERS;

    const char* name_ptr = addr_app_to_native((uintptr_t)name);
    return find_hash(name_ptr);
}

/* libtomcrypt/include/tomcrypt_mac.h
 * crypto/libtomcrypt/src/mac/hmac/hmac_memory.c:59
 */
extern int hmac_memory(int hash,
    const unsigned char* key, uint32_t keylen,
    const unsigned char* in, uint32_t inlen,
    unsigned char* out, unsigned long* outlen);

static int
hmac_memory_wrapper(wasm_exec_env_t exec_env,
    int hash,
    const unsigned char* key, unsigned long keylen,
    const unsigned char* in, unsigned long inlen,
    unsigned char* out, unsigned long* outlen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)key, keylen))
        return -1;

    const unsigned char* key_ptr = addr_app_to_native((uintptr_t)key);

    if (!validate_app_addr((uintptr_t)in, inlen))
        return -1;

    const unsigned char* in_ptr = addr_app_to_native((uintptr_t)in);

    if (!validate_app_addr((uintptr_t)outlen, sizeof(unsigned long)))
        return -1;

    unsigned long* outlen_ptr = addr_app_to_native((uintptr_t)outlen);

    if (!validate_app_addr((uintptr_t)out, *outlen))
        return -1;

    unsigned char* out_ptr = addr_app_to_native((uintptr_t)out);

    return hmac_memory(hash, key_ptr, keylen, in_ptr, inlen, out_ptr, outlen_ptr);
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
_printf_hex_uint(out_func_t out, void* ctx, const uint64_t num, bool is_u64,
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
_printf_dec_uint(out_func_t out, void* ctx, const uint32_t num,
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
print_err(out_func_t out, void* ctx)
{
    out('E', ctx);
    out('R', ctx);
    out('R', ctx);
}

static bool
_vprintf_wa(out_func_t out, void* ctx, const char* fmt, _va_list ap,
    wasm_module_inst_t module_inst)
{
    int might_format = 0; /* 1 if encountered a '%' */
    enum pad_type padding = PAD_NONE;
    int min_width = -1;
    int long_ctr = 0;
    uint8_t* native_end_addr;

    if (!wasm_runtime_get_native_addr_range(module_inst, (uint8_t*)ap, NULL,
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
            case 'i': {
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
            case 'u': {
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
            case 'X': {
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
            case 's': {
                char* s;
                char* start;
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
            case 'c': {
                int c;
                CHECK_VA_ARG(ap, int);
                c = _va_arg(ap, int);
                out(c, ctx);
                break;
            }
            case '%': {
                out((int)'%', ctx);
                break;
            }
            case 'f': {
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
            } // end switch
            might_format = 0;
        } // end else
    still_might_format:
        ++fmt;
    } // end while(*fmt)
    return true;
fail:
    wasm_runtime_set_exception(module_inst, "out of bounds memory access");
    return false;
}

struct str_context {
    char* str;
    uint32_t max;
    uint32_t count;
};

static char print_buf[128] = { 0 };
static int print_buf_size = 0;

static int
printf_out(int c, struct str_context* ctx)
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
    const char* function, int line, int level, bool level_ok,
    const char* fmt, _va_list va_args)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    struct str_context ctx = { NULL, 0, 0 };
    char buf[MAX_PRINT_SIZE];
    size_t boffs = 0;
    int res;

    /* format has been checked by runtime */
    void* args_ptr = addr_app_to_native((uintptr_t)(va_args));
    if (!validate_native_addr(args_ptr, sizeof(uintptr_t)))
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

    const char* wasm_func_str = addr_app_to_native((uintptr_t)function);
    res = snprintf(buf + boffs, sizeof(buf) - boffs, "%s:%d] ", wasm_func_str, line);
    if (res < 0)
        return;
    boffs += res;

out_put:
    for (int i = 0; i < boffs; i++) {
        printf_out(buf[i], &ctx);
    }

    const char* fmt_ptr = addr_app_to_native((uintptr_t)fmt);
    if (!_vprintf_wa((out_func_t)printf_out, &ctx, fmt_ptr, args_ptr, module_inst)) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
    }
}

/* 5.6.1 */
static TEE_Result
TEE_AllocateTransientObject_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectType objectType, uint32_t maxKeySize, TEE_ObjectHandle* object)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* object has been checked by runtime */
    if (!validate_app_addr((uintptr_t)object, sizeof(TEE_ObjectHandle)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_ObjectHandle* object_ptr = addr_app_to_native((uintptr_t)object);
    return TEE_AllocateTransientObject(objectType, maxKeySize, object_ptr);
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

static void
tee_attribute_wasm2native(wasm_module_inst_t module_inst,
    TEE_Attribute* attr_app,
    TEE_Attribute* attr_native)
{
    attr_native->attributeID = attr_app->attributeID;

    if ((attr_native->attributeID & (1 << 29)) != 0) {
        attr_native->content.value.a = attr_app->content.value.a;
        attr_native->content.value.b = attr_app->content.value.b;
    } else {
        if (attr_app->content.ref.buffer) {
            if (validate_app_addr((uintptr_t)attr_app->content.ref.buffer, attr_app->content.ref.length)) {
                attr_native->content.ref.buffer = addr_app_to_native((uintptr_t)attr_app->content.ref.buffer);
                DMSG("convert app address 0x%" PRIXPTR " to native address 0x%" PRIXPTR "\n",
                    (uintptr_t)attr_app->content.ref.buffer, (uintptr_t)attr_native->content.ref.buffer);
            }
        } else {
            attr_native->content.ref.buffer = attr_app->content.ref.buffer;
        }
        attr_native->content.ref.length = attr_app->content.ref.length;
    }
}

static void
tee_attribute_native2wasm(wasm_module_inst_t module_inst,
    TEE_Attribute* attr_native,
    TEE_Attribute* attr_app)
{
    attr_app->attributeID = attr_native->attributeID;

    /* This is determined by bit [29] of the attribute identifier. If this
     * bit is set to 0, then the attribute is a buffer attribute and the
     * field ref SHALL be selected. If the bit is set to 1, then it is a
     * value attribute and the field value SHALL be selected.
     */
    if ((attr_native->attributeID & (1 << 29)) != 0) {
        attr_app->content.value.a = attr_native->content.value.a;
        attr_app->content.value.b = attr_native->content.value.b;
    } else {
        if (attr_native->content.ref.buffer) {
            attr_app->content.ref.buffer = (void*)(uintptr_t)addr_native_to_app(attr_native->content.ref.buffer);
            DMSG("convert native address 0x%" PRIXPTR " to app address 0x%" PRIXPTR "\n",
                (uintptr_t)attr_native->content.ref.buffer, (uintptr_t)attr_app->content.ref.buffer);
        } else {
            attr_app->content.ref.buffer = attr_native->content.ref.buffer;
        }
        attr_app->content.ref.length = attr_native->content.ref.length;
    }
}

/* 5.6.6 */
static void
TEE_InitRefAttribute_wrapper(wasm_exec_env_t exec_env,
    TEE_Attribute* attr_app, uint32_t attributeID,
    const void* buffer, size_t length)
{
    DMSG("wasm.libtee.%s\n", __func__);
    TEE_Attribute attr_native;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (attr_app == NULL)
        return;

    if (!validate_app_addr((uintptr_t)attr_app, length))
        return;

    TEE_Attribute* attr_app_ptr = addr_app_to_native((uintptr_t)attr_app);

    /* convert wasm TEE_Attribute to native */
    tee_attribute_wasm2native(module_inst, attr_app_ptr, &attr_native);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)buffer, length))
        return;

    const void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);

    TEE_InitRefAttribute((TEE_Attribute*)&attr_native, attributeID, buffer_ptr, length);

    /* convert native TEE_Attribute to wasm */
    tee_attribute_native2wasm(module_inst, &attr_native, attr_app_ptr);

    attr_app = (TEE_Attribute*)(uintptr_t)addr_native_to_app(attr_app_ptr);
}

/* 5.6.4 */
static TEE_Result
TEE_PopulateTransientObject_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object, TEE_Attribute* attrs_app, uint32_t attrCount)
{
    DMSG("wasm.libtee.%s\n", __func__);
    TEE_Result res;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    TEE_Attribute* attrs_native = NULL;

    if (!validate_app_addr((uintptr_t)attrs_app, sizeof(TEE_Attribute)))
        return TEE_ERROR_BAD_PARAMETERS;

    if ((attrCount == 0)) {
        EMSG("attrs count: %" PRIx32 "\n", attrCount);
        return TEE_ERROR_BAD_PARAMETERS;
    }

    TEE_Attribute* attrs_app_ptr = addr_app_to_native((uintptr_t)attrs_app);

    /* convert wasm TEE_Attribute to native */
    attrs_native = (TEE_Attribute*)malloc(sizeof(TEE_Attribute) * attrCount);
    if (!attrs_native) {
        EMSG("%08x : 0x%zx\n", TEE_ERROR_OUT_OF_MEMORY, (size_t)(sizeof(TEE_Attribute) * attrCount));
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i < attrCount; i++) {
        tee_attribute_wasm2native(module_inst, attrs_app_ptr + i, attrs_native + i);
    }

    res = TEE_PopulateTransientObject(object, (const TEE_Attribute*)attrs_native, attrCount);

    free(attrs_native);
    return res;
}

/* 6.2.1 */
static TEE_Result
TEE_AllocateOperation_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle* operation,
    uint32_t algorithm, uint32_t mode, uint32_t maxKeySize)
{
    TEE_Result res;
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* operation has been checked by runtime */
    if (!validate_app_addr((uintptr_t)operation, sizeof(TEE_OperationHandle)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_OperationHandle* operation_ptr = addr_app_to_native((uintptr_t)operation);
    DMSG("algorithm: 0x%" PRIx32 ", mode: 0x%" PRIx32 ", maxKeySize: %" PRIu32 "\n", algorithm, mode, maxKeySize);
    res = TEE_AllocateOperation(operation_ptr, algorithm, mode, maxKeySize);
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
    TEE_OperationHandle operation, const void* IV, size_t IVLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* IV has been checked by runtime */
    if (!validate_app_addr((uintptr_t)IV, IVLen))
        return;

    const void* IV_ptr = addr_app_to_native((uintptr_t)IV);
    TEE_MACInit(operation, IV_ptr, IVLen);
}

/* 6.5.2 */
static void
TEE_MACUpdate_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* chunk, size_t chunkSize)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* chunk has been checked by runtime */
    if (!validate_app_addr((uintptr_t)chunk, chunkSize))
        return;

    const void* chunk_ptr = addr_app_to_native((uintptr_t)chunk);
    TEE_MACUpdate(operation, chunk_ptr, chunkSize);
}

/* 6.6.3 */
static TEE_Result
TEE_MACComputeFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* message, size_t messageLen,
    void* mac, size_t* macLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* messageLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)message, messageLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* message_ptr = addr_app_to_native((uintptr_t)message);

    /* CID 209905, SIZEOF_MISMATCH. No problem. */
    /* mac has been checked by runtime */
    if (!validate_app_addr((uintptr_t)macLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* macLen_ptr = addr_app_to_native((uintptr_t)macLen);
    if (!validate_app_addr((uintptr_t)mac, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* mac_ptr = addr_app_to_native((uintptr_t)mac);
    return TEE_MACComputeFinal(operation, message_ptr, messageLen, mac_ptr, macLen_ptr);
}

/* 6.5.4 */
static TEE_Result
TEE_MACCompareFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* message, size_t messageLen,
    const void* mac, size_t macLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* messageLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)message, messageLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* message_ptr = addr_app_to_native((uintptr_t)message);
    /* mac has been checked by runtime */
    if (!validate_app_addr((uintptr_t)mac, macLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* mac_ptr = addr_app_to_native((uintptr_t)mac);
    return TEE_MACCompareFinal(operation, message_ptr, messageLen, mac_ptr, macLen);
}

/* 6.3.1 */
static void
TEE_DigestUpdate_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* chunk, size_t chunkSize)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* chunk has been checked by runtime */
    if (!validate_app_addr((uintptr_t)chunk, chunkSize))
        return;

    const void* chunk_ptr = addr_app_to_native((uintptr_t)chunk);
    TEE_DigestUpdate(operation, chunk_ptr, chunkSize);
}

/* 6.3.2 */
static TEE_Result
TEE_DigestDoFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* chunk, size_t chunkLen,
    void* hash, size_t* hashLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* chunk has been checked by runtime */
    if (!validate_app_addr((uintptr_t)chunk, chunkLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* chunk_ptr = addr_app_to_native((uintptr_t)chunk);

    /* hashLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)hashLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* hashLen_ptr = addr_app_to_native((uintptr_t)hashLen);

    /* hash has been checked by runtime */
    if (!validate_app_addr((uintptr_t)hash, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* hash_ptr = addr_app_to_native((uintptr_t)hash);

    return TEE_DigestDoFinal(operation, chunk_ptr, chunkLen, hash_ptr, hashLen_ptr);
}

static TEE_Result
TEE_DigestExtract_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, void* hash, size_t* hashLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    /* chunk has been checked by runtime */
    if (!validate_app_addr((uintptr_t)hash, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* hash_ptr = addr_app_to_native((uintptr_t)hash);

    /* hashLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)hashLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* hashLen_ptr = addr_app_to_native((uintptr_t)hashLen);

    return TEE_DigestExtract(operation, hash_ptr, hashLen_ptr);
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
    TEE_Time* time)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* time has been checked by runtime */
    if (!validate_app_addr((uintptr_t)time, sizeof(TEE_Time))) {
        EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, time);
        return;
    }

    TEE_Time* time_ptr = addr_app_to_native((uintptr_t)time);
    TEE_GetSystemTime(time_ptr);
}

static TEE_Result
TEE_GenerateKey_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object, uint32_t keySize,
    const TEE_Attribute* params, uint32_t paramCount)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* params has been checked by runtime */
    if (!validate_app_addr((uintptr_t)params, sizeof(TEE_Attribute) * paramCount))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_Attribute* params_ptr = addr_app_to_native((uintptr_t)params);
    return TEE_GenerateKey(object, keySize, params_ptr, paramCount);
}

static TEE_Result
TEE_AEInit_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* nonce,
    size_t nonceLen, uint32_t tagLen, size_t AADLen,
    size_t payloadLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* nonce has been checked by runtime */
    if (!validate_app_addr((uintptr_t)nonce, nonceLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* nonce_ptr = addr_app_to_native((uintptr_t)nonce);
    return TEE_AEInit(operation, nonce_ptr, nonceLen, tagLen, AADLen, payloadLen);
}

static TEE_Result
TEE_AEEncryptFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const void* srcData, size_t srcLen,
    void* destData, size_t* destLen,
    void* tag, size_t* tagLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* srcData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    /* destLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    /* destData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    /* tagLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)tagLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* tagLen_ptr = addr_app_to_native((uintptr_t)tagLen);

    /* tag has been checked by runtime */
    if (!validate_app_addr((uintptr_t)tag, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* tag_ptr = addr_app_to_native((uintptr_t)tag);

    return TEE_AEEncryptFinal(operation, srcData_ptr, srcLen,
        destData_ptr, destLen_ptr, tag_ptr, tagLen_ptr);
}

static TEE_Result
TEE_AEDecryptFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const void* srcData, size_t srcLen,
    void* destData, size_t* destLen,
    void* tag, size_t tagLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* srcData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    /* destLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    /* destData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    /* tag has been checked by runtime */
    if (!validate_app_addr((uintptr_t)tag, tagLen))
        return TEE_ERROR_BAD_PARAMETERS;

    void* tag_ptr = addr_app_to_native((uintptr_t)tag);

    return TEE_AEDecryptFinal(operation, srcData_ptr, srcLen,
        destData_ptr, destLen_ptr, tag_ptr, tagLen);
}

static TEE_Result
TEE_GetObjectBufferAttribute_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object, uint32_t attributeID,
    void* buffer, size_t* size)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* size has been checked by runtime */
    if (!validate_app_addr((uintptr_t)size, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* size_ptr = addr_app_to_native((uintptr_t)size);

    /* buffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)buffer, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);

    return TEE_GetObjectBufferAttribute(object, attributeID, buffer_ptr, size_ptr);
}

static TEE_Result
TEE_GetObjectValueAttribute_wrapper(wasm_exec_env_t exec_env,
    TEE_ObjectHandle object, uint32_t attributeID, uint32_t* a,
    uint32_t* b)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* a has been checked by runtime */
    if (!validate_app_addr((uintptr_t)a, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* a_ptr = addr_app_to_native((uintptr_t)a);

    /* b has been checked by runtime */
    if (!validate_app_addr((uintptr_t)b, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* b_ptr = addr_app_to_native((uintptr_t)b);
    return TEE_GetObjectValueAttribute(object, attributeID, a_ptr, b_ptr);
}

static void
TEE_GenerateRandom_wrapper(wasm_exec_env_t exec_env,
    void* randomBuffer, size_t randomBufferLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* randomBuffer has been checked by runtime */
    if (!validate_app_addr((uintptr_t)randomBuffer, randomBufferLen)) {
        EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, randomBuffer);
        return;
    }

    void* randomBuffer_ptr = addr_app_to_native((uintptr_t)randomBuffer);
    TEE_GenerateRandom(randomBuffer_ptr, randomBufferLen);
}

static void
TEE_CipherInit_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* IV,
    size_t IVLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* IV has been checked by runtime */
    if (!validate_app_addr((uintptr_t)IV, IVLen)) {
        EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, IV);
        return;
    }
    const void* IV_ptr = addr_app_to_native((uintptr_t)IV);
    TEE_CipherInit(operation, IV_ptr, IVLen);
}

static TEE_Result
TEE_CipherUpdate_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* srcData,
    size_t srcLen, void* destData, size_t* destLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* srcData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    /* destLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    /* destData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);
    return TEE_CipherUpdate(operation, srcData_ptr, srcLen, destData_ptr, destLen_ptr);
}

static TEE_Result
TEE_CipherDoFinal_wrapper(wasm_exec_env_t exec_env,
    TEE_OperationHandle operation, const void* srcData,
    size_t srcLen, void* destData, size_t* destLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* srcData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    /* destLen has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    /* destData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    return TEE_CipherDoFinal(operation, srcData_ptr, srcLen, destData_ptr, destLen_ptr);
}

static void
TEE_InitValueAttribute_wrapper(wasm_exec_env_t exec_env,
    TEE_Attribute* attr, uint32_t attributeID,
    uint32_t a, uint32_t b)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* attr has been checked by runtime */
    if (!validate_app_addr((uintptr_t)attr, sizeof(TEE_Attribute))) {
        EMSG("%08x : %p\n", TEE_ERROR_BAD_PARAMETERS, attr);
        return;
    }
    TEE_Attribute* attr_ptr = addr_app_to_native((uintptr_t)attr);

    TEE_InitValueAttribute(attr_ptr, attributeID, a, b);
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
    const TEE_Attribute* params, uint32_t paramCount,
    const void* srcData, size_t srcLen,
    void* destData, size_t* destLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, paramCount))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;
    void* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    return TEE_AsymmetricEncrypt(operation,
        params_ptr, paramCount,
        srcData_ptr, srcLen,
        destData_ptr, destLen_ptr);
}

static TEE_Result TEE_AsymmetricDecrypt_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const TEE_Attribute* params, uint32_t paramCount,
    const void* srcData, size_t srcLen,
    void* destData, size_t* destLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, paramCount))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)srcData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    return TEE_AsymmetricDecrypt(operation,
        params_ptr, paramCount,
        srcData_ptr, srcLen,
        destData_ptr, destLen_ptr);
}

static TEE_Result TEE_AsymmetricSignDigest_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const TEE_Attribute* params, uint32_t paramCount,
    const void* digest, size_t digestLen,
    void* signature, size_t* signatureLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, paramCount))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)digest, digestLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* digest_ptr = addr_app_to_native((uintptr_t)digest);

    if (!validate_app_addr((uintptr_t)signature, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* signature_ptr = addr_app_to_native((uintptr_t)signature);

    if (!validate_app_addr((uintptr_t)signatureLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* signatureLen_ptr = addr_app_to_native((uintptr_t)signatureLen);

    return TEE_AsymmetricSignDigest(operation,
        params_ptr, paramCount,
        digest_ptr, digestLen,
        signature_ptr, signatureLen_ptr);
}

static TEE_Result TEE_AsymmetricVerifyDigest_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const TEE_Attribute* params, uint32_t paramCount,
    const void* digest, size_t digestLen,
    const void* signature, size_t signatureLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, paramCount))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)digest, digestLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* digest_ptr = addr_app_to_native((uintptr_t)digest);

    if (!validate_app_addr((uintptr_t)signature, signatureLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* signature_ptr = addr_app_to_native((uintptr_t)signature);

    return TEE_AsymmetricVerifyDigest(operation,
        params_ptr, paramCount,
        digest_ptr, digestLen,
        signature_ptr, signatureLen);
}

static void TEE_DeriveKey_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const TEE_Attribute* params, uint32_t paramCount,
    TEE_ObjectHandle derivedKey)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, paramCount))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_Attribute* params_ptr = addr_app_to_native((uintptr_t)params);

    TEE_DeriveKey(operation, params_ptr, paramCount, derivedKey);
}

static void TEE_AEUpdateAAD_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const void* AADdata, size_t AADdataLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    /* srcData has been checked by runtime */
    if (!validate_app_addr((uintptr_t)AADdata, AADdataLen))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* AADdata_ptr = addr_app_to_native((uintptr_t)AADdata);
    TEE_AEUpdateAAD(operation, AADdata_ptr, AADdataLen);
}

static TEE_Result TEE_AEUpdate_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    const void* srcData, size_t srcLen,
    void* destData, size_t* destLen)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)srcData, srcLen))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* srcData_ptr = addr_app_to_native((uintptr_t)srcData);

    if (!validate_app_addr((uintptr_t)destData, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* destData_ptr = addr_app_to_native((uintptr_t)destData);

    if (!validate_app_addr((uintptr_t)destLen, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    size_t* destLen_ptr = addr_app_to_native((uintptr_t)destLen);

    return TEE_AEUpdate(operation, srcData_ptr, srcLen, destData_ptr, destLen_ptr);
}

static void TEE_GetOperationInfo_wrapper(
    wasm_exec_env_t exec_env,
    TEE_OperationHandle operation,
    TEE_OperationInfo* operationInfo)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)operationInfo, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* operationInfo_ptr = addr_app_to_native((uintptr_t)operationInfo);
    return TEE_GetOperationInfo(operation, operationInfo_ptr);
}

static TEE_Result TEE_OpenTASession_wrapper(
    wasm_exec_env_t exec_env,
    const TEE_UUID* destination,
    uint32_t cancellationRequestTimeout,
    uint32_t paramTypes,
    TEE_Param params[TEE_NUM_PARAMS],
    TEE_TASessionHandle* session,
    uint32_t* returnOrigin)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)destination, sizeof(TEE_UUID)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_UUID* destination_ptr = addr_app_to_native((uintptr_t)destination);

    if (!validate_app_addr((uintptr_t)params, TEE_NUM_PARAMS * sizeof(TEE_Param)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_Param* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)session, sizeof(TEE_TASessionHandle)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_TASessionHandle* session_ptr = addr_app_to_native((uintptr_t)session);

    if (!validate_app_addr((uintptr_t)returnOrigin, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    uint32_t* returnOrigin_ptr = addr_app_to_native((uintptr_t)returnOrigin);

    return TEE_OpenTASession(destination_ptr, cancellationRequestTimeout,
        paramTypes, params_ptr, session_ptr, returnOrigin_ptr);
}

static TEE_Result TEE_InvokeTACommand_wrapper(
    wasm_exec_env_t exec_env,
    TEE_TASessionHandle session,
    uint32_t cancellationRequestTimeout,
    uint32_t commandID, uint32_t paramTypes,
    TEE_Param params[TEE_NUM_PARAMS],
    uint32_t* returnOrigin)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)params, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_Param* params_ptr = addr_app_to_native((uintptr_t)params);

    if (!validate_app_addr((uintptr_t)returnOrigin, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    uint32_t* returnOrigin_ptr = addr_app_to_native((uintptr_t)returnOrigin);

    return TEE_InvokeTACommand(session, cancellationRequestTimeout,
        commandID, paramTypes, params_ptr, returnOrigin_ptr);
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
    TEE_BigInt* bigInt, size_t len)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)bigInt, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* bigInt_ptr = addr_app_to_native((uintptr_t)bigInt);
    TEE_BigIntInit(bigInt_ptr, len);
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
    TEE_BigIntFMMContext* context, size_t len,
    const TEE_BigInt* modulus)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)context, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* context_ptr = addr_app_to_native((uintptr_t)context);

    if (!validate_app_addr((uintptr_t)modulus, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* modulus_ptr = addr_app_to_native((uintptr_t)modulus);
    TEE_BigIntInitFMMContext(context_ptr, len, modulus_ptr);
}

static size_t TEE_BigIntFMMSizeInU32_wrapper(
    wasm_exec_env_t exec_env, size_t modulusSizeInBits)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    return TEE_BigIntFMMSizeInU32(modulusSizeInBits);
}

static void TEE_BigIntInitFMM_wrapper(
    wasm_exec_env_t exec_env, TEE_BigIntFMM* bigIntFMM, size_t len)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)bigIntFMM, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* bigIntFMM_ptr = addr_app_to_native((uintptr_t)bigIntFMM);
    TEE_BigIntInitFMM(bigIntFMM_ptr, len);
}

static TEE_Result TEE_BigIntConvertFromOctetString_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const uint8_t* buffer, size_t bufferLen,
    int32_t sign)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)buffer, bufferLen))
        return TEE_ERROR_BAD_PARAMETERS;

    void* buffer_ptr = addr_app_to_native((uintptr_t)buffer);
    return TEE_BigIntConvertFromOctetString(dest_ptr, buffer_ptr, bufferLen, sign);
}

static void TEE_BigIntConvertFromS32_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest, int32_t shortVal)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* dest_ptr = addr_app_to_native((uintptr_t)dest);
    TEE_BigIntConvertFromS32(dest_ptr, shortVal);
}

static int32_t TEE_BigIntCmpS32_wrapper(
    wasm_exec_env_t exec_env, const TEE_BigInt* op, int32_t shortVal)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    void* op_ptr = addr_app_to_native((uintptr_t)op);
    return TEE_BigIntCmpS32(op_ptr, shortVal);
}

static TEE_Result TEE_BigIntConvertToOctetString_wrapper(
    wasm_exec_env_t exec_env,
    uint8_t* buffer, size_t* bufferLen, const TEE_BigInt* bigInt)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)buffer, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    uint8_t* buffer_ptr = addr_app_to_native((uintptr_t)buffer);

    if (!validate_app_addr((uintptr_t)bigInt, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* bigInt_ptr = addr_app_to_native((uintptr_t)bigInt);
    return TEE_BigIntConvertToOctetString(buffer_ptr, bufferLen, bigInt_ptr);
}

static TEE_Result TEE_BigIntConvertToS32_wrapper(
    wasm_exec_env_t exec_env, int32_t* dest, const TEE_BigInt* src)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    int32_t* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);
    return TEE_BigIntConvertToS32(dest_ptr, src_ptr);
}

static bool TEE_BigIntGetBit_wrapper(
    wasm_exec_env_t exec_env, const TEE_BigInt* src, uint32_t bitIndex)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);
    return TEE_BigIntGetBit(src_ptr, bitIndex);
}

static uint32_t TEE_BigIntGetBitCount_wrapper(
    wasm_exec_env_t exec_env, const TEE_BigInt* src)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);
    return TEE_BigIntGetBitCount(src_ptr);
}

static TEE_Result TEE_BigIntSetBit_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* op,
    uint32_t bitIndex, bool value)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);
    return TEE_BigIntSetBit(op_ptr, bitIndex, value);
}

static void TEE_BigIntShiftRight_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op, size_t bits)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);
    TEE_BigIntShiftRight(dest_ptr, op_ptr, bits);
}

static int32_t TEE_BigIntCmp_wrapper(
    wasm_exec_env_t exec_env,
    const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    return TEE_BigIntCmp(op1_ptr, op2_ptr);
}

static void TEE_BigIntAdd_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    TEE_BigIntAdd(dest_ptr, op1_ptr, op2_ptr);
}

static void TEE_BigIntSub_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    TEE_BigIntSub(dest_ptr, op1_ptr, op2_ptr);
}

static void TEE_BigIntMul_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    TEE_BigIntMul(dest_ptr, op1_ptr, op2_ptr);
}

static void TEE_BigIntNeg_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest, const TEE_BigInt* op)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);

    return TEE_BigIntNeg(dest_ptr, op_ptr);
}

static TEE_Result TEE_BigIntAssign_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const TEE_BigInt* src)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);
    return TEE_BigIntAssign(dest_ptr, src_ptr);
}

static TEE_Result TEE_BigIntAbs_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest, const TEE_BigInt* src)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);

    return TEE_BigIntAbs(dest_ptr, src_ptr);
}

static void TEE_BigIntSquare_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest, const TEE_BigInt* op)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);

    TEE_BigIntSquare(dest_ptr, op_ptr);
}

static void TEE_BigIntDiv_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest_q, TEE_BigInt* dest_r,
    const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest_q, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_q_ptr = addr_app_to_native((uintptr_t)dest_q);
    if (!validate_app_addr((uintptr_t)dest_r, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_r_ptr = addr_app_to_native((uintptr_t)dest_r);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    TEE_BigIntDiv(dest_q_ptr, dest_r_ptr, op1_ptr, op2_ptr);
}

static void TEE_BigIntMod_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);

    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);

    TEE_BigIntMod(dest_ptr, op_ptr, n_ptr);
}

static void TEE_BigIntAddMod_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    TEE_BigIntAddMod(dest_ptr, op1_ptr, op2_ptr, n_ptr);
}

static void TEE_BigIntSubMod_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    TEE_BigIntSubMod(dest_ptr, op1_ptr, op2_ptr, n_ptr);
}

static void TEE_BigIntMulMod_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    TEE_BigIntMulMod(dest_ptr, op1_ptr, op2_ptr, n_ptr);
}

static void TEE_BigIntSquareMod_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const TEE_BigInt* op, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    TEE_BigIntSquareMod(dest_ptr, op_ptr, n_ptr);
}

static void TEE_BigIntInvMod_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const TEE_BigInt* op, const TEE_BigInt* n)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* op_ptr = addr_app_to_native((uintptr_t)op);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    TEE_BigIntInvMod(dest_ptr, op_ptr, n_ptr);
}

static TEE_Result TEE_BigIntExpMod_wrapper(
    wasm_exec_env_t exec_env, TEE_BigInt* dest,
    const TEE_BigInt* op1, const TEE_BigInt* op2,
    const TEE_BigInt* n, const TEE_BigIntFMMContext* context)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_BigInt* dest_ptr = addr_app_to_native((uintptr_t)dest);
    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);
    if (!validate_app_addr((uintptr_t)context, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigIntFMMContext* context_ptr = addr_app_to_native((uintptr_t)context);
    return TEE_BigIntExpMod(dest_ptr, op1_ptr, op2_ptr, n_ptr, context_ptr);
}

static bool TEE_BigIntRelativePrime_wrapper(
    wasm_exec_env_t exec_env, const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op1_ptr = addr_app_to_native((uintptr_t)op1);
    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const TEE_BigInt* op2_ptr = addr_app_to_native((uintptr_t)op2);
    return TEE_BigIntRelativePrime(op1_ptr, op2_ptr);
}

static void TEE_BigIntComputeExtendedGcd_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* gcd, TEE_BigInt* u, TEE_BigInt* v,
    const TEE_BigInt* op1, const TEE_BigInt* op2)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)gcd, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* gcd_ptr = addr_app_to_native((uintptr_t)gcd);

    if (!validate_app_addr((uintptr_t)u, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* u_ptr = addr_app_to_native((uintptr_t)u);

    if (!validate_app_addr((uintptr_t)v, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* v_ptr = addr_app_to_native((uintptr_t)v);

    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* op1_ptr = addr_app_to_native((uintptr_t)op1);

    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* op2_ptr = addr_app_to_native((uintptr_t)op2);

    TEE_BigIntComputeExtendedGcd(gcd_ptr, u_ptr, v_ptr, op1_ptr, op2_ptr);
}

static int32_t TEE_BigIntIsProbablePrime_wrapper(
    wasm_exec_env_t exec_env, const TEE_BigInt* op, uint32_t confidenceLevel)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)op, sizeof(uintptr_t)))
        return TEE_ERROR_BAD_PARAMETERS;

    const void* op_ptr = addr_app_to_native((uintptr_t)op);
    return TEE_BigIntIsProbablePrime(op_ptr, confidenceLevel);
}

static void TEE_BigIntConvertToFMM_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigIntFMM* dest, const TEE_BigInt* src,
    const TEE_BigInt* n, const TEE_BigIntFMMContext* context)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    TEE_BigIntFMM* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* src_ptr = addr_app_to_native((uintptr_t)src);

    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigInt* n_ptr = addr_app_to_native((uintptr_t)n);

    if (!validate_app_addr((uintptr_t)context, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const TEE_BigIntFMMContext* context_ptr = addr_app_to_native((uintptr_t)context);

    TEE_BigIntConvertToFMM(dest_ptr, src_ptr, n_ptr, context_ptr);
}

static void TEE_BigIntConvertFromFMM_wrapper(
    wasm_exec_env_t exec_env,
    TEE_BigInt* dest, const TEE_BigIntFMM* src,
    const TEE_BigInt* n, const TEE_BigIntFMMContext* context)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)src, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* src_ptr = addr_app_to_native((uintptr_t)src);

    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* n_ptr = addr_app_to_native((uintptr_t)n);

    if (!validate_app_addr((uintptr_t)context, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* context_ptr = addr_app_to_native((uintptr_t)context);

    TEE_BigIntConvertFromFMM(dest_ptr, src_ptr, n_ptr, context_ptr);
}

static void TEE_BigIntComputeFMM_wrapper(
    wasm_exec_env_t exec_env, TEE_BigIntFMM* dest,
    const TEE_BigIntFMM* op1, const TEE_BigIntFMM* op2,
    const TEE_BigInt* n, const TEE_BigIntFMMContext* context)
{
    DMSG("wasm.libtee.%s\n", __func__);
    wasm_module_inst_t module_inst __unused = get_module_inst(exec_env);

    if (!validate_app_addr((uintptr_t)dest, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    void* dest_ptr = addr_app_to_native((uintptr_t)dest);

    if (!validate_app_addr((uintptr_t)op1, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* op1_ptr = addr_app_to_native((uintptr_t)op1);

    if (!validate_app_addr((uintptr_t)op2, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* op2_ptr = addr_app_to_native((uintptr_t)op2);

    if (!validate_app_addr((uintptr_t)n, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* n_ptr = addr_app_to_native((uintptr_t)n);

    if (!validate_app_addr((uintptr_t)context, sizeof(uintptr_t)))
        TEE_Panic(TEE_ERROR_BAD_PARAMETERS);

    const void* context_ptr = addr_app_to_native((uintptr_t)context);

    TEE_BigIntComputeFMM(dest_ptr, op1_ptr, op2_ptr, n_ptr, context_ptr);
}

#define REG_NATIVE_FUNC(func_name, signature)            \
    {                                                    \
#func_name, func_name##_wrapper, signature, NULL \
    }

#ifdef CONFIG_INTERPRETERS_WAMR_MEMORY64
static NativeSymbol native_symbols_libtee_builtin[] = {
    REG_NATIVE_FUNC(TEE_Malloc, "(Ii)I"),
    REG_NATIVE_FUNC(TEE_Realloc, "(II)I"),
    REG_NATIVE_FUNC(TEE_Free, "(I)"),
    REG_NATIVE_FUNC(TEE_MemMove, "(III)I"),
    REG_NATIVE_FUNC(TEE_MemCompare, "(III)i"),
    REG_NATIVE_FUNC(TEE_MemFill, "(IiI)"),
    REG_NATIVE_FUNC(TEE_GetObjectInfo1, "(II)i"),
    REG_NATIVE_FUNC(TEE_CloseObject, "(I)"),
    REG_NATIVE_FUNC(TEE_OpenPersistentObject, "(iIIiI)i"),
    REG_NATIVE_FUNC(TEE_CreatePersistentObject, "(iIIiIIII)i"),
    REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject1, "(I)i"),
    REG_NATIVE_FUNC(TEE_RenamePersistentObject, "(III)i"),
    REG_NATIVE_FUNC(TEE_ReadObjectData, "(IIII)i"),
    REG_NATIVE_FUNC(TEE_WriteObjectData, "(III)i"),
    REG_NATIVE_FUNC(TEE_TruncateObjectData, "(II)i"),
    REG_NATIVE_FUNC(TEE_SeekObjectData, "(IIi)i"),
    REG_NATIVE_FUNC(find_hash, "(I)i"),
    REG_NATIVE_FUNC(hmac_memory, "(iIIIIII)i"),
    REG_NATIVE_FUNC(trace_printf, "(IiiiII)"),

    REG_NATIVE_FUNC(TEE_AllocateTransientObject, "(iiI)i"),
    REG_NATIVE_FUNC(TEE_FreeTransientObject, "(I)"),
    REG_NATIVE_FUNC(TEE_InitRefAttribute, "(IiII)"),
    REG_NATIVE_FUNC(TEE_PopulateTransientObject, "(IIi)i"),
    REG_NATIVE_FUNC(TEE_AllocateOperation, "(Iiii)i"),
    REG_NATIVE_FUNC(TEE_FreeOperation, "(I)"),
    REG_NATIVE_FUNC(TEE_SetOperationKey, "(II)i"),
    REG_NATIVE_FUNC(TEE_CopyOperation, "(II)"),
    REG_NATIVE_FUNC(TEE_MACInit, "(III)"),
    REG_NATIVE_FUNC(TEE_MACUpdate, "(III)"),
    REG_NATIVE_FUNC(TEE_MACComputeFinal, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_MACCompareFinal, "(IIII)i"),

    REG_NATIVE_FUNC(TEE_DigestUpdate, "(III)"),
    REG_NATIVE_FUNC(TEE_DigestDoFinal, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_DigestExtract, "(III)i"),
    REG_NATIVE_FUNC(TEE_ResetOperation, "(I)"),
    REG_NATIVE_FUNC(TEE_IsAlgorithmSupported, "(ii)i"),

    REG_NATIVE_FUNC(sleep, "(i)i"),
    REG_NATIVE_FUNC(TEE_GetSystemTime, "(I)"),
    REG_NATIVE_FUNC(TEE_GenerateKey, "(IiIi)i"),
    REG_NATIVE_FUNC(TEE_AEInit, "(IIIiII)i"),
    REG_NATIVE_FUNC(TEE_AEEncryptFinal, "(IIIIIII)i"),
    REG_NATIVE_FUNC(TEE_AEDecryptFinal, "(IIIIIII)i"),
    REG_NATIVE_FUNC(TEE_GetObjectBufferAttribute, "(IiII)i"),
    REG_NATIVE_FUNC(TEE_GetObjectValueAttribute, "(IiII)i"),
    REG_NATIVE_FUNC(TEE_GenerateRandom, "(II)"),
    REG_NATIVE_FUNC(TEE_CipherInit, "(III)"),
    REG_NATIVE_FUNC(TEE_CipherUpdate, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_CipherDoFinal, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_InitValueAttribute, "(Iiii)"),
    REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject, "(I)"),

    REG_NATIVE_FUNC(TEE_AllocatePersistentObjectEnumerator, "(I)i"),
    REG_NATIVE_FUNC(TEE_FreePersistentObjectEnumerator, "(I)"),
    REG_NATIVE_FUNC(TEE_ResetPersistentObjectEnumerator, "(I)"),
    REG_NATIVE_FUNC(TEE_StartPersistentObjectEnumerator, "(Ii)i"),
    REG_NATIVE_FUNC(TEE_GetNextPersistentObject, "(IIII)i"),
    REG_NATIVE_FUNC(TEE_GetObjectInfo, "(II)"),
    REG_NATIVE_FUNC(TEE_RestrictObjectUsage1, "(Ii)i"),
    REG_NATIVE_FUNC(TEE_ResetTransientObject, "(I)"),
    REG_NATIVE_FUNC(TEE_Panic, "(i)"),
    REG_NATIVE_FUNC(TEE_SetOperationKey2, "(III)i"),
    REG_NATIVE_FUNC(TEE_CopyObjectAttributes1, "(II)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricEncrypt, "(IIiIIII)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricDecrypt, "(IIiIIII)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricSignDigest, "(IIiIIII)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricVerifyDigest, "(IIiIIII)i"),
    REG_NATIVE_FUNC(TEE_DeriveKey, "(IIiI)"),
    REG_NATIVE_FUNC(TEE_AEUpdateAAD, "(III)"),
    REG_NATIVE_FUNC(TEE_AEUpdate, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_GetOperationInfo, "(II)"),
    REG_NATIVE_FUNC(TEE_OpenTASession, "(IiiIII)i"),
    REG_NATIVE_FUNC(TEE_InvokeTACommand, "(IiiiII)i"),
    REG_NATIVE_FUNC(TEE_CloseTASession, "(I)"),
    REG_NATIVE_FUNC(TEE_BigIntInit, "(II)"),
    REG_NATIVE_FUNC(TEE_BigIntFMMContextSizeInU32, "(I)i"),
    REG_NATIVE_FUNC(TEE_BigIntInitFMMContext, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntFMMSizeInU32, "(I)i"),
    REG_NATIVE_FUNC(TEE_BigIntInitFMM, "(II)"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromOctetString, "(IIIi)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromS32, "(Ii)"),
    REG_NATIVE_FUNC(TEE_BigIntCmpS32, "(Ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToOctetString, "(III)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToS32, "(II)i"),
    REG_NATIVE_FUNC(TEE_BigIntGetBit, "(Ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntSetBit, "(Iii)i"),
    REG_NATIVE_FUNC(TEE_BigIntShiftRight, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntCmp, "(II)i"),
    REG_NATIVE_FUNC(TEE_BigIntAdd, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntSub, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntMul, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntNeg, "(II)"),
    REG_NATIVE_FUNC(TEE_BigIntAssign, "(II)i"),
    REG_NATIVE_FUNC(TEE_BigIntAbs, "(II)i"),
    REG_NATIVE_FUNC(TEE_BigIntSquare, "(II)"),
    REG_NATIVE_FUNC(TEE_BigIntDiv, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntMod, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntAddMod, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntSubMod, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntMulMod, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntSquareMod, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntInvMod, "(III)"),
    REG_NATIVE_FUNC(TEE_BigIntExpMod, "(IIIII)i"),
    REG_NATIVE_FUNC(TEE_BigIntRelativePrime, "(II)i"),
    REG_NATIVE_FUNC(TEE_BigIntComputeExtendedGcd, "(IIIII)"),
    REG_NATIVE_FUNC(TEE_BigIntIsProbablePrime, "(Ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToFMM, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromFMM, "(IIII)"),
    REG_NATIVE_FUNC(TEE_BigIntComputeFMM, "(IIIII)"),
    REG_NATIVE_FUNC(TEE_BigIntGetBitCount, "(I)i"),
};
#else
static NativeSymbol native_symbols_libtee_builtin[] = {
    REG_NATIVE_FUNC(TEE_Malloc, "(ii)i"),
    REG_NATIVE_FUNC(TEE_Realloc, "(ii)i"),
    REG_NATIVE_FUNC(TEE_Free, "(i)"),
    REG_NATIVE_FUNC(TEE_MemMove, "(iii)i"),
    REG_NATIVE_FUNC(TEE_MemCompare, "(iii)i"),
    REG_NATIVE_FUNC(TEE_MemFill, "(iii)"),
    REG_NATIVE_FUNC(TEE_GetObjectInfo1, "(ii)i"),
    REG_NATIVE_FUNC(TEE_CloseObject, "(i)"),
    REG_NATIVE_FUNC(TEE_OpenPersistentObject, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_CreatePersistentObject, "(iiiiiiii)i"),
    REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject1, "(i)i"),
    REG_NATIVE_FUNC(TEE_RenamePersistentObject, "(iii)i"),
    REG_NATIVE_FUNC(TEE_ReadObjectData, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_WriteObjectData, "(iii)i"),
    REG_NATIVE_FUNC(TEE_TruncateObjectData, "(ii)i"),
    REG_NATIVE_FUNC(TEE_SeekObjectData, "(iIi)i"),
    REG_NATIVE_FUNC(find_hash, "(i)i"),
    REG_NATIVE_FUNC(hmac_memory, "(iiiiiii)i"),
    REG_NATIVE_FUNC(trace_printf, "(iiiiii)"),

    REG_NATIVE_FUNC(TEE_AllocateTransientObject, "(iii)i"),
    REG_NATIVE_FUNC(TEE_FreeTransientObject, "(i)"),
    REG_NATIVE_FUNC(TEE_InitRefAttribute, "(iiii)"),
    REG_NATIVE_FUNC(TEE_PopulateTransientObject, "(iii)i"),
    REG_NATIVE_FUNC(TEE_AllocateOperation, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_FreeOperation, "(i)"),
    REG_NATIVE_FUNC(TEE_SetOperationKey, "(ii)i"),
    REG_NATIVE_FUNC(TEE_CopyOperation, "(ii)"),
    REG_NATIVE_FUNC(TEE_MACInit, "(iii)"),
    REG_NATIVE_FUNC(TEE_MACUpdate, "(iii)"),
    REG_NATIVE_FUNC(TEE_MACComputeFinal, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_MACCompareFinal, "(iiiii)i"),

    REG_NATIVE_FUNC(TEE_DigestUpdate, "(iii)"),
    REG_NATIVE_FUNC(TEE_DigestDoFinal, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_DigestExtract, "(iii)i"),
    REG_NATIVE_FUNC(TEE_ResetOperation, "(i)"),
    REG_NATIVE_FUNC(TEE_IsAlgorithmSupported, "(ii)i"),

    REG_NATIVE_FUNC(sleep, "(i)i"),
    REG_NATIVE_FUNC(TEE_GetSystemTime, "(i)"),
    REG_NATIVE_FUNC(TEE_GenerateKey, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_AEInit, "(iiiiii)i"),
    REG_NATIVE_FUNC(TEE_AEEncryptFinal, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_AEDecryptFinal, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_GetObjectBufferAttribute, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_GetObjectValueAttribute, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_GenerateRandom, "(ii)"),
    REG_NATIVE_FUNC(TEE_CipherInit, "(iii)"),
    REG_NATIVE_FUNC(TEE_CipherUpdate, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_CipherDoFinal, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_InitValueAttribute, "(iiii)"),
    REG_NATIVE_FUNC(TEE_CloseAndDeletePersistentObject, "(i)"),

    REG_NATIVE_FUNC(TEE_AllocatePersistentObjectEnumerator, "(i)i"),
    REG_NATIVE_FUNC(TEE_FreePersistentObjectEnumerator, "(i)"),
    REG_NATIVE_FUNC(TEE_ResetPersistentObjectEnumerator, "(i)"),
    REG_NATIVE_FUNC(TEE_StartPersistentObjectEnumerator, "(ii)i"),
    REG_NATIVE_FUNC(TEE_GetNextPersistentObject, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_GetObjectInfo, "(ii)"),
    REG_NATIVE_FUNC(TEE_RestrictObjectUsage1, "(ii)i"),
    REG_NATIVE_FUNC(TEE_ResetTransientObject, "(i)"),
    REG_NATIVE_FUNC(TEE_Panic, "(i)"),
    REG_NATIVE_FUNC(TEE_SetOperationKey2, "(iii)i"),
    REG_NATIVE_FUNC(TEE_CopyObjectAttributes1, "(ii)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricEncrypt, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricDecrypt, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricSignDigest, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_AsymmetricVerifyDigest, "(iiiiiii)i"),
    REG_NATIVE_FUNC(TEE_DeriveKey, "(iiii)"),
    REG_NATIVE_FUNC(TEE_AEUpdateAAD, "(iii)"),
    REG_NATIVE_FUNC(TEE_AEUpdate, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_GetOperationInfo, "(ii)"),
    REG_NATIVE_FUNC(TEE_OpenTASession, "(*iiiii)i"),
    REG_NATIVE_FUNC(TEE_InvokeTACommand, "(iiiiii)i"),
    REG_NATIVE_FUNC(TEE_CloseTASession, "(i)"),
    REG_NATIVE_FUNC(TEE_BigIntInit, "(ii)"),
    REG_NATIVE_FUNC(TEE_BigIntFMMContextSizeInU32, "(i)i"),
    REG_NATIVE_FUNC(TEE_BigIntInitFMMContext, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntFMMSizeInU32, "(i)i"),
    REG_NATIVE_FUNC(TEE_BigIntInitFMM, "(ii)"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromOctetString, "(iiii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromS32, "(ii)"),
    REG_NATIVE_FUNC(TEE_BigIntCmpS32, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToOctetString, "(iii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToS32, "(**)i"),
    REG_NATIVE_FUNC(TEE_BigIntGetBit, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntSetBit, "(iii)i"),
    REG_NATIVE_FUNC(TEE_BigIntShiftRight, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntCmp, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntAdd, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntSub, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntMul, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntNeg, "(ii)"),
    REG_NATIVE_FUNC(TEE_BigIntAssign, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntAbs, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntSquare, "(ii)"),
    REG_NATIVE_FUNC(TEE_BigIntDiv, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntMod, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntAddMod, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntSubMod, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntMulMod, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntSquareMod, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntInvMod, "(iii)"),
    REG_NATIVE_FUNC(TEE_BigIntExpMod, "(iiiii)i"),
    REG_NATIVE_FUNC(TEE_BigIntRelativePrime, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntComputeExtendedGcd, "(iiiii)"),
    REG_NATIVE_FUNC(TEE_BigIntIsProbablePrime, "(ii)i"),
    REG_NATIVE_FUNC(TEE_BigIntConvertToFMM, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntConvertFromFMM, "(iiii)"),
    REG_NATIVE_FUNC(TEE_BigIntComputeFMM, "(iiiii)"),
    REG_NATIVE_FUNC(TEE_BigIntGetBitCount, "(i)i"),
};
#endif

uint32_t
get_libtee_builtin_export_apis(NativeSymbol** p_libtee_builtin_apis)
{
    *p_libtee_builtin_apis = native_symbols_libtee_builtin;
    return sizeof(native_symbols_libtee_builtin) / sizeof(NativeSymbol);
}
