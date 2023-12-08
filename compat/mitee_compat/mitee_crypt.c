/*
 * Copyright (c) 2014, Linaro Limited
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

#include <assert.h>
#include <crypto/crypto_impl.h>
#include <crypto/crypto.h>
#include <stdlib.h>
#include <string.h>
#include <tee_api_types.h>
#include <tee/tee_cryp_utl.h>
#include <trace.h>
#include <utee_defines.h>
#include <util.h>
#include <mitee_crypt.h>

#if defined(CFG_WITH_VFP)
#include <tomcrypt_arm_neon.h>
#include <kernel/thread.h>
#endif

#include <string_ext.h>

#define CRYPT_OK		0

static TEE_Result crypto_aes_gcm_init(void *ctx, TEE_OperationMode mode,
		const uint8_t *key, size_t key_len,
		const uint8_t *nonce, size_t nonce_len,
		size_t tag_len, size_t aad_len, size_t payload_len)
{
	return crypto_authenc_init(ctx, mode, key, key_len, nonce,
		nonce_len, tag_len, aad_len, payload_len);
}

static TEE_Result crypto_aes_gcm_update_aad(void *ctx,
		TEE_OperationMode mode __unused,
		const uint8_t *data, size_t len)
{
	return crypto_authenc_update_aad(ctx, mode, data, len);
}

static TEE_Result crypto_aes_gcm_update_payload(void *ctx, TEE_OperationMode mode,
		const uint8_t *src_data,
		size_t src_len, uint8_t *dst_data, size_t *dst_len)
{
	return crypto_authenc_update_payload(ctx, mode, src_data,
		src_len, dst_data, dst_len);
}

static TEE_Result crypto_aes_gcm_enc_final(void *ctx, const uint8_t *src_data,
		size_t src_len, uint8_t *dst_data, size_t *dst_len,
		uint8_t *dst_tag, size_t *dst_tag_len)
{
	return crypto_authenc_enc_final(ctx, src_data, src_len,
		dst_data, dst_len, dst_tag, dst_tag_len);
}

static TEE_Result crypto_aes_gcm_dec_final(void *ctx, const uint8_t *src_data,
		size_t src_len, uint8_t *dst_data, size_t *dst_len,
		const uint8_t *tag, size_t tag_len)
{
	return crypto_authenc_dec_final(ctx, src_data, src_len,
		dst_data, dst_len, tag, tag_len);
}

static void crypto_aes_gcm_final(void *ctx)
{
	crypto_authenc_final(ctx);
}

static void crypto_aes_gcm_free_ctx(void *ctx)
{
	crypto_authenc_free_ctx(ctx);
}

TEE_Result mitee_crypto_authenc_alloc_ctx(void **ctx, uint32_t algo)
{
	TEE_Result res = TEE_SUCCESS;
	void *c = NULL;

	switch (algo) {
		case TEE_ALG_AES_GCM:
			res = crypto_aes_gcm_alloc_ctx((struct crypto_authenc_ctx **) &c);
			break;
		default:
			return TEE_ERROR_NOT_IMPLEMENTED;
	}

	if (!res)
		*ctx = c;

	return res;
}

TEE_Result mitee_crypto_authenc_init(void *ctx, uint32_t algo __unused,
		TEE_OperationMode mode, const uint8_t *key, size_t key_len,
		const uint8_t *nonce, size_t nonce_len,
		size_t tag_len, size_t aad_len, size_t payload_len)
{
	return crypto_aes_gcm_init(ctx, mode, key, key_len, nonce,
		nonce_len, tag_len, aad_len, payload_len);
}

TEE_Result mitee_crypto_authenc_update_aad(void *ctx, uint32_t algo __unused,
			TEE_OperationMode mode, const uint8_t *data, size_t len)
{
	return crypto_aes_gcm_update_aad(ctx, mode, data, len);
}

TEE_Result mitee_crypto_authenc_update_payload(
			void *ctx, uint32_t algo __unused,
			TEE_OperationMode mode, const uint8_t *src_data,
			size_t src_len, uint8_t *dst_data, size_t *dst_len)
{
	if (*dst_len < src_len)
		return TEE_ERROR_SHORT_BUFFER;

	*dst_len = src_len;
	return crypto_aes_gcm_update_payload(ctx, mode,
		src_data, src_len, dst_data, dst_len);
}

TEE_Result mitee_crypto_authenc_enc_final(void *ctx, uint32_t algo __unused,
			const uint8_t *src_data, size_t src_len,
			uint8_t *dst_data, size_t *dst_len,
			uint8_t *dst_tag, size_t *dst_tag_len)
{
	if (*dst_len < src_len)
		return TEE_ERROR_SHORT_BUFFER;

	*dst_len = src_len;
	return crypto_aes_gcm_enc_final(ctx, src_data, src_len,
			dst_data, dst_len, dst_tag, dst_tag_len);
}

TEE_Result mitee_crypto_authenc_dec_final(
		void *ctx, uint32_t algo __unused,
		const uint8_t *src_data, size_t src_len,
		uint8_t *dst_data, size_t *dst_len,
		const uint8_t *tag, size_t tag_len)
{
	if (*dst_len < src_len)
		return TEE_ERROR_SHORT_BUFFER;
	*dst_len = src_len;

	return crypto_aes_gcm_dec_final(ctx, src_data, src_len,
		dst_data, dst_len, tag, tag_len);
}

void mitee_crypto_authenc_final(void *ctx, uint32_t algo __unused)
{
	crypto_aes_gcm_final(ctx);
}

void mitee_crypto_authenc_free_ctx(void *ctx, uint32_t algo __unused)
{
	if (ctx)
		crypto_aes_gcm_free_ctx(ctx);
}

void mitee_crypto_authenc_copy_state(void *dst_ctx, void *src_ctx,
		uint32_t algo __unused)
{
	crypto_aes_gcm_copy_state(dst_ctx, src_ctx);
}

#if defined(CFG_WITH_VFP)
void tomcrypt_arm_neon_enable(struct tomcrypt_arm_neon_state *state)
{
	state->state = thread_kernel_enable_vfp();
}

void tomcrypt_arm_neon_disable(struct tomcrypt_arm_neon_state *state)
{
	thread_kernel_disable_vfp(state->state);
}
#endif

#if defined(CFG_CRYPTO_SHA256)
TEE_Result hash_sha256_check(const uint8_t *hash, const uint8_t *data,
		size_t data_size)
{
	hash_state hs;
	uint8_t digest[TEE_SHA256_HASH_SIZE];

	if (mitee_sha256_init(&hs) != CRYPT_OK)
		return TEE_ERROR_GENERIC;
	if (mitee_sha256_process(&hs, data, data_size) != CRYPT_OK)
		return TEE_ERROR_GENERIC;
	if (mitee_sha256_done(&hs, digest) != CRYPT_OK)
		return TEE_ERROR_GENERIC;
	if (buf_compare_ct(digest, hash, sizeof(digest)) != 0)
		return TEE_ERROR_SECURITY;
	return TEE_SUCCESS;
}
#endif

TEE_Result rng_generate(void *buffer, size_t len)
{
#if defined(CFG_WITH_SOFTWARE_PRNG)
#ifdef _CFG_CRYPTO_WITH_FORTUNA_PRNG
	int (*start)(prng_state *) = fortuna_start;
	int (*ready)(prng_state *) = fortuna_ready;
	unsigned long (*read)(unsigned char *, unsigned long, prng_state *) =
		fortuna_read;
#else
	int (*start)(prng_state *) = rc4_start;
	int (*ready)(prng_state *) = rc4_ready;
	unsigned long (*read)(unsigned char *, unsigned long, prng_state *) =
		rc4_read;
#endif

	if (!_tee_ltc_prng.inited) {
		if (start(&_tee_ltc_prng.state) != CRYPT_OK)
			return TEE_ERROR_BAD_STATE;
		plat_prng_add_jitter_entropy_norpc();
		if (ready(&_tee_ltc_prng.state) != CRYPT_OK)
			return TEE_ERROR_BAD_STATE;
		_tee_ltc_prng.inited = true;
	}
	if (read(buffer, len, &_tee_ltc_prng.state) != len)
		return TEE_ERROR_BAD_STATE;
	return TEE_SUCCESS;


#else
	return get_rng_array(buffer, len);
#endif
}
