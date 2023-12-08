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

#include <kernel/tee_misc.h>
#include <kernel/tee_ta_manager.h>
#include <string.h>
#include <tee_api_defines_extensions.h>
#include <tee_api_defines.h>
#include <tee/tee_fs.h>
#include <tee/tee_obj.h>
#include <tee/tee_pobj.h>
#include <tee/tee_svc_cryp.h>
#include <tee/tee_svc_compat.h>
#include <tee/tee_svc_storage.h>
#include <trace.h>
#include <kernel/user_ta.h>
#include <tee/error_messages.h>

/*
 * Returns the appropriate tee_file_operations for the specified storage ID.
 * The value TEE_STORAGE_PRIVATE will select the REE FS if available, otherwise
 * RPMB.
 */
static const struct tee_file_operations *file_ops(uint32_t storage_id)
{
	switch (storage_id) {
	case TEE_STORAGE_PRIVATE:
	case TEE_STORAGE_USER:
		return &ree_fs_ops;
	default:
		EMSG(ERR_MSG_ITEM_NOT_FOUND ": 0x%08lx\n", storage_id);
		return NULL;
	}
}

/* SSF (Secure Storage File version 00 */
#define TEE_SVC_STORAGE_MAGIC 0x53534600

/* Header of GP formated secure storage files */
struct tee_svc_storage_head {
	uint32_t magic;
	uint32_t head_size;
	uint32_t meta_size;
	uint32_t ds_size;
	uint32_t keySize;
	uint32_t maxKeySize;
	uint32_t objectUsage;
	uint32_t objectType;
	uint32_t have_attrs;
};

/* #define FS_STORAGE_DIR_PRIVATE "/data/", "/mnt/lfs/" */
/* "/TA_uuid/object_id" or "/TA_uuid/.object_id" */
char *tee_svc_storage_create_filename(struct ts_session *ts_sess,
				      uint32_t storage_id,
				      void *object_id,
				      uint32_t object_id_len,
				      bool transient)
{
	uint8_t *file;
	uint32_t pos = 0;
	uint32_t hslen;

	hslen = strlen(FS_STORAGE_DIR_PRIVATE) /* Leading slash "/data/" */
			+ TEE_B2HS_HSBUF_SIZE(sizeof(storage_id)) + 1
			+ TEE_B2HS_HSBUF_SIZE(sizeof(TEE_UUID) + object_id_len)
			+ 1; /* Intermediate slash */
	/* +1 for the '.' (temporary persistent object) */
	if (transient)
		hslen++;

	file = malloc(hslen);
	if (!file) {
		EMSG(ERR_MSG_OUT_OF_MEMORY ": %lu\n", hslen);
		return NULL;
	}

	/* file[pos++] = '/'; */
	memcpy(file, FS_STORAGE_DIR_PRIVATE, strlen(FS_STORAGE_DIR_PRIVATE));
	pos += strlen(FS_STORAGE_DIR_PRIVATE);

	pos += tee_b2hs((uint8_t *)&storage_id, &file[pos],
			sizeof(storage_id), hslen);
	file[pos++] = '/';

	pos += tee_b2hs((uint8_t *)&ts_sess->ctx->uuid, &file[pos],
			sizeof(TEE_UUID), hslen);
	file[pos++] = '/';

	if (transient)
		file[pos++] = '.';

	tee_b2hs(object_id, file + pos, object_id_len, hslen - pos);

	return (char *)file;
}

/* "/TA_uuid" */
char *tee_svc_storage_create_dirname(struct ts_session *ts_sess)
{
	uint8_t *dir;
	uint32_t hslen = TEE_B2HS_HSBUF_SIZE(sizeof(TEE_UUID)) + 1;

	dir = malloc(hslen);
	if (!dir)
		return NULL;

	dir[0] = '/';
	tee_b2hs((uint8_t *)&ts_sess->ctx->uuid, dir + 1, sizeof(TEE_UUID),
		 hslen);

	return (char *)dir;
}

static TEE_Result tee_svc_storage_remove_corrupt_obj(
					struct ts_session *ts_sess,
					struct tee_obj *o)
{
	TEE_Result res;
	char *file = NULL;

	file = tee_svc_storage_create_filename(ts_sess,
					       o->pobj->storage_id,
					       o->pobj->obj_id, o->pobj->obj_id_len, false);
	if (file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	tee_obj_close(to_user_ta_ctx(ts_sess->ctx), o);
	EMSG(ERR_MSG_CORRUPT_OBJECT ": %s", file);
	free(file);

	res = TEE_SUCCESS;

exit:
	return res;
}

static TEE_Result tee_svc_storage_read_head(struct ts_session *ts_sess,
				     struct tee_obj *o)
{
	TEE_Result res = TEE_SUCCESS;
	size_t bytes;
	struct tee_svc_storage_head head;
	char *file = NULL;
	const struct tee_file_operations *fops;
	void *attr = NULL;

	if (o == NULL || o->pobj == NULL) {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	fops = o->pobj->fops;

	file = tee_svc_storage_create_filename(ts_sess,
					       o->pobj->storage_id,
					       o->pobj->obj_id, o->pobj->obj_id_len, false);
	if (file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	res = fops->open(file, &o->fh);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	/* read head */
	bytes = sizeof(struct tee_svc_storage_head);
	res = fops->read(o->fh, &head, &bytes);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		if (res == TEE_ERROR_CORRUPT_OBJECT) {
			EMSG(ERR_MSG_CORRUPT_OBJECT "\n");
		}
		goto exit;
	}

	if (bytes != sizeof(struct tee_svc_storage_head)) {
		EMSG(ERR_MSG_BAD_FORMAT ": %zu\n", bytes);
		res = TEE_ERROR_BAD_FORMAT;
		goto exit;
	}

	res = tee_obj_set_type(o, head.objectType, head.maxKeySize);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	if (head.meta_size) {
		attr = malloc(head.meta_size);
		if (!attr) {
			EMSG(ERR_MSG_OUT_OF_MEMORY ": %lu\n", head.meta_size);
			res = TEE_ERROR_OUT_OF_MEMORY;
			goto exit;
		}

		/* read meta */
		bytes = head.meta_size;
		res = fops->read(o->fh, attr, &bytes);
		if (res != TEE_SUCCESS || bytes != head.meta_size) {
			EMSG(ERR_MSG_CORRUPT_OBJECT ": 0x%08lx, %zu, %lu\n", res, bytes, head.meta_size);
			res = TEE_ERROR_CORRUPT_OBJECT;
			goto exit;
		}
	}

	res = tee_obj_attr_from_binary(o, attr, head.meta_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	o->info.dataSize = head.ds_size;
	o->info.objectSize = head.objectType;
	o->info.objectUsage = head.objectUsage;
	o->info.objectType = head.objectType;
	o->have_attrs = head.have_attrs;

exit:
	free(attr);
	free(file);

	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

static TEE_Result tee_svc_storage_update_head(struct tee_obj *o,
					uint32_t ds_size)
{
	TEE_Result res;
	const struct tee_file_operations *fops;
	int32_t old_off;

	fops = o->pobj->fops;

	/* save original offset */
	res = fops->seek(o->fh, 0, TEE_DATA_SEEK_CUR, &old_off);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	/* update head.ds_size */
	res = fops->seek(o->fh, offsetof(struct tee_svc_storage_head,
			ds_size), TEE_DATA_SEEK_SET, NULL);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	res = fops->write(o->fh, &ds_size, sizeof(uint32_t));
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	/* restore original offset */
	res = fops->seek(o->fh, old_off, TEE_DATA_SEEK_SET, NULL);
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

static TEE_Result tee_svc_storage_init_file(struct ts_session *ts_sess,
					    struct tee_obj *o,
					    struct tee_obj *attr_o, void *data,
					    uint32_t len)
{
	TEE_Result res = TEE_SUCCESS;
	struct tee_svc_storage_head head;
	char *tmpfile = NULL;
	const struct tee_file_operations *fops;
	void *attr = NULL;
	size_t attr_size = 0;

	if (o == NULL || o->pobj == NULL) {
		EMSG(ERR_MSG_BAD_PARAMETERS "\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	fops = o->pobj->fops;

	/* create temporary persistent object filename */
	tmpfile = tee_svc_storage_create_filename(ts_sess,
						  o->pobj->storage_id,
						  o->pobj->obj_id, o->pobj->obj_id_len, true);
	if (tmpfile == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto exit;
	}
	res = fops->create(tmpfile, &o->fh);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	if (attr_o) {
		res = tee_obj_set_type(o, attr_o->info.objectType,
				attr_o->info.maxObjectSize);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
			goto exit;
		}
		res = tee_obj_attr_copy_from(o, attr_o);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
			goto exit;
		}
		o->have_attrs = attr_o->have_attrs;
		o->info.objectUsage = attr_o->info.objectUsage;
		o->info.objectSize = attr_o->info.objectSize;
		res = tee_obj_attr_to_binary(o, NULL, &attr_size);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
			goto exit;
		}
		if (attr_size) {
			attr = malloc(attr_size);
			if (!attr) {
				EMSG(ERR_MSG_OUT_OF_MEMORY ": %zu\n", attr_size);
				res = TEE_ERROR_OUT_OF_MEMORY;
				goto exit;
			}
			res = tee_obj_attr_to_binary(o, attr, &attr_size);
			if (res != TEE_SUCCESS) {
				EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
				goto exit;
			}
		}
	} else {
		res = tee_obj_set_type(o, TEE_TYPE_DATA, 0);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
			goto exit;
		}
	}

	/* write head */
	head.magic = TEE_SVC_STORAGE_MAGIC;
	head.head_size = sizeof(struct tee_svc_storage_head);
	head.meta_size = attr_size;
	head.ds_size = len;
	head.objectType = o->info.objectSize;
	head.maxKeySize = o->info.maxObjectSize;
	head.objectUsage = o->info.objectUsage;
	head.objectType = o->info.objectType;
	head.have_attrs = o->have_attrs;

	/* write head */
	DMSG("write header, size: %d\n", sizeof(struct tee_svc_storage_head));
	res = fops->write(o->fh, &head, sizeof(struct tee_svc_storage_head));
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	DMSG("write attribute, size: %d\n", attr_size);
	/* write meta */
	res = fops->write(o->fh, attr, attr_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	/* write init data */
	o->info.dataSize = len;
	DMSG("write data, size: %zu\n", o->info.dataSize);

	/* write data to fs if needed */
	if (data && len)
		res = fops->write(o->fh, data, len);

exit:
	free(attr);
	free(tmpfile);
	/* close temporary persistent object */
	fops->close(&o->fh);

	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_open(unsigned long storage_id, void *object_id,
			size_t object_id_len, unsigned long flags,
			uint32_t *obj)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o = NULL;
	char *file = NULL;
	struct tee_pobj *po = NULL;
	struct user_ta_ctx *utc;
	const struct tee_file_operations *fops = file_ops(storage_id);
	size_t attr_size;

	if (!fops) {
		EMSG(ERR_MSG_ITEM_NOT_FOUND "\n");
		res = TEE_ERROR_ITEM_NOT_FOUND;
		goto exit;
	}

	if (object_id_len > TEE_OBJECT_ID_MAX_LEN) {
		EMSG(ERR_MSG_BAD_PARAMETERS ": %zu\n", object_id_len);
		res = TEE_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_pobj_get((void *)&ts_sess->ctx->uuid, object_id,
			   object_id_len, flags, false, fops, &po);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto err;
	}

	o = tee_obj_alloc();
	if (o == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		tee_pobj_release(po);
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	o->info.handleFlags =
		TEE_HANDLE_FLAG_PERSISTENT | TEE_HANDLE_FLAG_INITIALIZED;
	o->flags = flags;
	o->pobj = po;
	o->pobj->storage_id = storage_id;
	tee_obj_add(utc, o);

	res = tee_svc_storage_read_head(ts_sess, o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		if (res == TEE_ERROR_CORRUPT_OBJECT) {
			EMSG(ERR_MSG_CORRUPT_OBJECT "\n");
			goto err;
		}
		goto oclose;
	}

	res = tee_svc_copy_kaddr_to_uref(obj, o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto oclose;
	}

	res = tee_obj_attr_to_binary(o, NULL, &attr_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto oclose;
	}

	res = fops->seek(o->fh, sizeof(struct tee_svc_storage_head) + attr_size,
			 TEE_DATA_SEEK_SET, NULL);
	if (res  != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto err;
	}
	goto exit;

oclose:
	tee_obj_close(utc, o);
	o = NULL;

err:
	if (res == TEE_ERROR_NO_DATA || res == TEE_ERROR_BAD_FORMAT)
		res = TEE_ERROR_CORRUPT_OBJECT;
	if (res == TEE_ERROR_CORRUPT_OBJECT && o)
		tee_svc_storage_remove_corrupt_obj(ts_sess, o);
exit:
	free(file);
	file = NULL;
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_create(unsigned long storage_id, void *object_id,
			size_t object_id_len, unsigned long flags,
			unsigned long attr, void *data, size_t len,
			uint32_t *obj)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o = NULL;
	struct tee_obj *attr_o = NULL;
	char *file = NULL;
	struct tee_pobj *po = NULL;
	char *tmpfile = NULL;
	struct user_ta_ctx *utc;
	const struct tee_file_operations *fops = file_ops(storage_id);
	size_t attr_size;

	if (!fops) {
		EMSG(ERR_MSG_ITEM_NOT_FOUND "\n");
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	if (object_id_len > TEE_OBJECT_ID_MAX_LEN) {
		EMSG(ERR_MSG_BAD_PARAMETERS ": %zu\n", object_id_len);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_pobj_get((void *)&ts_sess->ctx->uuid, object_id,
			   object_id_len, flags, true, fops, &po);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto err;
	}

	o = tee_obj_alloc();
	if (o == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	o->info.handleFlags =
	    TEE_HANDLE_FLAG_PERSISTENT | TEE_HANDLE_FLAG_INITIALIZED;
	o->flags = flags;
	o->pobj = po;
	o->pobj->storage_id = storage_id;

	if (attr != TEE_HANDLE_NULL) {
		res = tee_obj_get(utc, tee_svc_uref_to_vaddr(attr),
				  &attr_o);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
			goto err;
		}
	}

	res = tee_svc_storage_init_file(ts_sess, o, attr_o, data, len);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto err;
	}

	/* create persistent object filename */
	file = tee_svc_storage_create_filename(ts_sess,
				o->pobj->storage_id,
				object_id, object_id_len, false);
	if (file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	/* create temporary persistent object filename */
	tmpfile = tee_svc_storage_create_filename(ts_sess,
				o->pobj->storage_id,
				object_id, object_id_len, true);
	if (tmpfile == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	/* rename temporary persistent object filename */
	res = fops->rename(tmpfile, file, !!(flags & TEE_DATA_FLAG_OVERWRITE));
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto rmfile;
	}

	res = fops->open(file, &o->fh);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto err;
	}

	tee_obj_add(utc, o);

	res = tee_svc_copy_kaddr_to_uref(obj, o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto oclose;
	}

	res = tee_obj_attr_to_binary(o, NULL, &attr_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto oclose;
	}

	res = fops->seek(o->fh, sizeof(struct tee_svc_storage_head) + attr_size,
			 TEE_DATA_SEEK_SET, NULL);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto oclose;
	}

	goto exit;

oclose:
	EMSG(ERR_MSG_GENERIC "\n");
	tee_obj_close(utc, o);
	goto exit;

rmfile:
	EMSG(ERR_MSG_GENERIC ": %s\n", tmpfile);
	fops->remove(tmpfile);

err:
	EMSG(ERR_MSG_GENERIC "\n");
	if (res == TEE_ERROR_NO_DATA || res == TEE_ERROR_BAD_FORMAT)
		res = TEE_ERROR_CORRUPT_OBJECT;
	if (res == TEE_ERROR_CORRUPT_OBJECT && file)
		fops->remove(file);
	if (o)
		fops->close(&o->fh);
	if (po)
		tee_pobj_release(po);
	free(o);

exit:
	free(file);
	free(tmpfile);
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_del(unsigned long obj)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	char *file;
	struct user_ta_ctx *utc;
	const struct tee_file_operations *fops;

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_obj_get(utc, tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	if (!(o->flags & TEE_DATA_FLAG_ACCESS_WRITE_META)) {
		EMSG(ERR_MSG_ACCESS_CONFLICT ": 0x%08lx\n", o->flags);
		return TEE_ERROR_ACCESS_CONFLICT;
	}

	if (o->pobj == NULL || o->pobj->obj_id == NULL) {
		EMSG(ERR_MSG_BAD_STATE "\n");
		return TEE_ERROR_BAD_STATE;
	}

	file = tee_svc_storage_create_filename(ts_sess,
				o->pobj->storage_id, o->pobj->obj_id,
				o->pobj->obj_id_len, false);
	if (file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	fops = o->pobj->fops;
	tee_obj_close(utc, o);

	res = fops->remove(file);
	free(file);
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_rename(unsigned long obj, void *object_id,
			size_t object_id_len)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	struct tee_pobj *po = NULL;
	char *new_file = NULL;
	char *old_file = NULL;
	struct user_ta_ctx *utc;
	const struct tee_file_operations *fops;

	if (object_id_len > TEE_OBJECT_ID_MAX_LEN) {
		EMSG(ERR_MSG_BAD_PARAMETERS ": %zu\n", object_id_len);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_obj_get(utc, tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		return res;
	}

	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->info.handleFlags);
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	if (!(o->flags & TEE_DATA_FLAG_ACCESS_WRITE_META)) {
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->flags);
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	if (o->pobj == NULL || o->pobj->obj_id == NULL) {
		EMSG(ERR_MSG_BAD_STATE "\n");
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	/* get new ds name */
	new_file = tee_svc_storage_create_filename(ts_sess,
						   o->pobj->storage_id,
						   object_id, object_id_len, false);
	if (new_file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	old_file = tee_svc_storage_create_filename(ts_sess,
						   o->pobj->storage_id,
						   o->pobj->obj_id, o->pobj->obj_id_len, false);
	if (old_file == NULL) {
		EMSG(ERR_MSG_OUT_OF_MEMORY "\n");
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto exit;
	}

	/* reserve dest name */
	fops = o->pobj->fops;
	res = tee_pobj_get((void *)&ts_sess->ctx->uuid, object_id,
			   object_id_len, TEE_DATA_FLAG_ACCESS_WRITE_META, false,
			   fops, &po);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	/* fsync for VELAPLATFO-1183 */
	res = fops->fsync(&o->fh);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	/* move */
	res = fops->rename(old_file, new_file, false /* no overwrite */);
	if (res == TEE_ERROR_GENERIC) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}
	res = tee_pobj_rename(o->pobj, object_id, object_id_len);

exit:
	tee_pobj_release(po);

	free(new_file);
	free(old_file);
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_read(unsigned long obj, void *data, size_t len,
			uint64_t *count)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	uint64_t u_count;
	struct user_ta_ctx *utc;
	size_t bytes;

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_obj_get(utc, tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", res);
		goto exit;
	}

	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->info.handleFlags);
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	if (!(o->flags & TEE_DATA_FLAG_ACCESS_READ)) {
		EMSG(ERR_MSG_ACCESS_CONFLICT ": 0x%08lx\n", o->flags);
		res = TEE_ERROR_ACCESS_CONFLICT;
		goto exit;
	}

	bytes = len;
	res = o->pobj->fops->read(o->fh, data, &bytes);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		if (res == TEE_ERROR_CORRUPT_OBJECT) {
			EMSG(ERR_MSG_CORRUPT_OBJECT "\n");
			tee_svc_storage_remove_corrupt_obj(ts_sess, o);
		}
		goto exit;
	}

	o->info.dataPosition += bytes;

	u_count = bytes;
	res = tee_svc_copy_to_user(count, &u_count, sizeof(*count));
exit:
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_write(unsigned long obj, void *data, size_t len)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	struct user_ta_ctx *utc;

	ts_sess = ts_get_current_session();
	utc = to_user_ta_ctx(ts_sess->ctx);

	res = tee_obj_get(utc, tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->info.handleFlags);
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	if (!(o->flags & TEE_DATA_FLAG_ACCESS_WRITE)) {
		EMSG(ERR_MSG_ACCESS_CONFLICT ": 0x%08lx\n", o->flags);
		res = TEE_ERROR_ACCESS_CONFLICT;
		goto exit;
	}

	res = o->pobj->fops->write(o->fh, data, len);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	o->info.dataPosition += len;
	if (o->info.dataPosition > o->info.dataSize) {
		res = tee_svc_storage_update_head(o, o->info.dataPosition);
		if (res != TEE_SUCCESS) {
			EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
			goto exit;
		}
		o->info.dataSize = o->info.dataPosition;
	}

exit:
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_trunc(unsigned long obj, size_t len)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	size_t off;
	size_t attr_size;

	ts_sess = ts_get_current_session();

	res = tee_obj_get(to_user_ta_ctx(ts_sess->ctx),
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->info.handleFlags);
		res = TEE_ERROR_BAD_STATE;
		goto exit;
	}

	if (!(o->flags & TEE_DATA_FLAG_ACCESS_WRITE)) {
		EMSG(ERR_MSG_ACCESS_CONFLICT ": 0x%08lx\n", o->flags);
		res = TEE_ERROR_ACCESS_CONFLICT;
		goto exit;
	}

	res = tee_obj_attr_to_binary(o, NULL, &attr_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	off = sizeof(struct tee_svc_storage_head) + attr_size;
	res = o->pobj->fops->truncate(o->fh, len + off);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		if (res == TEE_ERROR_CORRUPT_OBJECT) {
			EMSG(ERR_MSG_CORRUPT_OBJECT "\n");
			res = tee_svc_storage_remove_corrupt_obj(ts_sess, o);
			if (res != TEE_SUCCESS) {
				EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
				goto exit;
			}
			res = TEE_ERROR_CORRUPT_OBJECT;
			goto exit;
		} else
			res = TEE_ERROR_GENERIC;
	}

exit:
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_obj_seek(unsigned long obj, int32_t offset,
				    unsigned long whence)
{
	TEE_Result res;
	struct ts_session *ts_sess;
	struct tee_obj *o;
	int32_t off;
	size_t attr_size;

	ts_sess = ts_get_current_session();

	res = tee_obj_get(to_user_ta_ctx(ts_sess->ctx),
			  tee_svc_uref_to_vaddr(obj), &o);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	if (!(o->info.handleFlags & TEE_HANDLE_FLAG_PERSISTENT)) {
		res = TEE_ERROR_BAD_STATE;
		EMSG(ERR_MSG_BAD_STATE ": 0x%08lx\n", o->info.handleFlags);
		goto exit;
	}

	res = tee_obj_attr_to_binary(o, NULL, &attr_size);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	off = offset;
	if (whence == TEE_DATA_SEEK_SET)
		off += sizeof(struct tee_svc_storage_head) + attr_size;

	res = o->pobj->fops->seek(o->fh, off, whence, &off);
	if (res != TEE_SUCCESS) {
		EMSG(ERR_MSG_GENERIC ": 0x%08lx\n", (uint32_t)res);
		goto exit;
	}

	o->info.dataPosition = off - (sizeof(struct tee_svc_storage_head) +
				      attr_size);

exit:
	if (res) {
		DMSG("res: 0x%08lx\n", res);
	}
	return res;
}

TEE_Result syscall_storage_alloc_enum(uint32_t *obj_enum)
{
	return TEE_SUCCESS;
}

TEE_Result syscall_storage_free_enum(unsigned long obj_enum)
{
	return TEE_SUCCESS;
}

TEE_Result syscall_storage_reset_enum(unsigned long obj_enum)
{
	return TEE_SUCCESS;
}

TEE_Result syscall_storage_start_enum(unsigned long obj_enum,
				      unsigned long storage_id)
{
	return TEE_SUCCESS;
}

TEE_Result syscall_storage_next_enum(unsigned long obj_enum,
			struct utee_object_info *info, void *obj_id,
			uint64_t *len)
{
	return TEE_SUCCESS;
}
