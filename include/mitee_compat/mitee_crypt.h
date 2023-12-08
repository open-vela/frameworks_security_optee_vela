/*
 * Copyright (c) 2016-2022, Linaro Limited
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

#ifndef MITEE_CRYP_H
#define MITEE_CRYP_H

#include <tee_api_types.h>
#include <crypto/crypto.h>

/*
 * Verifies a SHA-256 hash, doesn't require tee_cryp_init() to be called in
 * advance and has as few dependencies as possible.
 *
 * This function is primarily used by pager and early initialization code
 * where the complete crypto library isn't available.
 */
TEE_Result hash_sha256_check(const uint8_t *hash, const uint8_t *data,
			size_t data_size);

TEE_Result rng_generate(void *buffer, size_t len);

TEE_Result get_rng_array(void *buffer, int len);

void crypto_aes_gcm_copy_state(void *dst_ctx, void *src_ctx);

TEE_Result mitee_crypto_authenc_alloc_ctx(void **ctx, uint32_t algo);

TEE_Result mitee_crypto_authenc_init(void *ctx, uint32_t algo,
			TEE_OperationMode mode,
			const uint8_t *key, size_t key_len,
			const uint8_t *nonce, size_t nonce_len,
			size_t tag_len, size_t aad_len,
			size_t payload_len);

TEE_Result mitee_crypto_authenc_update_aad(void *ctx, uint32_t algo,
			TEE_OperationMode mode,
			const uint8_t *data, size_t len);

TEE_Result mitee_crypto_authenc_update_payload(
			void *ctx, uint32_t algo, TEE_OperationMode mode,
			const uint8_t *src_data, size_t src_len, uint8_t *dst_data,
			size_t *dst_len);

TEE_Result mitee_crypto_authenc_enc_final(void *ctx, uint32_t algo,
			const uint8_t *src_data, size_t src_len,
			uint8_t *dst_data, size_t *dst_len,
			uint8_t *dst_tag, size_t *dst_tag_len);

TEE_Result mitee_crypto_authenc_dec_final(void *ctx, uint32_t algo,
			const uint8_t *src_data, size_t src_len,
			uint8_t *dst_data, size_t *dst_len,
			const uint8_t *tag, size_t tag_len);

void mitee_crypto_authenc_final(void *ctx, uint32_t algo);

void mitee_crypto_authenc_free_ctx(void *ctx, uint32_t algo);

#endif /* MITEE_CRYP_H */
