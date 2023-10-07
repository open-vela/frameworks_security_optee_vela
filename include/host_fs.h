/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * All rights reserved.
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

#ifndef HOST_FS_H
#define HOST_FS_H

#include <kernel/thread.h>
#include <tee_api_types.h>

TEE_Result host_fs_open(size_t num_params, struct thread_param *params);

TEE_Result host_fs_create(size_t num_params, struct thread_param *params);

TEE_Result host_fs_close(size_t num_params, struct thread_param *params);

TEE_Result host_fs_read(size_t num_params, struct thread_param *params);

TEE_Result host_fs_write(size_t num_params, struct thread_param *params);

TEE_Result host_fs_truncate(size_t num_params, struct thread_param *params);

TEE_Result host_fs_remove(size_t num_params, struct thread_param *params);

TEE_Result host_fs_rename(size_t num_params, struct thread_param *params);

#endif /* HOST_FS_H */
