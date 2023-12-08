/*
 * Copyright (c) 2015, Linaro Limited
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

#ifndef TEE_FS_KEY_MANAGER_H
#define TEE_FS_KEY_MANAGER_H

#include <tee_api_types.h>
#include <utee_defines.h>

#define TEE_FS_KM_CHIP_ID_LENGTH    32
#define TEE_FS_KM_HMAC_ALG          TEE_ALG_HMAC_SHA256
#define TEE_FS_KM_AUTH_ENC_ALG      TEE_ALG_AES_GCM
#define TEE_FS_KM_ENC_FEK_ALG       TEE_ALG_AES_ECB_NOPAD
#define TEE_FS_KM_SSK_SIZE          TEE_SHA256_HASH_SIZE
#define TEE_FS_KM_TSK_SIZE          TEE_SHA256_HASH_SIZE
#define TEE_FS_KM_FEK_SIZE          16  /* bytes */
#define TEE_FS_KM_IV_LEN            12  /* bytes */
#define TEE_FS_KM_MAX_TAG_LEN       16  /* bytes */

#define NUM_BLOCKS_PER_FILE         32

enum tee_fs_file_type {
	META_FILE,
	BLOCK_FILE
};

struct tee_fs_file_info {
	uint64_t length;
	uint32_t backup_version_table[NUM_BLOCKS_PER_FILE / 32];
};

struct tee_fs_file_meta {
	struct tee_fs_file_info info;
	uint8_t encrypted_fek[TEE_FS_KM_FEK_SIZE];
	uint32_t counter;
};

struct common_header {
	uint8_t iv[TEE_FS_KM_IV_LEN];
	uint8_t tag[TEE_FS_KM_MAX_TAG_LEN];
};

struct meta_header {
	uint8_t encrypted_key[TEE_FS_KM_FEK_SIZE];
	struct common_header common;
};

struct block_header {
	struct common_header common;
};

size_t tee_fs_get_header_size(enum tee_fs_file_type type);
TEE_Result tee_fs_generate_fek(const TEE_UUID *uuid, void *buf,
		size_t buf_size);
TEE_Result tee_fs_encrypt_file(enum tee_fs_file_type file_type,
		const uint8_t *plaintext, size_t plaintext_size,
		uint8_t *ciphertext, size_t *ciphertext_size,
		const uint8_t *encrypted_fek);
TEE_Result tee_fs_decrypt_file(enum tee_fs_file_type file_type,
		const uint8_t *data_in, size_t data_in_size,
		uint8_t *plaintext, size_t *plaintext_size,
		uint8_t *encrypted_fek);
TEE_Result tee_fs_crypt_block(const TEE_UUID *uuid, uint8_t *out,
		const uint8_t *in, size_t size, uint16_t blk_idx,
		const uint8_t *encrypted_fek, TEE_OperationMode mode);

TEE_Result tee_fs_fek_crypt(const TEE_UUID *uuid, TEE_OperationMode mode,
		const uint8_t *in_key, size_t size, uint8_t *out_key);

#endif /* TEE_FS_KEY_MANAGER_H */
