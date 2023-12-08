/*
 * Copyright (c) 2016-2022, Linaro Limited
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tee/tee_fs.h>
#include <tee/tee_fs_rpc.h>
#include <trace.h>
#include <util.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <tee/error_messages.h>

/* file name: /data/00F2AA8A5024E411ABE20002A5D5C51B/.74657374312E74787400 */
TEE_Result tee_fs_rpc_open(const char *file, bool create, int *fd)
{
	int _fd = -1;
	int flags_c = 0x87; /* O_RDWR | O_CREAT | O_SYNC */
	int flags_o = 0x83; /* O_RDWR | O_SYNC */
	char tmp_file[128] = { 0 };
	char tmp_dir[128] = { 0 };

	if (!file || *file == '\0') {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

try_open:
	DMSG("%s %s", create ? "create" : "open", file);
	if(create) {
		_fd = open(file, flags_c);
		if(_fd < 0) {
			EMSG(ERR_MSG_GENERIC ": %d\n", errno);
			if (strlen(file) > sizeof(tmp_file) - 1) {
				EMSG(ERR_MSG_SHORT_BUFFER "\n");
				return TEE_ERROR_SHORT_BUFFER;
			}
			strcpy(tmp_file, file);

			char *dir = dirname(tmp_file);

			/* CID 209916, STRING_OVERFLOW. No problem */
			strcpy(tmp_dir, dir);
			char *top_dir = dirname(tmp_dir);

			DMSG("mkdir: %s\n", top_dir);
			DMSG("mkdir: %s\n", dir);
			if(mkdir(top_dir, 0777)){
				DMSG("mkdir %s failed\n", top_dir);
			}
			if(!mkdir(dir, 0777)){
				DMSG("mkdir %s ok, goto try_open\n", dir);
				goto try_open;
			} else {
				EMSG(ERR_MSG_GENERIC ": %s\n", dir);
				return TEE_ERROR_GENERIC;
			}
		}
	} else {
		_fd = open(file, flags_o);
		if (_fd < 0){
			EMSG(ERR_MSG_GENERIC ": %d\n", errno);
			if (errno == ENOENT) {
				EMSG(ERR_MSG_ITEM_NOT_FOUND "\n");
				return TEE_ERROR_ITEM_NOT_FOUND;
			}
			return TEE_ERROR_GENERIC;
		}
	}
        DMSG("fd: %d\n", _fd);
	*fd = _fd;
	return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_close(int fd)
{
	if (fd < 0) {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	while( close(fd)) {
                if (errno != EINTR) {
			EMSG(ERR_MSG_GENERIC ": %d\n", errno);
                        return TEE_ERROR_GENERIC;
		}
        }
        return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_read(int fd, void *buf, size_t *size, int offs)
{
	long long int _offs = 0;
	ssize_t r = -1;
	ssize_t _size = *size;

	_offs = lseek(fd, offs, 0); /* SEEK_SET */
	DMSG("lseek, offs: %d --> offs: %lld\n", offs, _offs);
	if(_offs != offs) {
		EMSG(ERR_MSG_GENERIC ": %lld\n", _offs);
		*size = 0;
		return TEE_ERROR_GENERIC;
	}

	while (r && _size) {
		DMSG("fd: %d, read size: %d\n", fd, _size);
		r = read(fd, buf, _size);
		DMSG("r: %d\n", r);
		if (r < 0) {
			EMSG(ERR_MSG_GENERIC ": %d, %d\n", fd, errno);
			if (errno == EINTR)
				continue;
			*size -= _size;
			return TEE_ERROR_GENERIC;
		}
		buf += r;
		_size -= r;
	}

	*size -= _size;
	return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_write(int fd, void *buf, size_t *size, int offs)
{
	long long int _offs = 0;
	ssize_t r = 0;
	size_t _size = *size;

	_offs = lseek(fd, offs, 0); /* SEEK_SET */
	DMSG("lseek, offs: %d --> offs: %lld\n", offs, _offs);
	if(_offs != offs) {
		EMSG(ERR_MSG_GENERIC ": %lld\n", _offs);
		*size = 0;
		return TEE_ERROR_GENERIC;
	}

	while (_size) {
		DMSG("fd: %d, write size: %d\n", fd, _size);
		r = write(fd, buf, _size);
		DMSG("r: %d\n", r);
		if (r < 0) {
			EMSG(ERR_MSG_GENERIC ": %d, %d\n", fd, errno);
			if (errno == EINTR)
				continue;
			*size -= _size;
			return TEE_ERROR_GENERIC;
		}
		buf += r;
		_size -= r;
	}

	*size -= _size;
	return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_rename(const char * old_file, const char * new_file, bool overwrite)
{
	int r = 0;

	if (!old_file || *old_file == '\0') {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	if (!new_file || *new_file == '\0') {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (!overwrite) {
		struct stat st;
		if (!stat(new_file, &st)) {
			EMSG(ERR_MSG_ACCESS_CONFLICT "\n");
			return TEE_ERROR_ACCESS_CONFLICT;
		}
	}

	r = rename(old_file, new_file);
	if (r) {
		EMSG(ERR_MSG_GENERIC ": %d\n", errno);
		if (errno == ENOENT) {
			EMSG(ERR_MSG_ITEM_NOT_FOUND "\n");
			return TEE_ERROR_ITEM_NOT_FOUND;
		}
		return TEE_ERROR_GENERIC;
	}
	return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_remove(const char *file)
{
	int r = 0;

	if (!file || *file == '\0') {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	r = unlink(file);
	if (r) {
		EMSG(ERR_MSG_GENERIC ": %d\n", errno);
		if (errno == ENOENT) {
			EMSG(ERR_MSG_ITEM_NOT_FOUND "\n");
			return TEE_ERROR_ITEM_NOT_FOUND;
		}
		return TEE_ERROR_GENERIC;
	}
	return TEE_SUCCESS;
}

TEE_Result tee_fs_rpc_fsync(int fd)
{
	if (fd < 0) {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	return fsync(fd);
}
