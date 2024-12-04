/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2022, Linaro Limited.
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

#include <malloc.h>
#include <nuttx/mm/mm.h>
#include <stdlib.h>

void* raw_malloc(size_t hdr_size, size_t ftr_size, size_t pl_size,
    struct malloc_ctx* ctx)
{
    return malloc(pl_size);
}

void raw_free(void* ptr, struct malloc_ctx* ctx, bool wipe)
{
    free(ptr);
}

void* raw_calloc(size_t hdr_size, size_t ftr_size, size_t pl_nmemb,
    size_t pl_size, struct malloc_ctx* ctx)
{
    return calloc(pl_nmemb, ftr_size);
}

bool raw_malloc_buffer_overlaps_heap(struct malloc_ctx* ctx,
    void* buf, size_t len)
{
    return umm_heapmember(buf);
}

bool malloc_buffer_is_within_alloced(void* buf, size_t len)
{
    return umm_heapmember(buf);
}

void free_wipe(void* ptr)
{
    raw_free(ptr, NULL, true);
}

bool malloc_buffer_overlaps_heap(void* buf, size_t len)
{
    return umm_heapmember(buf);
}

void malloc_add_pool(void* buf, size_t len)
{
    raw_malloc_add_pool(NULL, buf, len);
}

size_t raw_malloc_get_ctx_size(void)
{
    return sizeof(struct malloc_ctx);
}

void raw_malloc_init_ctx(struct malloc_ctx* ctx) { }

void raw_malloc_add_pool(struct malloc_ctx* ctx, void* buf, size_t len) { }
