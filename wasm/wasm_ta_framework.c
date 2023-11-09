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

#include <tee_internal_api.h>
#include <trace.h>
#include <kernel/user_ta.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <user_ta_wasm_header.h>

extern struct user_ta_head *user_ta;

uint32_t wasm_TA_CreateEntryPoint(uint32_t dummy)
{
	TEE_Result ret;

	DMSG("%s >>\n", __func__);
	if (!user_ta || !user_ta->create_entry_point) {
		EMSG("TEE bad parameters\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	ret = user_ta->create_entry_point();
	DMSG("%s ret: 0x%08" PRIx32 " <<\n", __func__, ret);
	return ret;
}

void wasm_TA_DestroyEntryPoint(void)
{
	DMSG("%s >>\n", __func__);
	if (!user_ta || !user_ta->destroy_entry_point) {
		EMSG("TEE bad parameters\n");
		return;
	}

	user_ta->destroy_entry_point();
	DMSG("%s <<\n", __func__);
}

static TEE_Result wasm_copy_in_params(uint32_t param_types, uint32_t *p, TEE_Param *params)
{
	/* p[] format case 1: size(4 bytes) + buffer(size bytes)
	 *            case 2: a(4 bytes) + b(4 bytes)
	 */
	uint32_t type;

	memset(params, 0, sizeof(TEE_Param) * 4);

	for (int n = 0; n < 4; n++) {
		type = TEE_PARAM_TYPE_GET(param_types, n);
		switch(type){
		case TEE_PARAM_TYPE_NONE:
			params[n].value.a = 0;
			params[n].value.b = 0;
			break;
		case TEE_PARAM_TYPE_MEMREF_INPUT:
		case TEE_PARAM_TYPE_MEMREF_OUTPUT:
		case TEE_PARAM_TYPE_MEMREF_INOUT:
			params[n].memref.size = *((uint32_t *)(p[n]));
			params[n].memref.buffer = (void *)(p[n] + 4);
			break;
		case TEE_PARAM_TYPE_VALUE_INPUT:
		case TEE_PARAM_TYPE_VALUE_OUTPUT:
		case TEE_PARAM_TYPE_VALUE_INOUT:
			params[n].value.a =*((uint32_t *)(p[n]));
			params[n].value.b =*((uint32_t *)(p[n] + 4));
			break;
		default:
			EMSG("TEE item not found: 0x%08" PRIx32 "\n", type);
			return TEE_ERROR_ITEM_NOT_FOUND;
		}
	}
	return TEE_SUCCESS;
}

static void wasm_copy_out_params(uint32_t param_types, uint32_t *p, TEE_Param *params)
{
	/* p[] format case 1: size(4 bytes) + buffer(size bytes)
	 *            case 2: a(4 bytes) + b(4 bytes)
	 */
	uint32_t type;

	for (int n = 0; n < 4; n++) {
		type = TEE_PARAM_TYPE_GET(param_types, n);
		switch(type){
		case TEE_PARAM_TYPE_MEMREF_OUTPUT:
		case TEE_PARAM_TYPE_MEMREF_INOUT:
			*((uint32_t *)(p[n])) = params[n].memref.size;
			break;
		case TEE_PARAM_TYPE_VALUE_OUTPUT:
		case TEE_PARAM_TYPE_VALUE_INOUT:
			*((uint32_t *)(p[n])) =	params[n].value.a;
			*((uint32_t *)(p[n] + 4)) = params[n].value.b;
			break;
		default:
			break;
		}
	}
}

TEE_Result wasm_TA_OpenSessionEntryPoint(uint32_t param_types,
	uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t *sess_ctx)
{
	TEE_Result ret;
	TEE_Param params[4];
	uint32_t _pl[4];
	void * ctx;

	DMSG("%s >>\n", __func__);
	if (!user_ta || !user_ta->open_session_entry_point) {
		EMSG("%s -> the user_ta are invalid or open_session_entry_point of user_ta invalid!!!", __FUNCTION__);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	_pl[0] = p0;
	_pl[1] = p1;
	_pl[2] = p2;
	_pl[3] = p3;

	ret = wasm_copy_in_params(param_types, _pl, params);
	if (ret != TEE_SUCCESS) {
		EMSG("TEE generic error: 0x%08" PRIx32 "\n", ret);
		goto out;
	}

	ret = user_ta->open_session_entry_point(param_types, params, &ctx);

	wasm_copy_out_params(param_types, _pl, params);

	if (ret != TEE_SUCCESS) {
		EMSG("TEE generic error: 0x%08" PRIx32 "\n", ret);
		goto out;
	}

	*sess_ctx = (uint32_t)ctx;
out:
	DMSG("%s ret: 0x%08" PRIx32 " <<\n", __func__, ret);
	return ret;
}

void wasm_TA_CloseSessionEntryPoint(uint32_t sess_ctx)
{
	DMSG("%s >>\n", __func__);
	if (!user_ta || !user_ta->close_session_entry_point) {
		EMSG("TEE bad parameters\n");
		return;
	}

	user_ta->close_session_entry_point((void *)sess_ctx);
	DMSG("%s <<\n", __func__);
}

TEE_Result wasm_TA_InvokeCommandEntryPoint(uint32_t sess_ctx, uint32_t cmd_id,
	uint32_t param_types,
	uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3)
{
	TEE_Result ret;
	TEE_Param params[4];
	uint32_t _pl[4];

	DMSG("%s >>\n", __func__);
	if (!user_ta || !user_ta->invoke_command_entry_point) {
		return TEE_ERROR_BAD_PARAMETERS;
	}
	_pl[0] = p0;
	_pl[1] = p1;
	_pl[2] = p2;
	_pl[3] = p3;

	ret = wasm_copy_in_params(param_types, _pl, params);
	if (ret != TEE_SUCCESS) {
		EMSG("TEE generic error: 0x%08" PRIx32 "\n", ret);
		goto out;
	}

	ret =  user_ta->invoke_command_entry_point((void *)sess_ctx, cmd_id,
						      param_types, params);

	wasm_copy_out_params(param_types, _pl, params);

	if (ret != TEE_SUCCESS) {
		EMSG("TEE generic error: 0x%08" PRIx32 "\n", ret);
		goto out;
	}

out:
	DMSG("%s ret: 0x%08" PRIx32 " <<\n", __func__, ret);
	return ret;
}
