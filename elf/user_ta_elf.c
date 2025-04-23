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

#include <nuttx/config.h>

#include <assert.h>
#include <string.h>

#include <dlfcn.h>
#include <kernel/tee_misc.h>
#include <kernel/tee_ta_manager.h>
#include <kernel/user_ta.h>
#include <malloc.h>
#include <mm/mobj.h>
#include <nuttx/symtab.h>
#include <tee/tee_svc_cryp.h>
#include <user_ta_wasm_header.h>

#define ELF_FILE_TEMPLATE "/etc/ta/00112233445566778899AABBCCDDEEFF"
#define ELF_FILE_TEMPLATE_SIZE 8

extern const struct symtab_s g_elf_ta_exports[];
extern const int g_elf_ta_nexports;

static void free_memref_buffers(TEE_Param* params)
{
    for (int n = 0; n < 4; n++) {
        if (params[n].memref.buffer) {
            free(params[n].memref.buffer);
            params[n].memref.buffer = NULL;
        }
    }
}

static TEE_Result convert_params_from_ta(uint32_t param_types, struct tee_ta_param* dst, TEE_Param* src)
{
    TEE_Result res = TEE_SUCCESS;

    for (int n = 0; n < 4; n++) {
        uint32_t type = TEE_PARAM_TYPE_GET(param_types, n);

        switch (type) {
        case TEE_PARAM_TYPE_NONE:
            break;
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
        case TEE_PARAM_TYPE_VALUE_INOUT:
            /*  dst->u[n].val is defined as:
                    struct param_val {
                        uint32_t a;
                        uint32_t b;
                    };
                src[n].value is defined as:
                    struct {
                        uint32_t a;
                        uint32_t b;
                    } value;
            */
            dst->u[n].val.a = src[n].value.a;
            dst->u[n].val.b = src[n].value.b;
            break;
        case TEE_PARAM_TYPE_MEMREF_OUTPUT:
        case TEE_PARAM_TYPE_MEMREF_INOUT:
            if (!dst || !dst->u[n].mem.mobj) {
                EMSG("dst param error!!!");
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            if (src[n].memref.size > dst->u[n].mem.mobj->size) {
                EMSG("src param error!!!");
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            /*
                dst->u[n].mem is defined as:
                    struct mobj {
                        size_t size;
                        void *buffer;
                    };
                    struct param_mem {
                        struct mobj *mobj;
                        size_t size;
                        size_t offs;
                    };
                src[n].memref is defined as:
                    struct {
                        void *buffer;
                        size_t size;
                    } memref;
            */
            memcpy(dst->u[n].mem.mobj->buffer, src[n].memref.buffer, src[n].memref.size);
            dst->u[n].mem.mobj->size = src[n].memref.size;
            dst->u[n].mem.size = src[n].memref.size;
            break;
        default:
            break;
        }
    }

    return TEE_SUCCESS;
out:
    free_memref_buffers(src);

    return res;
}

static TEE_Result convert_params_to_ta(uint32_t param_types, TEE_Param* dst, struct tee_ta_param* src)
{
    TEE_Result res = TEE_SUCCESS;
    memset(dst, 0, 4 * sizeof(TEE_Param));

    for (int n = 0; n < 4; n++) {
        uint32_t type = TEE_PARAM_TYPE_GET(param_types, n);

        switch (type) {
        case TEE_PARAM_TYPE_NONE:
            break;
        case TEE_PARAM_TYPE_VALUE_INPUT:
        case TEE_PARAM_TYPE_VALUE_OUTPUT:
        case TEE_PARAM_TYPE_VALUE_INOUT:
            /*  src->u[n].val is defined as:
                    struct param_val {
                        uint32_t a;
                        uint32_t b;
                    };
                dst[n].value is defined as:
                    struct {
                        uint32_t a;
                        uint32_t b;
                    } value;
            */
            dst[n].value.a = src->u[n].val.a;
            dst[n].value.b = src->u[n].val.b;
            break;
        case TEE_PARAM_TYPE_MEMREF_INPUT:
        case TEE_PARAM_TYPE_MEMREF_OUTPUT:
        case TEE_PARAM_TYPE_MEMREF_INOUT:
            if (!src || !src->u[n].mem.mobj) {
                EMSG("src param error!!!");
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            if (src->u[n].mem.size > src->u[n].mem.mobj->size) {
                EMSG("src param error!!!");
                res = TEE_ERROR_BAD_PARAMETERS;
                goto out;
            }
            /*
                src->u[n].mem is defined as:
                    struct mobj {
                        size_t size;
                        void *buffer;
                    };
                    struct param_mem {
                        struct mobj *mobj;
                        size_t size;
                        size_t offs;
                    };
                dst[n].memref is defined as:
                    struct {
                        void *buffer;
                        size_t size;
                    } memref;
            */
            dst[n].memref.buffer = (void*)malloc(src->u[n].mem.size);
            if (dst[n].memref.buffer == NULL) {
                EMSG("ERROR: malloc for memref.buffer failed\n");
                res = TEE_ERROR_OUT_OF_MEMORY;
                goto out;
            }
            memcpy(dst[n].memref.buffer, src->u[n].mem.mobj->buffer + src->u[n].mem.offs, src->u[n].mem.size);
            dst[n].memref.size = src->u[n].mem.size;
            break;
        default:
            EMSG("Unsupported parameter type: 0x%" PRIx32 "\n", type);
            res = TEE_ERROR_BAD_PARAMETERS;
            goto out;
        }
    }

    return TEE_SUCCESS;
out:
    free_memref_buffers(dst);

    return res;
}

static TEE_Result user_ta_enter_open_session(struct ts_session* s)
{
    TEE_Result res = TEE_ERROR_GENERIC;
    struct ts_session* ts_sess __maybe_unused = NULL;
    struct tee_ta_session* ta_sess = to_ta_session(s);

    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    struct user_ta_head* user_ta = *(utc->user_ta);
    ts_push_current_session(s);
    DMSG("context.ref_count: %" PRIu32 "\n", utc->ta_ctx.ref_count);

    /* call create entry point if first open session, or when the session is re-opened
     * but the utc->func is not inited, we need to perform the init action
     */
    if (utc->ta_ctx.ref_count >= 1) {
        res = user_ta->create_entry_point();
        DMSG("%s ret: 0x%08" PRIx32 " <<\n", __func__, res);
    }

    /* TEE_Result TA_EXPORT TA_OpenSessionEntryPoint(
     *				uint32_t paramTypes,
     *				[inout] TEE_Param params[4],
     *				[out][ctx] void** sessionContext )
     */
    TEE_Param params[4] = { 0 };
    res = convert_params_to_ta(ta_sess->param->types, params, ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("ERROR: convert_params_to_ta failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

    void* session_context = NULL;
    res = user_ta->open_session_entry_point(ta_sess->param->types, params, &session_context);
    if (res != 0) {
        EMSG("ERROR: user_ta->open_session_entry_point failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

    res = convert_params_from_ta(ta_sess->param->types, ta_sess->param, params);
    if (res != TEE_SUCCESS) {
        EMSG("ERROR: convert_params_from_ta failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

    // Set the session context
    s->user_ctx = session_context;

out:
    free_memref_buffers(params);
    ts_sess = ts_pop_current_session();
    assert(ts_sess == s);

    return res;
}

static TEE_Result user_ta_enter_invoke_cmd(struct ts_session* s, uint32_t cmd)
{
    TEE_Result res = TEE_ERROR_GENERIC;
    struct ts_session* ts_sess __maybe_unused = NULL;
    struct tee_ta_session* ta_sess = to_ta_session(s);
    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    struct user_ta_head* user_ta = *(utc->user_ta);

    ts_push_current_session(s);

    /* TEE_Result TA_EXPORT TA_InvokeCommandEntryPoint(
     *				[ctx] void* sessionContext,
     *				uint32_t commandID,
     *				uint32_t paramTypes,
     *				[inout] TEE_Param params[4]);
     */

    void* user_ctx = s->user_ctx;
    TEE_Param params[4];
    res = convert_params_to_ta(ta_sess->param->types, params, ta_sess->param);
    if (res != TEE_SUCCESS) {
        EMSG("ERROR: convert_params_to_ta failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

    res = user_ta->invoke_command_entry_point(user_ctx, cmd,
        ta_sess->param->types, params);
    if (res != 0) {
        EMSG("ERROR: user_ta->invoke_command_entry_point failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

    res = convert_params_from_ta(ta_sess->param->types, ta_sess->param, params);
    if (res != TEE_SUCCESS) {
        EMSG("ERROR: convert_params_from_ta failed with 0x%08" PRIx32 "\n", res);
        goto out;
    }

out:
    ts_sess = ts_pop_current_session();
    assert(ts_sess == s);

    return res;
}

static void user_ta_enter_close_session(struct ts_session* s)
{
    struct ts_session* ts_sess __maybe_unused = NULL;
    struct user_ta_ctx* utc = to_user_ta_ctx(s->ctx);
    struct user_ta_head* user_ta = *(utc->user_ta);

    ts_push_current_session(s);
    DMSG("context.ref_count: %" PRIu32 "\n", utc->ta_ctx.ref_count);

    /* void TA_EXPORT TA_CloseSessionEntryPoint( [ctx] void* sessionContext); */
    void* user_ctx = s->user_ctx;
    user_ta->close_session_entry_point(user_ctx);

    /* call destory entry point if last opened session */
    if (utc->ta_ctx.ref_count == 1) {
        /* void TA_EXPORT TA_DestroyEntryPoint( void ); */
        user_ta->destroy_entry_point();
    }

    ts_sess = ts_pop_current_session();
}

static void user_ta_ctx_destroy(struct ts_ctx* ctx)
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

    /* Close the second shared library */
    if (utc->ta_handle) {
        int ret = dlclose(utc->ta_handle);
        if (ret < 0) {
            EMSG("ERROR: dlclose(utc->ta_handle) failed: %d\n", ret);
        }
    }

    free(utc);
}

/*
 * Note: this variable is weak just to ease breaking its dependency chain
 * when added to the unpaged area.
 */
const struct ts_ops user_ta_ops __weak __relrodata_unpaged("user_ta_ops") = {
    .enter_open_session = user_ta_enter_open_session,
    .enter_invoke_cmd = user_ta_enter_invoke_cmd,
    .enter_close_session = user_ta_enter_close_session,
    .destroy = user_ta_ctx_destroy,
};

static void set_ta_ctx_ops(struct tee_ta_ctx* ctx)
{
    ctx->ts_ctx.ops = &user_ta_ops;
}

static TEE_Result tee_ta_init_elf_user_ta_session(const TEE_UUID* uuid __unused, struct tee_ta_session* s __unused)
{
    TEE_Result res = TEE_SUCCESS;
    char elf_file[64] = { 0 };
    uint32_t pos = ELF_FILE_TEMPLATE_SIZE;
    struct user_ta_ctx* utc = NULL;

    utc = calloc(1, sizeof(struct user_ta_ctx));
    if (utc == NULL) {
        return TEE_ERROR_OUT_OF_MEMORY;
    }

    /* Get the filename */
    strcpy(elf_file, ELF_FILE_TEMPLATE);
    pos += tee_b2hs((uint8_t*)uuid, (uint8_t*)(elf_file + pos),
        sizeof(TEE_UUID), sizeof(elf_file) - pos);

    DMSG("Loaded ELF TA file: %s\n", elf_file);

    /* Set the shared library symbol table */
    res = dlsymtab(g_elf_ta_exports, g_elf_ta_nexports);
    if (res < 0) {
        EMSG("ERROR: dlsymtab failed: 0x%08" PRIx32 "\n", res);
        return res;
    }

    /* Install the shared library */
    utc->ta_handle = dlopen(elf_file, 0);
    if (utc->ta_handle == NULL) {
        EMSG("ERROR: dlopen %s failed\n", elf_file);
        return -1;
    }
    DMSG("dlopen %s successfully\n", elf_file);

    utc->user_ta = (struct user_ta_head**)dlsym(utc->ta_handle, "user_ta");
    if (utc->user_ta == NULL) {
        EMSG("ERROR: Failed to get symbol user_ta\n");
        return -1;
    }

    TAILQ_INIT(&utc->open_sessions);
    TAILQ_INIT(&utc->cryp_states);
    TAILQ_INIT(&utc->objects);

    utc->ta_ctx.flags = TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION;
    utc->ta_ctx.ts_ctx.uuid = *uuid;

    /* set ta operations */
    set_ta_ctx_ops(&utc->ta_ctx);
    utc->ta_ctx.ref_count++;

    utc->uctx.is_initializing = false;
    s->ts_sess.ctx = &utc->ta_ctx.ts_ctx;

    TAILQ_INSERT_TAIL(&tee_ctxes, &utc->ta_ctx, link);
    DMSG("Context was successfully inserted!\n");

    return res;
}

bool is_user_ta_ctx(struct ts_ctx* ctx)
{
    return ctx && ctx->ops == &user_ta_ops;
}

TEE_Result tee_ta_init_user_ta_session(
    const TEE_UUID* uuid __unused,
    struct tee_ta_session* s __unused)
{
    return tee_ta_init_elf_user_ta_session(uuid, s);
}
