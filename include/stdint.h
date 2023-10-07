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

/*
 * This file provides what C99 standard requires in
 * 7.18 interger types <stdint.h>
 */

#ifndef __STDINT_COMPAT_H
#define __STDINT_COMPAT_H

#include_next <stdint.h>

/*
 * 7.18.4 Macros for integer constants
 */

#ifdef __ASSEMBLER__
#define U(v)		v
#define UL(v)		v
#define ULL(v)		v
#define L(v)		v
#define LL(v)		v
#else
#define U(v)		v ## U
#define UL(v)		v ## UL
#define ULL(v)		v ## ULL
#define L(v)		v ## L
#define LL(v)		v ## LL
#endif

#endif /* __STDINT_COMPAT_H */
