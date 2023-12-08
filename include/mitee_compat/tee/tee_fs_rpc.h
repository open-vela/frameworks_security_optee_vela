/*
 * Copyright (c) 2016, Linaro Limited
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

#ifndef TEE_FS_RPC_H
#define TEE_FS_RPC_H

#include <stdbool.h>
#include <stddef.h>
#include <tee_api_types.h>
#include <tee/tee_fs.h>

TEE_Result tee_fs_rpc_open(const char *file, bool create, int *fd);
TEE_Result tee_fs_rpc_close(int fd);
TEE_Result tee_fs_rpc_read(int fd, void *buf, size_t *size, int offs);
TEE_Result tee_fs_rpc_write(int fd, void *buf, size_t *size, int offs);
TEE_Result tee_fs_rpc_rename(const char * old_file, const char * new_file,
			bool overwrite);
TEE_Result tee_fs_rpc_remove(const char *file);
TEE_Result tee_fs_rpc_fsync(int fd);

#endif /* TEE_FS_RPC_H */
