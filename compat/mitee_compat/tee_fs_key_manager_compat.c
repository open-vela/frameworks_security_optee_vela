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

#include <string.h>

#include <kernel/tee_ta_manager.h>
#include <tee/tee_fs_key_manager.h>
#include <tee/error_messages.h>
#include <mitee_crypt.h>

struct aad {
	const uint8_t *encrypted_key;
	const uint8_t *iv;
};

struct km_header {
	struct aad aad;
	uint8_t *tag;
};

static TEE_Result do_auth_enc(TEE_OperationMode mode,
		struct km_header *hdr,
		uint8_t *fek, int fek_len,
		const uint8_t *data_in, size_t in_size,
		uint8_t *data_out, size_t *out_size)
{
#ifdef FS_PLAINTEXT
	DMSG("WARNING: plaintext fs\n");
	memcpy(data_out,data_in,in_size);
	*out_size = in_size;
	return TEE_SUCCESS;
#else
	TEE_Result res = TEE_SUCCESS;
	void *ctx = NULL;
	size_t tag_len = TEE_FS_KM_MAX_TAG_LEN;

	if ((mode != TEE_MODE_ENCRYPT) && (mode != TEE_MODE_DECRYPT)) {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (*out_size < in_size) {
		EMSG(ERR_MSG_SHORT_BUFFER ": %zd, %zd\n", *out_size, in_size);
		return TEE_ERROR_SHORT_BUFFER;
	}

	res = mitee_crypto_authenc_alloc_ctx(&ctx, TEE_FS_KM_AUTH_ENC_ALG);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	res = mitee_crypto_authenc_init(ctx, TEE_FS_KM_AUTH_ENC_ALG,
			mode, fek, fek_len, hdr->aad.iv,
			TEE_FS_KM_IV_LEN, TEE_FS_KM_MAX_TAG_LEN,
			sizeof(struct aad), in_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	res = mitee_crypto_authenc_update_aad(ctx, TEE_FS_KM_AUTH_ENC_ALG,
			mode, (uint8_t *)hdr->aad.encrypted_key,
			TEE_FS_KM_FEK_SIZE);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	res = mitee_crypto_authenc_update_aad(ctx, TEE_FS_KM_AUTH_ENC_ALG,
			mode, (uint8_t *)hdr->aad.iv,
			TEE_FS_KM_IV_LEN);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	if (mode == TEE_MODE_ENCRYPT) {
		res = mitee_crypto_authenc_enc_final(ctx, TEE_FS_KM_AUTH_ENC_ALG,
				data_in, in_size, data_out, out_size,
				hdr->tag, &tag_len);
	} else {
		res = mitee_crypto_authenc_dec_final(ctx, TEE_FS_KM_AUTH_ENC_ALG,
				data_in, in_size, data_out, out_size,
				hdr->tag, tag_len);
	}

	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	mitee_crypto_authenc_final(ctx, TEE_FS_KM_AUTH_ENC_ALG);

exit:
	mitee_crypto_authenc_free_ctx(ctx, TEE_FS_KM_AUTH_ENC_ALG);

	if (res) {
		DMSG("res: 0x%08lx", res);
	}
	return res;
#endif
}

static TEE_Result generate_iv(uint8_t *iv, uint8_t len)
{
#ifdef FS_PLAINTEXT
	DMSG("WARNING: plaintext fs\n");
	memset(iv, 0, len);
	return TEE_SUCCESS;
#else
	return crypto_rng_read(iv, len);
#endif
}

size_t tee_fs_get_header_size(enum tee_fs_file_type type)
{
	size_t header_size = 0;

	switch (type) {
	case META_FILE:
		header_size = sizeof(struct meta_header);
		break;
	case BLOCK_FILE:
		header_size = sizeof(struct block_header);
		break;
	default:
		EMSG(ERR_MSG_ITEM_NOT_FOUND ": 0x%08x\n", type);
	}
	return header_size;
}

TEE_Result tee_fs_encrypt_file(enum tee_fs_file_type file_type,
		const uint8_t *data_in, size_t data_in_size,
		uint8_t *data_out, size_t *data_out_size,
		const uint8_t *encrypted_fek)
{
	TEE_Result res = TEE_SUCCESS;
	struct km_header hdr;
	uint8_t iv[TEE_FS_KM_IV_LEN];
	uint8_t tag[TEE_FS_KM_MAX_TAG_LEN];
	uint8_t fek[TEE_FS_KM_FEK_SIZE];
	uint8_t *ciphertext;
	size_t cipher_size;
	size_t header_size = tee_fs_get_header_size(file_type);

	/*
	 * Meta File Format: |Header|Chipertext|
	 * Header Format:    |AAD|Tag|
	 * AAD Format:       |Encrypted_FEK|IV|
	 *
	 * Block File Format: |Header|Ciphertext|
	 * Header Format:     |IV|Tag|
	 *
	 * TSK = HMAC(SSK, TA_UUID)
	 * FEK = AES_DECRYPT(TSK, Encrypted_FEK)
	 * Chipertext = AES_GCM_ENCRYPT(FEK, IV, Meta_Info, AAD)
	 */

	if (*data_out_size != (header_size + data_in_size)) {
		EMSG(ERR_MSG_SHORT_BUFFER "\n");
		return TEE_ERROR_SHORT_BUFFER;
	}

	res = generate_iv(iv, TEE_FS_KM_IV_LEN);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto fail;
	}
#ifdef DEBUG_KEY_MANAGER
	dump_buf("WARNING: iv", iv, sizeof(iv));
#endif
	struct ts_session *ts_sess = ts_get_current_session();

	res = tee_fs_fek_crypt(&ts_sess->ctx->uuid, TEE_MODE_DECRYPT, encrypted_fek,
			       TEE_FS_KM_FEK_SIZE, fek);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto fail;
	}

	ciphertext = data_out + header_size;
	cipher_size = data_in_size;

	hdr.aad.iv = iv;
	hdr.aad.encrypted_key = encrypted_fek;
	hdr.tag = tag;

	res = do_auth_enc(TEE_MODE_ENCRYPT, &hdr,
			fek, TEE_FS_KM_FEK_SIZE,
			data_in, data_in_size,
			ciphertext, &cipher_size);

	if (res == TEE_SUCCESS) {
		if (file_type == META_FILE) {
			memcpy(data_out, encrypted_fek, TEE_FS_KM_FEK_SIZE);
			data_out += TEE_FS_KM_FEK_SIZE;
		}

		memcpy(data_out, iv, TEE_FS_KM_IV_LEN);
		data_out += TEE_FS_KM_IV_LEN;
		memcpy(data_out, tag, TEE_FS_KM_MAX_TAG_LEN);

		*data_out_size = header_size + cipher_size;
		DMSG("output cipher data size: %zd\n", *data_out_size);
	}

fail:
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result tee_fs_decrypt_file(enum tee_fs_file_type file_type,
		const uint8_t *data_in, size_t data_in_size,
		uint8_t *plaintext, size_t *plaintext_size,
		uint8_t *encrypted_fek)
{
	TEE_Result res = TEE_SUCCESS;
	struct km_header km_hdr;
	size_t file_hdr_size = tee_fs_get_header_size(file_type);
	const uint8_t *cipher = data_in + file_hdr_size;
	int cipher_size = data_in_size - file_hdr_size;
	uint8_t fek[TEE_FS_KM_FEK_SIZE];

	if (file_type == META_FILE) {
		DMSG("meta file\n");
		struct meta_header *hdr = (struct meta_header *)data_in;

		km_hdr.aad.encrypted_key = hdr->encrypted_key;
		km_hdr.aad.iv = hdr->common.iv;
		km_hdr.tag = hdr->common.tag;

		memcpy(encrypted_fek, hdr->encrypted_key, TEE_FS_KM_FEK_SIZE);
	} else {
		DMSG("block file\n");
		struct block_header *hdr = (struct block_header *)data_in;

		km_hdr.aad.encrypted_key = encrypted_fek;
		km_hdr.aad.iv = hdr->common.iv;
		km_hdr.tag = hdr->common.tag;
	}

	struct ts_session *ts_sess = ts_get_current_session();

	res = tee_fs_fek_crypt(&ts_sess->ctx->uuid, TEE_MODE_DECRYPT, km_hdr.aad.encrypted_key,
			       TEE_FS_KM_FEK_SIZE, fek);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	return do_auth_enc(TEE_MODE_DECRYPT, &km_hdr, fek, TEE_FS_KM_FEK_SIZE,
			cipher, cipher_size, plaintext, plaintext_size);
}
