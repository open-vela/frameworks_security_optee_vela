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

#include <assert.h>
#include <compiler.h>
#include <kernel/tee_ta_manager.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <tee/tee_obj.h>
#include <tee/tee_svc.h>
#include <tee/tee_svc_cryp.h>
#include <tee/uuid.h>
#include <trace.h>
#include <types_ext.h>
#include <utee_defines.h>
#include <util.h>

#include "wasm_export.h"
#include <fcntl.h>
#include <kernel/tee_misc.h>
#include <kernel/user_ta.h>
#include <mm/mobj.h>
#include <sys/mman.h>
#include <sys/stat.h>

#ifdef CONFIG_INTERPRETERS_WAMR_MEMORY64
#define SET_WASM_VAL_T_VALUE(a, n, b)            \
    do {                                         \
        (a)[n].of.i64 = (int64_t)(uintptr_t)(b); \
        (a)[n].kind = WASM_I64;                  \
    } while (0)

#define WASM_RUNTIME_MODULE_MALLOC(utc, size, buffer) \
    (uint64_t) wasm_runtime_module_malloc((utc)->wasm_module_inst, (size), (buffer))
#else
#define SET_WASM_VAL_T_VALUE(a, n, b)            \
    do {                                         \
        (a)[n].of.i32 = (int32_t)(uintptr_t)(b); \
        (a)[n].kind = WASM_I32;                  \
    } while (0)

#define WASM_RUNTIME_MODULE_MALLOC(utc, size, buffer) \
    (uint32_t) wasm_runtime_module_malloc((utc)->wasm_module_inst, (size), (buffer))
#endif

static uint8_t wasm_runtime_init_flag = 0;

static void wasm_free_app_params(struct user_ta_ctx* utc, wasm_val_t* p)
{
    for (int n = 0; n < 4; n++) {
        /* it is runtime embedder's responsibility to release the memory,
         * unless the WASM app will free the passed pointer in its code
         */
        if (p[n].of.i64) {
            wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i64);
            p[n].of.i64 = 0;
        }

        if (p[n].of.i32) {
            wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i32);
            p[n].of.i32 = 0;
        }
    }
}

static TEE_Result wasm_copy_in_app_params(struct user_ta_ctx* utc,
    uint32_t param_types,
    wasm_val_t* p, wasm_val_t* p_cookie,
    struct tee_ta_param* param)
{
    TEE_Result res = TEE_ERROR_OUT_OF_MEMORY;
    uint32_t type;
    void* buffer = NULL;
    size_t buffer_for_wasm;

    memset(p, 0, 4 * sizeof(wasm_val_t));
    memset(p_cookie, 0, 4 * sizeof(wasm_val_t));

    for (int n = 0; n < 4; n++) {
        type = TEE_PARAM_TYPE_GET(param_types, n);
        switch (type) {
        case TEE_PARAM_TYPE_NONE:
            SET_WASM_VAL_T_VALUE(p, n, 0);
            break;
        case TEE_PARAM_TYPE_MEMREF_INPUT:
        case TEE_PARAM_TYPE_MEMREF_OUTPUT:
        case TEE_PARAM_TYPE_MEMREF_INOUT:
            if (!param || !param->u[n].mem.mobj) {
                EMSG("param error!!!");
                continue;
            }
            /*
                param[] case 1 as follows:
                struct mobj {
                    size_t size;
                    void *buffer;
                };
                struct param_mem {
                    struct mobj *mobj;
                    size_t size;
                    size_t offs;
                };
            */
            buffer_for_wasm = WASM_RUNTIME_MODULE_MALLOC(utc,
                param->u[n].mem.mobj->size + sizeof(size_t),
                &buffer);
            if (buffer_for_wasm != 0) {
                SET_WASM_VAL_T_VALUE(p, n, buffer_for_wasm);
                SET_WASM_VAL_T_VALUE(p_cookie, n, buffer);

                memcpy(buffer, &param->u[n].mem.mobj->size, sizeof(size_t));
                memcpy(buffer + sizeof(size_t),
                    param->u[n].mem.mobj->buffer,
                    param->u[n].mem.mobj->size);

            } else {
                EMSG("TEE out of memory: %zu\n", param->u[n].mem.mobj->size + sizeof(size_t));
                res = TEE_ERROR_OUT_OF_MEMORY;
                goto out;
            }
            break;
        case TEE_PARAM_TYPE_VALUE_INPUT:
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
        case TEE_PARAM_TYPE_VALUE_INOUT:
            if (!param) {
                EMSG("param error!!!");
                continue;
            }
            /*
                param[] case 2 as follows:
                struct param_val {
                    uint32_t a;
                    uint32_t b;
                };
            */
            buffer_for_wasm = WASM_RUNTIME_MODULE_MALLOC(utc,
                sizeof(uint32_t) * 2,
                &buffer);
            if (buffer_for_wasm != 0) {
                SET_WASM_VAL_T_VALUE(p, n, buffer_for_wasm);
                SET_WASM_VAL_T_VALUE(p_cookie, n, buffer);
                memcpy(buffer, &param->u[n].val.a, sizeof(uint32_t));
                memcpy(buffer + sizeof(uint32_t), &param->u[n].val.b, sizeof(uint32_t));
            } else {
                EMSG("%08x : %zu\n", TEE_ERROR_OUT_OF_MEMORY, sizeof(uint32_t) * 2);
                res = TEE_ERROR_OUT_OF_MEMORY;
                goto out;
            }
            break;
        default:
            EMSG("%08x : 0x%" PRIx32 "\n", TEE_ERROR_ITEM_NOT_FOUND, type);
            res = TEE_ERROR_ITEM_NOT_FOUND;
            goto out;
        }
    }

    return TEE_SUCCESS;
out:
    wasm_free_app_params(utc, p);

    return res;
}

static TEE_Result wasm_copy_out_app_params(struct user_ta_ctx* utc,
    uint32_t param_types,
    wasm_val_t* p, wasm_val_t* p_cookie,
    struct tee_ta_param* param)
{

    TEE_Result res = TEE_ERROR_GENERIC;
    uint32_t type;

    for (int n = 0; n < 4; n++) {
        type = TEE_PARAM_TYPE_GET(param_types, n);
        switch (type) {
        case TEE_PARAM_TYPE_NONE:
            break;
        case TEE_PARAM_TYPE_MEMREF_INPUT:
        case TEE_PARAM_TYPE_MEMREF_OUTPUT:
        case TEE_PARAM_TYPE_MEMREF_INOUT:
            if (!param || !param->u[n].mem.mobj) {
                EMSG("param error!!!");
                continue;
            }
            /*
                p_cookie[] format case 1 as follows:
                struct {
                    void *buffer;
                    size_t size;
                } memref;
            */
#ifdef CONFIG_INTERPRETERS_WAMR_MEMORY64
            if (p_cookie[n].kind == WASM_I64 && p_cookie[n].of.i64 != 0) {
                memcpy(&param->u[n].mem.mobj->size, (const void*)(uintptr_t)p_cookie[n].of.i64,
                    sizeof(size_t));
                param->u[n].mem.size = param->u[n].mem.mobj->size;
                memcpy(param->u[n].mem.mobj->buffer,
                    (const void*)(uintptr_t)(p_cookie[n].of.i64 + sizeof(size_t)),
                    param->u[n].mem.mobj->size);
                wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i64);
                p[n].of.i64 = 0;
            }
#else
            if (p_cookie[n].kind == WASM_I32 && p_cookie[n].of.i32 != 0) {
                memcpy(&param->u[n].mem.mobj->size, (const void*)(uintptr_t)p_cookie[n].of.i32,
                    sizeof(size_t));
                param->u[n].mem.size = param->u[n].mem.mobj->size;
                memcpy(param->u[n].mem.mobj->buffer,
                    (const void*)(uintptr_t)(p_cookie[n].of.i32 + sizeof(size_t)),
                    param->u[n].mem.mobj->size);
                wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i32);
                p[n].of.i32 = 0;
            }
#endif
            else {
                EMSG("%08x\n", TEE_ERROR_BAD_PARAMETERS);
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            break;
        case TEE_PARAM_TYPE_VALUE_INPUT:
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
        case TEE_PARAM_TYPE_VALUE_INOUT:
            if (!param) {
                EMSG("param error!!!");
                continue;
            }
            /*
                p_cookie[] format case 2 as follows:
                struct {
                    uint32_t a;
                    uint32_t b;
                } value;
            */
#ifdef CONFIG_INTERPRETERS_WAMR_MEMORY64
            if (p_cookie[n].kind == WASM_I64 && p_cookie[n].of.i64 != 0) {
                memcpy(&param->u[n].val.a, (const void*)(uintptr_t)p_cookie[n].of.i64, sizeof(uint32_t));
                memcpy(&param->u[n].val.b,
                    (const void*)(uintptr_t)(p_cookie[n].of.i64 + sizeof(uint32_t)),
                    sizeof(uint32_t));
                wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i64);
                p[n].of.i64 = 0;
            }
#else
            if (p_cookie[n].kind == WASM_I32 && p_cookie[n].of.i32 != 0) {
                memcpy(&param->u[n].val.a, (const void*)(uintptr_t)p_cookie[n].of.i32, sizeof(uint32_t));
                memcpy(&param->u[n].val.b,
                    (const void*)(uintptr_t)(p_cookie[n].of.i32 + sizeof(uint32_t)),
                    sizeof(uint32_t));
                wasm_runtime_module_free(utc->wasm_module_inst, (uint64_t)p[n].of.i32);
                p[n].of.i32 = 0;
            }
#endif
            else {
                EMSG("%08x\n", TEE_ERROR_BAD_PARAMETERS);
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            break;
        default:
            EMSG("%08x : 0x%" PRIx32 "\n", TEE_ERROR_ITEM_NOT_FOUND, type);
            res = TEE_ERROR_ITEM_NOT_FOUND;
            goto out;
        }
    }

    return TEE_SUCCESS;
out:
    wasm_free_app_params(utc, p);

    return res;
}

static TEE_Result user_ta_wasm_enter_open_session(struct ts_session* s)
{
    TEE_Result res = TEE_ERROR_GENERIC;
    wasm_val_t ta_arguments[6] = { 0 };
    wasm_val_t p_cookie[4] = { 0 };
    wasm_val_t results[1] = { 0 };
    void* buffer = NULL;
    struct tee_ta_session* ta_sess = to_ta_session(s);
    struct ts_session* ts_sess __maybe_unused = NULL;
    size_t buffer_for_wasm = 0;

    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    ts_push_current_session(s);
    DMSG("context.ref_count: %" PRIu32 "\n", utc->ta_ctx.ref_count);

    /* call create entry point if first open session, or when the session is re-opened
     * but the utc->func is not inited, we need to perform the init action
     */
    if (utc->ta_ctx.ref_count >= 1 || !utc->func) {
        /* lookup a WASM function by its name. */
        utc->func = wasm_runtime_lookup_function(utc->wasm_module_inst,
            "wasm_TA_CreateEntryPoint");
        if (!utc->func) {
            EMSG("%08x\n", TEE_ERROR_GENERIC);
            res = TEE_ERROR_GENERIC;
            goto out;
        }
    }

    results[0].kind = WASM_I32;
    results[0].of.i32 = TEE_ERROR_GENERIC;
    /* TEE_Result TA_EXPORT TA_CreateEntryPoint( void ) */
    if (wasm_runtime_call_wasm_a(utc->exec_env, utc->func, 1, results, 1, ta_arguments)) {
        DMSG("call wasm_TA_CreateEntryPoint ret: 0x%" PRIx32 "\n", results[0].of.i32);
        if (results[0].of.i32 != TEE_SUCCESS) {
            res = results[0].of.i32;
            goto out;
        }
    } else {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_runtime_get_exception(utc->wasm_module_inst));
        res = TEE_ERROR_GENERIC;
        goto out;
    }

    /* lookup a WASM function by its name. */
    utc->func = wasm_runtime_lookup_function(utc->wasm_module_inst,
        "wasm_TA_OpenSessionEntryPoint");
    if (!utc->func) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
        res = TEE_ERROR_GENERIC;
        goto out;
    }

    /* TEE_Result TA_EXPORT TA_OpenSessionEntryPoint(
     *				uint32_t paramTypes,
     *				[inout] TEE_Param params[4],
     *				[out][ctx] void** sessionContext )
     */

    ta_arguments[0].kind = WASM_I32;
    ta_arguments[0].of.i32 = ta_sess->param->types;
    res = wasm_copy_in_app_params(utc, ta_sess->param->types, &ta_arguments[1], p_cookie, ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("%08x : 0x%" PRIx32 "\n", TEE_ERROR_GENERIC, res);
        goto out;
    }
    buffer_for_wasm = WASM_RUNTIME_MODULE_MALLOC(utc,
        sizeof(size_t), &buffer);
    if (buffer_for_wasm != 0) {
        SET_WASM_VAL_T_VALUE(ta_arguments, 5, buffer_for_wasm);
    } else {
        EMSG("%08x : %zu\n", TEE_ERROR_OUT_OF_MEMORY, sizeof(size_t));
        wasm_free_app_params(utc, &ta_arguments[1]);
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto out;
    }

    /*TEE_Result TA_EXPORT TA_OpenSessionEntryPoint(
     * 				uint32_t paramTypes,
     *				[inout] TEE_Param params[4],
     *				[out][ctx] void** sessionContext );
     */
    results[0].kind = WASM_I32;
    results[0].of.i32 = TEE_ERROR_GENERIC;
    if (wasm_runtime_call_wasm_a(utc->exec_env, utc->func, 1, results, 6, ta_arguments)) {
        DMSG("call wasm_TA_OpenSessionEntryPoint ret: 0x%" PRIx32 "\n", results[0].of.i32);
        if (results[0].of.i32 != TEE_SUCCESS) {
            res = results[0].of.i32;
            goto out;
        }
    } else {
        EMSG("%08x : %s\n", TEE_ERROR_OUT_OF_MEMORY, wasm_runtime_get_exception(utc->wasm_module_inst));
        wasm_free_app_params(utc, &ta_arguments[1]);
        res = TEE_ERROR_GENERIC;
        goto out;
    }

    res = wasm_copy_out_app_params(utc, ta_sess->param->types, &ta_arguments[1], p_cookie,
        ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("%08x :0x%" PRIx32 "\n", TEE_ERROR_GENERIC, res);
        goto out;
    }

    if (res == TEE_SUCCESS) {
        size_t session_ctx_value = *((size_t*)buffer);
        s->user_ctx = (void*)session_ctx_value;
    }
    res = results[0].of.i32;
out:
    if (buffer_for_wasm) {
        wasm_runtime_module_free(utc->wasm_module_inst, buffer_for_wasm);
    }

    ts_sess = ts_pop_current_session();
    assert(ts_sess == s);

    return res;
}

static TEE_Result user_ta_wasm_enter_invoke_cmd(struct ts_session* s, uint32_t cmd)
{
    /* fixed CID 209922, UNUSED_VALUE(res) */
    TEE_Result res = TEE_ERROR_GENERIC;
    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    struct tee_ta_session* ta_sess = to_ta_session(s);
    struct ts_session* ts_sess __maybe_unused = NULL;
    wasm_val_t ta_arguments[7] = { 0 };
    wasm_val_t p_cookie[4] = { 0 };
    wasm_val_t results[1] = { 0 };

    ts_push_current_session(s);

    /* lookup a WASM function by its name. */
    utc->func = wasm_runtime_lookup_function(utc->wasm_module_inst,
        "wasm_TA_InvokeCommandEntryPoint");
    if (!utc->func) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
        res = TEE_ERROR_GENERIC;
        goto out;
    }

    /* TEE_Result TA_EXPORT TA_InvokeCommandEntryPoint(
     *				[ctx] void* sessionContext,
     *				uint32_t commandID,
     *				uint32_t paramTypes,
     *				[inout] TEE_Param params[4]);
     */

    SET_WASM_VAL_T_VALUE(ta_arguments, 0, s->user_ctx);
    ta_arguments[1].of.i32 = cmd;
    ta_arguments[1].kind = WASM_I32;
    ta_arguments[2].of.i32 = ta_sess->param->types;
    ta_arguments[2].kind = WASM_I32;

    // ta_argv[3,4,5,6] = p0..p3
    res = wasm_copy_in_app_params(utc, ta_sess->param->types, &ta_arguments[3], p_cookie,
        ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("%08x : 0x%" PRIx32 "\n", TEE_ERROR_GENERIC, res);
        goto out;
    }

    results[0].kind = WASM_I32;
    results[0].of.i32 = TEE_ERROR_GENERIC;
    if (wasm_runtime_call_wasm_a(utc->exec_env, utc->func, 1, results, 7, ta_arguments)) {
        DMSG("call wasm_TA_InvokeCommandEntryPoint ret: 0x%" PRIx32 "\n", results[0].of.i32);
        if (results[0].of.i32 != TEE_SUCCESS) {
            res = results[0].of.i32;
            goto out;
        }
    } else {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_runtime_get_exception(utc->wasm_module_inst));
        wasm_free_app_params(utc, &ta_arguments[3]);
        res = TEE_ERROR_GENERIC;
        goto out;
    }

    res = wasm_copy_out_app_params(utc, ta_sess->param->types, &ta_arguments[3], p_cookie,
        ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("%08x : 0x%" PRIx32 "\n", TEE_ERROR_GENERIC, res);
    }
    res = results[0].of.i32;

out:
    ts_sess = ts_pop_current_session();
    assert(ts_sess == s);
    return res;
}

static void user_ta_wasm_enter_close_session(struct ts_session* s)
{
    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    wasm_val_t ta_arguments[1] = { 0 };
    struct ts_session* ts_sess __maybe_unused = NULL;
    ts_push_current_session(s);

    /* lookup a WASM function by its name. */
    utc->func = wasm_runtime_lookup_function(utc->wasm_module_inst,
        "wasm_TA_CloseSessionEntryPoint");
    if (!utc->func) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
        goto out;
    }

    /* void TA_EXPORT TA_CloseSessionEntryPoint( [ctx] void* sessionContext); */
    SET_WASM_VAL_T_VALUE(ta_arguments, 0, s->user_ctx);
    if (wasm_runtime_call_wasm_a(utc->exec_env, utc->func, 0, NULL, 1, ta_arguments)) {
        /* to do */
    } else {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_runtime_get_exception(utc->wasm_module_inst));
        goto out;
    }

    DMSG("context.ref_count: %" PRIu32 "\n", utc->ta_ctx.ref_count);
    /* call destory entry point if last opened session */
    if (utc->ta_ctx.ref_count == 1) {
        /* lookup a WASM function by its name. */
        utc->func = wasm_runtime_lookup_function(utc->wasm_module_inst,
            "wasm_TA_DestroyEntryPoint");
        if (!utc->func) {
            EMSG("%08x\n", TEE_ERROR_GENERIC);
            goto out;
        }

        /* void TA_EXPORT TA_DestroyEntryPoint( void ); */
        if (wasm_runtime_call_wasm_a(utc->exec_env, utc->func, 0, NULL, 0, NULL)) {
            /* to do */
        } else {
            EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_runtime_get_exception(utc->wasm_module_inst));
        }
    }

out:
    ts_sess = ts_pop_current_session();
}

static void user_ta_wasm_ctx_destroy(struct ts_ctx* ctx)
{
    struct user_ta_ctx* utc = to_user_ta_ctx(ctx);

    DMSG("context.ref_count: %" PRIu32 "\n", utc->ta_ctx.ref_count);
    /*
     * Close sessions opened by this TA
     * Note that tee_ta_close_session() removes the item
     * from the utc->open_sessions list.
     */
    /* CID 209913, USE_AFTER_FREE. No problem, because TAILQ_REMOVE utc->open_sessions.tqh_first */
    while (!TAILQ_EMPTY(&utc->open_sessions)) {
        tee_ta_close_session(TAILQ_FIRST(&utc->open_sessions),
            &utc->open_sessions, KERN_IDENTITY);
    }

    /* Free cryp states created by this TA */
    tee_svc_cryp_free_states(utc);
    /* Close cryp objects opened by this TA */
    tee_obj_close_all(utc);

    /* destroy wasm members */
    if (utc->exec_env) {
        wasm_runtime_destroy_exec_env(utc->exec_env);
    }
    if (utc->wasm_module_inst) {
        wasm_runtime_deinstantiate(utc->wasm_module_inst);
    }
    if (utc->wasm_module) {
        wasm_runtime_unload(utc->wasm_module);
    }
    if (utc->is_xip_file) {
        munmap(utc->wasm_file_buffer, utc->wasm_file_size);
    } else {
        if (utc->wasm_file_buffer) {
            wasm_runtime_free(utc->wasm_file_buffer);
        }
    }
    free(utc);
}

static int b_memcpy_s(void* dest, unsigned int s1max, const void* src, unsigned int n)
{
    if (n == 0) {
        return 0;
    }

    if (dest == NULL) {
        return -1;
    }

    if (src == NULL || n > s1max) {
        memset(dest, 0, s1max);
        return -1;
    }

    memcpy(dest, src, n);
    return 0;
}

/*
 * Note: this variable is weak just to ease breaking its dependency chain
 * when added to the unpaged area.
 */
const struct ts_ops user_ta_wasm_ops __weak __relrodata_unpaged("user_ta_wasm_ops") = {
    .enter_open_session = user_ta_wasm_enter_open_session,
    .enter_invoke_cmd = user_ta_wasm_enter_invoke_cmd,
    .enter_close_session = user_ta_wasm_enter_close_session,
    .destroy = user_ta_wasm_ctx_destroy,
};

static void set_ta_ctx_ops(struct tee_ta_ctx* ctx)
{
    ctx->ts_ctx.ops = &user_ta_wasm_ops;
}

#ifdef WASM_HEAP_POOL
static char global_heap_buf[128 * 1024] = { 0 };
#endif

#define WASM_FILE_TEMPLATE "/etc/ta/00112233445566778899AABBCCDDEEFF"
#define WASM_FILE_TEMPLATE_DIR_SIZE 8

extern uint32_t get_libtee_builtin_export_apis(NativeSymbol** p_libtee_builtin_apis);

static TEE_Result tee_ta_init_user_ta_wasm_session(const TEE_UUID* uuid __unused,
    struct tee_ta_session* s)
{
    TEE_Result res = TEE_ERROR_GENERIC;
    struct user_ta_ctx* utc = NULL;
    char wasm_file[64] = { 0 };
    uint32_t pos = WASM_FILE_TEMPLATE_DIR_SIZE;
    uint32_t stack_size = 48 * 1024, heap_size = 32 * 1024;
    char error_buf[128] = { 0 };
    NativeSymbol* native_symbols;
    uint32_t n_native_symbols;
    RuntimeInitArgs init_args;

    /* convert uuid to wasm file name */
    strcpy(wasm_file, WASM_FILE_TEMPLATE);
    pos += tee_b2hs((uint8_t*)uuid, (uint8_t*)(wasm_file + pos),
        sizeof(TEE_UUID), sizeof(wasm_file) - pos);

    DMSG("Open ta: %s\n", wasm_file);

    /* Register context */
    utc = calloc(1, sizeof(struct user_ta_ctx));
    if (!utc) {
        EMSG("%08x : %zu\n", TEE_ERROR_OUT_OF_MEMORY, sizeof(struct user_ta_ctx));
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto out1;
    }

    if (!wasm_runtime_init_flag) {
        /* initialize init arguments */
        DMSG("wasm runtime init ...\n");
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
#ifdef WASM_HEAP_POOL
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
#else
        init_args.mem_alloc_type = Alloc_With_Allocator;
        init_args.mem_alloc_option.allocator.malloc_func = malloc;
        init_args.mem_alloc_option.allocator.realloc_func = realloc;
        init_args.mem_alloc_option.allocator.free_func = free;
#endif
        /* initialize runtime environment */
        if (!wasm_runtime_full_init(&init_args)) {
            EMSG("%08x\n", TEE_ERROR_GENERIC);
            goto out2;
        }

        n_native_symbols = get_libtee_builtin_export_apis(&native_symbols);

        if (!wasm_runtime_register_natives("env", native_symbols, n_native_symbols)) {
            EMSG("%08x\n", TEE_ERROR_GENERIC);
            goto out2;
        }

        wasm_runtime_init_flag = 1;
    }

    /* load WASM byte buffer from WASM bin file */
#ifdef FILE_TO_BUFFER
    if (!(utc->wasm_file_buffer = (uint8_t*)bh_read_file_to_buffer(wasm_file, &utc->wasm_file_size))) {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_file);
        goto out2;
    }
#else
    int fd;
    struct stat stat_buf;

    fd = open(wasm_file, O_RDONLY);
    if (fd < 0) {
        EMSG("%08x : %s, %d\n", TEE_ERROR_GENERIC, wasm_file, fd);
        goto out2;
    }
    if (fstat(fd, &stat_buf) != 0) {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, wasm_file);
        close(fd);
        goto out2;
    }
    utc->wasm_file_size = (uint32_t)stat_buf.st_size;
    DMSG("ta size: %" PRIu32 "\n", utc->wasm_file_size);
    utc->wasm_file_buffer = (uint8_t*)mmap(NULL, utc->wasm_file_size, PROT_READ, MAP_SHARED | MAP_FILE, fd, 0);
    if (!utc->wasm_file_buffer || utc->wasm_file_buffer == (uint8_t*)MAP_FAILED) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
        /* fixed CID 209895, RESOURCE_LEAK */
        close(fd);
        goto out2;
    }
    DMSG("file address: 0x%" PRIxPTR "\n", (uintptr_t)utc->wasm_file_buffer);
    close(fd);
    utc->is_xip_file = true;
#endif

    /* map xip file */
    if (wasm_runtime_is_xip_file(utc->wasm_file_buffer, utc->wasm_file_size)) {
        DMSG("XIP ta\n");
    }
#ifndef FILE_TO_BUFFER
    else {
        DMSG("!XIP ta\n");
        void* tmp_buf = wasm_runtime_malloc(utc->wasm_file_size);
        if (tmp_buf == NULL) {
            EMSG("%08x : %" PRIu32 "\n", TEE_ERROR_OUT_OF_MEMORY, utc->wasm_file_size);
            goto out2;
        }
        b_memcpy_s(tmp_buf, utc->wasm_file_size, utc->wasm_file_buffer, utc->wasm_file_size);
        munmap(utc->wasm_file_buffer, utc->wasm_file_size);
        utc->wasm_file_buffer = tmp_buf;
        utc->is_xip_file = false;
    }
#endif

    /* load WASM module */
    if (!(utc->wasm_module = wasm_runtime_load(utc->wasm_file_buffer, utc->wasm_file_size,
              error_buf, sizeof(error_buf)))) {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, error_buf);
        goto out2;
    }

    /* instantiate the module */
    if (!(utc->wasm_module_inst = wasm_runtime_instantiate(utc->wasm_module, stack_size, heap_size,
              error_buf, sizeof(error_buf)))) {
        EMSG("%08x : %s\n", TEE_ERROR_GENERIC, error_buf);
        goto out2;
    }

    utc->stack_size = stack_size;
    /* creat an execution environment to execute the WASM functions */
    if (!(utc->exec_env = wasm_runtime_create_exec_env(utc->wasm_module_inst,
              stack_size))) {
        EMSG("%08x\n", TEE_ERROR_GENERIC);
        goto out2;
    }

    TAILQ_INIT(&utc->open_sessions);
    TAILQ_INIT(&utc->cryp_states);
    TAILQ_INIT(&utc->objects);

    utc->ta_ctx.flags = TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION;
    utc->ta_ctx.ts_ctx.uuid = *uuid;

    set_ta_ctx_ops(&utc->ta_ctx);

    /* Mark as initializing before adding to tee_ctxes */
    utc->uctx.is_initializing = true;

    /*
     * Protect ref_count and tee_ctxes list operations with mutex,
     * following the pattern from native OPTEE user_ta.c
     */
    mutex_lock(&tee_ta_mutex);
    utc->ta_ctx.ref_count = 1;
    s->ts_sess.ctx = &utc->ta_ctx.ts_ctx;
    s->ts_sess.handle_scall = s->ts_sess.ctx->ops->handle_scall;
    /*
     * Another thread trying to load this same TA may need to wait
     * until this context is fully initialized. This is needed to
     * handle single instance TAs.
     */
    TAILQ_INSERT_TAIL(&tee_ctxes, &utc->ta_ctx, link);
    mutex_unlock(&tee_ta_mutex);

    DMSG("Context was successfully inserted!\n");

    /*
     * Mark initialization complete and notify any waiting threads.
     * In native OPTEE, ldelf initialization happens here, but for WASM TA
     * the initialization is already complete at this point.
     */
    mutex_lock(&tee_ta_mutex);
    utc->uctx.is_initializing = false;
    condvar_broadcast(&tee_ta_init_cv);
    mutex_unlock(&tee_ta_mutex);

    return TEE_SUCCESS;

out2:
    res = TEE_ERROR_GENERIC;
    if (utc->exec_env) {
        wasm_runtime_destroy_exec_env(utc->exec_env);
    }
    if (utc->wasm_module_inst) {
        wasm_runtime_deinstantiate(utc->wasm_module_inst);
    }
    if (utc->wasm_module) {
        wasm_runtime_unload(utc->wasm_module);
    }
    if (utc->is_xip_file) {
        munmap(utc->wasm_file_buffer, utc->wasm_file_size);
    } else {
        if (utc->wasm_file_buffer) {
            wasm_runtime_free(utc->wasm_file_buffer);
        }
    }
    if (utc) {
        free(utc);
    }

out1:
    DMSG("res: 0x%" PRIx32 "\n", res);
    return res;
}

bool is_user_ta_ctx(struct ts_ctx* ctx)
{
    return ctx && ctx->ops == &user_ta_wasm_ops;
}

TEE_Result tee_ta_init_user_ta_session(
    const TEE_UUID* uuid __unused,
    struct tee_ta_session* s __unused)
{
    return tee_ta_init_user_ta_wasm_session(uuid, s);
}
