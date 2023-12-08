/*
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

#include <kernel/tee_common_otp.h>
#include <sys/param.h>

#if defined(CFG_OTP_SUPPORT)
#include <sys/boardctl.h>

/* this method is override from otp_stubs.c#tee_otp_get_hw_unique_key()
 * the tee will use the following the implementation only when CFG_OTP_SUPPORT
 * configuration is enabled
 */
TEE_Result tee_otp_get_hw_unique_key(struct tee_hw_unique_key *hwkey)
{
	uint8_t tmp_key[CONFIG_BOARDCTL_UNIQUEKEY_SIZE];
	int res = boardctl(BOARDIOC_UNIQUEKEY, (uintptr_t)tmp_key);
#if defined(CONFIG_OPTEE_COMPAT_MITEE_FS) && defined(CFG_OTP_SUPPORT_NO_PROVISION_TMP)
	{
		uint8_t no_provision[HW_UNIQUE_KEY_LENGTH];

		memset(no_provision, 0xFF, sizeof(no_provision));

		if(!memcmp(tmp_key, no_provision, HW_UNIQUE_KEY_LENGTH)) { //first 16 bytes
			/* set default huk */
			memset(&hwkey->data[0], 0, sizeof(hwkey->data));
		} else {
			memcpy(&hwkey->data[0], tmp_key, sizeof(hwkey->data));
		}
	}
#else
	memset(hwkey, 0, sizeof(struct tee_hw_unique_key));
	memcpy(&hwkey->data[0], tmp_key,
	       MIN(CONFIG_BOARDCTL_UNIQUEKEY_SIZE, sizeof(hwkey->data)));
#endif

	return res >= 0 ? TEE_SUCCESS : TEE_ERROR_GENERIC;
}

#ifdef CONFIG_BOARDCTL_UNIQUEID

int tee_otp_get_die_id(uint8_t *buffer, size_t len)
{
	int res;
	uint8_t tmp[CONFIG_BOARDCTL_UNIQUEID_SIZE];

	memset(buffer, 0, len);
	res = boardctl(BOARDIOC_UNIQUEID, (uintptr_t)tmp);
#if defined(CONFIG_OPTEE_COMPAT_MITEE_FS) && defined(CFG_OTP_SUPPORT_NO_PROVISION_TMP)
	size_t i;

	char pattern[4] = { 'B', 'E', 'E', 'F' };
	for (i = 0; i < len; i++) {
		buffer[i] = pattern[i % 4];
	}
#else
	memcpy(buffer, tmp, MIN(CONFIG_BOARDCTL_UNIQUEID_SIZE, len));
#endif

	return res >= 0 ? TEE_SUCCESS : TEE_ERROR_GENERIC;
}

#endif /* CONFIG_BOARDCTL_UNIQUEID */

#endif /* CFG_OTP_SUPPORT */
