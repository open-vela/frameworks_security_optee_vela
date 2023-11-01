/*
 * Copyright (c) 2019-2021, Linaro Limited
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

#include <host_fs.h>
#include <optee_msg.h>
#include <optee_rpc_cmd.h>
#include <tee/tee_cryp_utl.h>
#include <tee/tee_fs_rpc.h>

static bool thread_prealloc_rpc_cache;
static unsigned int thread_rpc_pnum;

static TEE_Result handle_fs_op(size_t num_params, struct thread_param *params) {
	TEE_Result res;
	if (num_params == 0) {
		res = TEE_ERROR_BAD_PARAMETERS;
		goto out;
	}

	unsigned int cmd = params[0].u.value.a;
	switch (cmd) {
		case OPTEE_RPC_FS_OPEN:
			res = host_fs_open(num_params, params);
			break;
		case OPTEE_RPC_FS_CREATE:
			res = host_fs_create(num_params, params);
			break;
		case OPTEE_RPC_FS_CLOSE:
			res = host_fs_close(num_params, params);
			break;
		case OPTEE_RPC_FS_READ:
			res = host_fs_read(num_params, params);
			break;
		case OPTEE_RPC_FS_WRITE:
			res = host_fs_write(num_params, params);
			break;
		case OPTEE_RPC_FS_TRUNCATE:
			res = host_fs_truncate(num_params, params);
			break;
		case OPTEE_RPC_FS_REMOVE:
			res = host_fs_remove(num_params, params);
			break;
		case OPTEE_RPC_FS_RENAME:
			res = host_fs_rename(num_params, params);
			break;
		default:
			res = TEE_ERROR_NOT_IMPLEMENTED;
			break;
	}

out:
	return res;
}

uint32_t thread_rpc_cmd(uint32_t cmd, size_t num_params,
			struct thread_param *params)
{
	TEE_Result res;
	/* The source CRYPTO_RNG_SRC_JITTER_RPC is safe to use here */
	plat_prng_add_jitter_entropy(CRYPTO_RNG_SRC_JITTER_RPC,
					 &thread_rpc_pnum);

	switch (cmd) {
		case OPTEE_RPC_CMD_LOAD_TA:
			res = TEE_ERROR_NOT_SUPPORTED;
			break;
		case OPTEE_RPC_CMD_RPMB:
			res = TEE_ERROR_NOT_SUPPORTED;
			break;
		case OPTEE_RPC_CMD_FS:
			res = handle_fs_op(num_params, params);
			break;
		case OPTEE_RPC_CMD_GET_TIME:
			res = TEE_ERROR_NOT_SUPPORTED;
			break;
		default:
			res = TEE_ERROR_NOT_SUPPORTED;
			break;
	}

	return res;
}