/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2018, Linaro Limited.
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

#ifndef USER_TA_WASM_HEADER_H
#define USER_TA_WASM_HEADER_H

#include <user_ta_header.h>

struct user_ta_head {
	TEE_UUID uuid;
	const char *name;
	uint32_t flags;
	TEE_Result (*create_entry_point)(void);
	void (*destroy_entry_point)(void);
	TEE_Result (*open_session_entry_point)(uint32_t nParamTypes,
				TEE_Param pParams[TEE_NUM_PARAMS],
				void **ppSessionContext);
	void (*close_session_entry_point)(void *pSessionContext);
	TEE_Result (*invoke_command_entry_point)(void *pSessionContext,
				uint32_t nCommandID, uint32_t nParamTypes,
				TEE_Param pParams[TEE_NUM_PARAMS]);
};

#endif /* USER_TA_WASM_HEADER_H */
