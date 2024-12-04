/*
 * Copyright (C) 2020-2024 Xiaomi Corporation
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

#include <kernel/user_ta.h>
#include <tee_internal_api.h>
#include <trace.h>
#include <user_ta_header.h>

#include <stdlib.h>

/* a dummy ta_head implementation  */
const struct ta_head ta_head = {
    .uuid = {},
    .stack_size = 0,
    .flags = 0,
    .depr_entry = UINT64_MAX,
};

uint8_t ta_heap[1];

const size_t ta_heap_size = sizeof(ta_heap);

void __utee_tcb_init(void) { }

int tahead_get_trace_level(void)
{
    return TRACE_INFO;
}

TEE_Result TEE_GetPropertyAsU32(TEE_PropSetHandle propsetOrEnumerator,
    const char* name, uint32_t* value)
{
    return TEE_ERROR_NOT_IMPLEMENTED;
}

TEE_Result TA_CreateEntryPoint(void)
{
    return TEE_ERROR_NOT_IMPLEMENTED;
}

void TA_DestroyEntryPoint(void) { }

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
    TEE_Param __unused params[4],
    void** tee_session)
{
    return TEE_ERROR_NOT_IMPLEMENTED;
}

void TA_CloseSessionEntryPoint(void* tee_session) { }

TEE_Result TA_InvokeCommandEntryPoint(void* tee_session, uint32_t cmd,
    uint32_t ptypes,
    TEE_Param params[TEE_NUM_PARAMS])
{
    return TEE_ERROR_NOT_IMPLEMENTED;
}
