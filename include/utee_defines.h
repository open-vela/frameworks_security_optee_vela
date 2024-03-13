/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2021, SumUp Services GmbH
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

#ifndef UTEE_DEFINES_COMPAT_H
#define UTEE_DEFINES_COMPAT_H

#include_next <utee_defines.h>

/* In devices that have already been deployed by using mitee, the
 * length of HUK is fixed at 16. If we set the length of HUK to be
 * the same as CONFIG_BOARDCTL_UNIQUEKEY_SIZE, then the FEK constructed
 * based on this HUK will not be able to decrypt the pre-set "TRIAD"
 * in the devices that have already been implemented using mitee.
 * Therefore, for devices that have already been released, it is
 * recommended to directly set the length of HWUNIQUE_KEY_LENGTH to 16
 * to ensure that they can work properly and avoid the problem of
 * being unable to decrypt the "TRIAD" in already deployed devices
 * that using mitee.
 */

#ifndef CONFIG_OPTEE_COMPAT_MITEE_FS

/* the HW_UNIQUE_KEY_LENGTH could be large than 16, so in order to compat
 * with the true device implementation, we need to keep the value of
 * HW_UNIQUE_KEY_LENGTH same as CONFIG_BOARDCTL_UNIQUEKEY_SIZE
 */

#if defined(CONFIG_BOARDCTL_UNIQUEKEY) && defined(HW_UNIQUE_KEY_LENGTH)
#undef HW_UNIQUE_KEY_LENGTH
#if CONFIG_BOARDCTL_UNIQUEKEY_SIZE <= 64
#define HW_UNIQUE_KEY_LENGTH     CONFIG_BOARDCTL_UNIQUEKEY_SIZE
#else
#error "the value of CONFIG_BOARDCTL_UNIQUEKEY_SIZE too large"
#endif
#endif

#endif

#endif /* UTEE_DEFINES_COMPAT_H */
