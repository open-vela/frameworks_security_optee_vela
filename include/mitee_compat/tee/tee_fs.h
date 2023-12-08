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

#ifndef TEE_FS_H
#define TEE_FS_H

#include <stddef.h>
#include <stdint.h>
#include <tee_api_types.h>

#define TEE_FS_NAME_MAX 350

typedef int64_t tee_fs_off_t;
typedef uint32_t tee_fs_mode_t;

struct tee_fs_dirent {
	char *d_name;
};

struct tee_fs_dir;
struct tee_file_handle;

/*
 * tee_fs implements a POSIX like secure file system with GP extension
 */
struct tee_file_operations {
	TEE_Result (*open)(const char *name, struct tee_file_handle **fh);
	TEE_Result (*create)(const char *name, struct tee_file_handle **fh);
	void (*close)(struct tee_file_handle **fh);
	TEE_Result (*read)(struct tee_file_handle *fh, void *buf, size_t *len);
	TEE_Result (*write)(struct tee_file_handle *fh, const void *buf,
			    size_t len);
	TEE_Result (*seek)(struct tee_file_handle *fh, int32_t offs,
			   TEE_Whence whence, int32_t *new_offs);
	TEE_Result (*rename)(const char *old_name, const char *new_name,
			     bool overwrite);
	TEE_Result (*remove)(const char *name);
	TEE_Result (*truncate)(struct tee_file_handle *fh, size_t size);

	TEE_Result (*opendir)(const char *name, struct tee_fs_dir **d);
	TEE_Result (*readdir)(struct tee_fs_dir *d, struct tee_fs_dirent **ent);
	void (*closedir)(struct tee_fs_dir *d);
	TEE_Result (*fsync)(struct tee_file_handle **fh);
};

extern const struct tee_file_operations ree_fs_ops;

#endif /* TEE_FS_H */
