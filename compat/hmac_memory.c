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

#include <ctype.h>
#include <mbedtls/md.h>
#include <hmac_memory.h>
#include <md_wrap.h>

#define MAX_HASH_NAME_LEN 32

int find_hash(const char *name)
{
	if (!name) {
		return -1;
	}
	char uc_name[MAX_HASH_NAME_LEN];
	int i = 0;
	do {
		uc_name[i++] = toupper(*name);
	} while (*name++);
	const mbedtls_md_info_t *info = mbedtls_md_info_from_string(uc_name);
	if (!info) {
		return -1;
	}
	return info->type;
}

/*
 * HMAC a block of memory to produce the authentication tag
 */
int hmac_memory(int md_type,
		const unsigned char *key, unsigned long keylen,
		const unsigned char *in, unsigned long inlen,
		unsigned char *out, unsigned long *outlen)
{
	const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
	if (!md_info) {
		return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
	}
	*outlen = mbedtls_md_get_size(md_info);
	return mbedtls_md_hmac(md_info, key,
				keylen, in, inlen, out);
}
