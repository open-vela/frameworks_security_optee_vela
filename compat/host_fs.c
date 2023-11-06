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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <host_fs.h>
#include <initcall.h>
#include <libgen.h>
#include <mm/mobj.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <tee/tee_fs.h>
#include <tee/tee_fs_rpc.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 255
#endif

/* Path to all secure storage files. */
static char tee_fs_root[PATH_MAX];

static void fs_fsync(void)
{
	int fd = 0;

	fd = open(tee_fs_root, O_RDONLY | O_DIRECTORY);
	if (fd > 0) {
		fsync(fd);
		close(fd);
	}
}

static int do_mkdir(const char *path, mode_t mode)
{
	struct stat st;

	memset(&st, 0, sizeof(st));

	if (mkdir(path, mode) != 0 && errno != EEXIST)
		return -1;

	if (stat(path, &st) != 0 && !S_ISDIR(st.st_mode))
		return -1;

	fs_fsync();
	return 0;
}

static int mkpath(const char *path, mode_t mode)
{
	int status = 0;
	char *subpath = strdup(path);
	char *prev = subpath;
	char *curr = NULL;

	while (status == 0 && (curr = strchr(prev, '/')) != 0) {
		/*
		 * Check for root or double slash
		 */
		if (curr != prev) {
			*curr = '\0';
			status = do_mkdir(subpath, mode);
			*curr = '/';
		}
		prev = curr + 1;
	}
	if (status == 0)
		status = do_mkdir(path, mode);

	free(subpath);
	return status;
}

static TEE_Result errno_to_tee(int err)
{
	switch (err) {
	case ENOSPC:
		return TEE_ERROR_STORAGE_NO_SPACE;
	case ENOENT:
		return TEE_ERROR_ITEM_NOT_FOUND;
	default:
		break;
	}
	return TEE_ERROR_GENERIC;
}

static TEE_Result host_fs_init(void)
{
	size_t n = 0;
	mode_t mode = 0700;
	if (tee_fs_root[0]) {
		return TEE_SUCCESS;
	}

	n = snprintf(tee_fs_root, sizeof(tee_fs_root), "%s/", HOST_FS_PARENT_PATH);

	if (n >= sizeof(tee_fs_root))
		return TEE_ERROR_NOT_SUPPORTED;

	if (mkpath(tee_fs_root, mode) != 0)
		return TEE_ERROR_NOT_SUPPORTED;

	return TEE_SUCCESS;
}

static size_t tee_fs_get_absolute_filename(char *file, char *out,
					   size_t out_size)
{
	int s = 0;

	if (!file || !out || (out_size <= strlen(tee_fs_root) + 1))
		return 0;

	s = snprintf(out, out_size, "%s%s", tee_fs_root, file);
	if (s < 0 || (size_t)s >= out_size)
		return 0;

	/* Safe to cast since we have checked that sizes are OK */
	return (size_t)s;
}

static int open_wrapper(const char *fname, int flags)
{
	int fd = 0;
	while (true) {
		fd = open(fname, flags | O_SYNC, 0600);
		if (fd >= 0 || errno != EINTR)
			return fd;
	}
}

static bool param_is_memref(struct thread_param *param)
{
	switch (param->attr) {
		case THREAD_PARAM_ATTR_MEMREF_IN:
		case THREAD_PARAM_ATTR_MEMREF_OUT:
		case THREAD_PARAM_ATTR_MEMREF_INOUT:
			return true;
		default:
			return false;
	}
}

static void *convert_param_to_va(struct thread_param *param)
{
	if (!param_is_memref(param)) {
		return NULL;
	}

	return param->u.memref.mobj->buffer + param->u.memref.offs;
}

static size_t get_memsize_from_param(struct thread_param *param)
{
	if (!param_is_memref(param)) {
		return 0;
	}

	return param->u.memref.size;
}

static void set_memsize(struct thread_param *param, size_t new_size)
{
	if (!param_is_memref(param)) {
		return;
	}

	param->u.memref.size = new_size;
}

TEE_Result host_fs_open(size_t num_params, struct thread_param *params)
{
	char abs_filename[PATH_MAX] = { 0 };
	char *fname = NULL;
	int fd = 0;

	if (num_params != 3 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_IN ||
		params[2].attr != THREAD_PARAM_ATTR_VALUE_OUT) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fname = convert_param_to_va(&params[1]);
	if (!fname) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (!tee_fs_get_absolute_filename(fname, abs_filename,
					  sizeof(abs_filename)))
		return TEE_ERROR_BAD_PARAMETERS;

	fd = open_wrapper(abs_filename, O_RDWR);
	if (fd < 0) {
		/*
		 * In case the problem is the filesystem is RO, retry with the
		 * open flags restricted to RO.
		 */
		fd = open_wrapper(abs_filename, O_RDONLY);
		if (fd < 0) {
			return TEE_ERROR_ITEM_NOT_FOUND;
		}
	}

	params[2].u.value.a = fd;
	return TEE_SUCCESS;
}

TEE_Result host_fs_create(size_t num_params, struct thread_param *params)
{
	char abs_filename[PATH_MAX] = { 0 };
	char abs_dir[PATH_MAX] = { 0 };
	char *fname = NULL;
	char *d = NULL;
	int fd = 0;
	const int flags = O_RDWR | O_CREAT | O_TRUNC;

	if (num_params != 3 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_IN ||
		params[2].attr != THREAD_PARAM_ATTR_VALUE_OUT) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fname = convert_param_to_va(&params[1]);
	if (!fname)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!tee_fs_get_absolute_filename(fname, abs_filename,
					  sizeof(abs_filename)))
		return TEE_ERROR_BAD_PARAMETERS;

	fd = open_wrapper(abs_filename, flags);
	if (fd >= 0)
		goto out;
	if (errno != ENOENT)
		return errno_to_tee(errno);

	/* Directory for file missing, try make to it */
	strncpy(abs_dir, abs_filename, sizeof(abs_dir));
	abs_dir[sizeof(abs_dir) - 1] = '\0';
	d = dirname(abs_dir);
	if (!mkdir(d, 0700)) {
		int err = 0;

		fd = open_wrapper(abs_filename, flags);
		if (fd >= 0)
			goto out;

		/*
		 * The directory was made but the file could still not be
		 * created.
		 */
		err = errno;
		rmdir(d);
		return errno_to_tee(err);
	}
	if (errno != ENOENT)
		return errno_to_tee(errno);

	/* Parent directory for file missing, try to make it */
	d = dirname(d);
	if (mkdir(d, 0700))
		return errno_to_tee(errno);

	/* Try to make directory for file again */
	strncpy(abs_dir, abs_filename, sizeof(abs_dir));
	abs_dir[sizeof(abs_dir) - 1] = '\0';
	d = dirname(abs_dir);
	if (mkdir(d, 0700)) {
		int err = errno;

		d = dirname(d);
		rmdir(d);
		return errno_to_tee(err);
	}

	fd = open_wrapper(abs_filename, flags);
	if (fd < 0) {
		int err = errno;

		rmdir(d);
		d = dirname(d);
		rmdir(d);
		return errno_to_tee(err);
	}

out:
	fs_fsync();
	params[2].u.value.a = fd;
	return TEE_SUCCESS;
}

TEE_Result host_fs_close(size_t num_params, struct thread_param *params)
{
	int fd = 0;

	if (num_params != 1 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fd = params[0].u.value.b;
	while (close(fd)) {
		if (errno != EINTR)
			return errno_to_tee(errno);
	}
	return TEE_SUCCESS;
}

TEE_Result host_fs_read(size_t num_params, struct thread_param *params)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	off_t offs = 0;
	int fd = 0;
	ssize_t r = 0;
	size_t s = 0;

	if (num_params != 2 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_OUT) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fd = params[0].u.value.b;
	offs = params[0].u.value.c;

	buf = convert_param_to_va(&params[1]);

	if (!buf)
		return TEE_ERROR_BAD_PARAMETERS;
	len = get_memsize_from_param(&params[1]);

	s = 0;
	r = -1;
	while (r && len) {
		r = pread(fd, buf, len, offs);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return errno_to_tee(errno);
		}
		assert((size_t)r <= len);
		buf += r;
		len -= r;
		offs += r;
		s += r;
	}

	set_memsize(&params[1], s);
	return TEE_SUCCESS;
}

TEE_Result host_fs_write(size_t num_params, struct thread_param *params)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	tee_fs_off_t offs = 0;
	int fd = 0;
	ssize_t r = 0;

	if (num_params != 2 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_IN) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fd = params[0].u.value.b;
	offs = params[0].u.value.c;

	buf = convert_param_to_va(&params[1]);
	if (!buf)
		return TEE_ERROR_BAD_PARAMETERS;

	len = get_memsize_from_param(&params[1]);

	while (len) {
		r = pwrite(fd, buf, len, offs);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return errno_to_tee(errno);
		}
		assert((size_t)r <= len);
		buf += r;
		len -= r;
		offs += r;
	}

	return TEE_SUCCESS;
}

TEE_Result host_fs_truncate(size_t num_params, struct thread_param *params)
{
	size_t len = 0;
	int fd = 0;

	if (num_params != 1 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fd = params[0].u.value.b;
	len = params[0].u.value.c;

	while (ftruncate(fd, len)) {
		if (errno != EINTR)
			return errno_to_tee(errno);
	}

	return TEE_SUCCESS;
}

TEE_Result host_fs_remove(size_t num_params, struct thread_param *params)
{
	char abs_filename[PATH_MAX] = { 0 };
	char *fname = NULL;
	char *d = NULL;

	if (num_params != 2 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_IN) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fname = convert_param_to_va(&params[1]);
	if (!fname)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!tee_fs_get_absolute_filename(fname, abs_filename,
					  sizeof(abs_filename)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (unlink(abs_filename))
		return errno_to_tee(errno);

	/* If a file is removed, maybe the directory can be removed to? */
	d = dirname(abs_filename);
	if (!rmdir(d)) {
		/*
		 * If the directory was removed, maybe the parent directory
		 * can be removed too?
		 */
		d = dirname(d);
		rmdir(d);
	}

	return TEE_SUCCESS;
}

TEE_Result host_fs_rename(size_t num_params, struct thread_param *params)
{
	char old_abs_filename[PATH_MAX] = { 0 };
	char new_abs_filename[PATH_MAX] = { 0 };
	char *old_fname = NULL;
	char *new_fname = NULL;
	bool overwrite = false;

	if (num_params != 3 ||
		params[0].attr != THREAD_PARAM_ATTR_VALUE_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_IN ||
		params[2].attr != THREAD_PARAM_ATTR_MEMREF_IN) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	overwrite = !!params[0].u.value.b;

	old_fname = convert_param_to_va(&params[1]);
	if (!old_fname) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	new_fname = convert_param_to_va(&params[2]);
	if (!new_fname) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (!tee_fs_get_absolute_filename(old_fname, old_abs_filename,
					  sizeof(old_abs_filename)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!tee_fs_get_absolute_filename(new_fname, new_abs_filename,
					  sizeof(new_abs_filename)))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!overwrite) {
		struct stat st;

		if (!stat(new_abs_filename, &st))
			return TEE_ERROR_ACCESS_CONFLICT;
	}
	if (rename(old_abs_filename, new_abs_filename)) {
		if (errno == ENOENT)
			return TEE_ERROR_ITEM_NOT_FOUND;
	}

	fs_fsync();
	return TEE_SUCCESS;
}

service_init_late(host_fs_init);
