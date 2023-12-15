/*
 * Copyright (c) 2019, Linaro Limited
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

#include <config.h>
#include <crypto/crypto.h>
#include <kernel/huk_subkey.h>
#include <kernel/tee_common_otp.h>
#include <string_ext.h>
#include <tee/tee_fs_key_manager.h>

static uint8_t string_for_ssk_gen[] = "ONLY_FOR_tee_fs_ssk";

static TEE_Result do_hmac(void *out_key, size_t out_key_size,
			  const void *in_key, size_t in_key_size,
			  const void *message, size_t message_size)
{
	TEE_Result res;
	void *ctx = NULL;

	if (!out_key || !in_key || !message)
		return TEE_ERROR_BAD_PARAMETERS;

	res = crypto_mac_alloc_ctx(&ctx, TEE_FS_KM_HMAC_ALG);
	if (res != TEE_SUCCESS)
		return res;

	res = crypto_mac_init(ctx, in_key, in_key_size);
	if (res != TEE_SUCCESS)
		goto exit;

	res = crypto_mac_update(ctx, message, message_size);
	if (res != TEE_SUCCESS)
		goto exit;

	res = crypto_mac_final(ctx, out_key, out_key_size);
	if (res != TEE_SUCCESS)
		goto exit;

	res = TEE_SUCCESS;

exit:
	crypto_mac_free_ctx(ctx);
	return res;
}

TEE_Result huk_subkey_derive(enum huk_subkey_usage usage __unused,
			     const void *const_data, size_t const_data_len,
			     uint8_t *subkey, size_t subkey_len)
{
	TEE_Result res = TEE_SUCCESS;

	/* the following implementation are using to compat with mitee
	 * implementation.
	 * there are 2 difference between optee_os and mitee in huk_subkey
	 * implementation:
	 * 1. the key do not calculated based on hub_subkey_usage
	 * 2. the key construction is different, in optee, the huk is only
	 * construct by the device hw unique key, in mitee, the huk is construct
	 * with two parts
	 */
	struct tee_hw_unique_key huk;
	uint8_t chip_id[TEE_FS_KM_CHIP_ID_LENGTH];
	uint8_t message[sizeof(chip_id) + sizeof(string_for_ssk_gen)];

	/* Secure Storage Key Generation:
	 *
	 *     SSK = HMAC(HUK, message)
	 *     message := concatenate(chip_id, static string)
	 */
	tee_otp_get_hw_unique_key(&huk);
	tee_otp_get_die_id(chip_id, sizeof(chip_id));

	memcpy(message, chip_id, sizeof(chip_id));
	memcpy(message + sizeof(chip_id), string_for_ssk_gen,
			sizeof(string_for_ssk_gen));

	res = do_hmac(subkey, subkey_len,
			huk.data, sizeof(huk.data),
			message, sizeof(message));

	return res;
}
