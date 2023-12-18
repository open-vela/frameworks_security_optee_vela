/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
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

#ifndef TEE_OTP_COMPAT_H
#define TEE_OTP_CMOPAT_H

#include <kernel/tee_common_otp.h>

TEE_Result tee_otp_get_hw_unique_key_compat(struct tee_hw_unique_key *hwkey,
			uint8_t *default_key);
int tee_otp_get_die_id_compat(uint8_t *buffer, size_t len, uint8_t default_key);

#endif /* TEE_OTP_CMOPAT_H */
