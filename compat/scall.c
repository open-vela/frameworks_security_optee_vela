/*
 * Copyright (c) 2014-2022, Linaro Limited
 * Copyright (c) 2020, Arm Limited
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

#include <kernel/scall.h>
#include <kernel/panic.h>
#include <tee/tee_svc_cryp.h>
#include <tee/tee_svc_storage.h>
#include <utee_syscalls.h>

bool scall_handle_user_ta(struct thread_scall_regs *regs)
{
	return false;
}

TEE_Result _utee_open_ta_session(const TEE_UUID *dest,
				 unsigned long cancel_req_to,
				 struct utee_params *params, uint32_t *sess,
				 uint32_t *ret_orig)
{
	return syscall_open_ta_session(dest, cancel_req_to, params, sess, ret_orig);
}

TEE_Result _utee_close_ta_session(unsigned long sess)
{
	return syscall_close_ta_session(sess);
}

TEE_Result _utee_invoke_ta_command(unsigned long sess,
				unsigned long cancel_req_to,
				unsigned long cmd_id,
				struct utee_params *params,
				uint32_t *ret_orig)
{
	return syscall_invoke_ta_command(sess, cancel_req_to,
				cmd_id, params, ret_orig);
}

TEE_Result _utee_check_access_rights(uint32_t flags, const void *buf,
				size_t len)
{
	return syscall_check_access_rights(flags, buf, len);
}

TEE_Result _utee_cryp_obj_get_info(unsigned long obj,
				   struct utee_object_info *info)
{
	return syscall_cryp_obj_get_info(obj, info);
}

TEE_Result _utee_cryp_obj_restrict_usage(unsigned long obj,
					 unsigned long usage)
{
	return syscall_cryp_obj_restrict_usage(obj, usage);
}

TEE_Result _utee_cryp_obj_get_attr(unsigned long obj, unsigned long attr_id,
				   void *buffer, uint64_t *size)
{
	return syscall_cryp_obj_get_attr(obj, attr_id, buffer, size);
}

TEE_Result _utee_cryp_obj_close(unsigned long obj)
{
	return syscall_cryp_obj_close(obj);
}

TEE_Result _utee_hash_update(unsigned long state, const void *chunk,
				 size_t chunk_size)
{
	return syscall_hash_update(state, chunk, chunk_size);
}

TEE_Result _utee_authenc_init(unsigned long state, const void *nonce,
				  size_t nonce_len, size_t tag_len, size_t aad_len,
				  size_t payload_len)
{
	return syscall_authenc_init(state, nonce, nonce_len, tag_len, aad_len, payload_len);
}

TEE_Result _utee_authenc_update_aad(unsigned long state, const void *aad_data,
				size_t aad_data_len)
{
	return syscall_authenc_update_aad(state, aad_data, aad_data_len);
}

TEE_Result _utee_asymm_operate(unsigned long state,
			       const struct utee_attribute *params,
			       unsigned long num_params, const void *src_data,
			       size_t src_len, void *dest_data,
			       uint64_t *dest_len)
{
	return syscall_asymm_operate(state, params, num_params,
			src_data, src_len, dest_data, dest_len);
}

TEE_Result _utee_asymm_verify(unsigned long state,
			const struct utee_attribute *params,
			unsigned long num_params, const void *data,
			size_t data_len, const void *sig, size_t sig_len)
{
	return syscall_asymm_verify(state, params, num_params,
			data, data_len, sig, sig_len);
}

TEE_Result _utee_cryp_obj_alloc(unsigned long type, unsigned long max_size,
				uint32_t *obj)
{
	return syscall_cryp_obj_alloc(type, max_size, obj);
}

TEE_Result _utee_cryp_obj_reset(unsigned long obj)
{
	return syscall_cryp_obj_reset(obj);
}

TEE_Result _utee_cryp_obj_populate(unsigned long obj,
				   struct utee_attribute *attrs,
				   unsigned long attr_count)
{
	return syscall_cryp_obj_populate(obj, attrs, attr_count);
}

TEE_Result _utee_cryp_obj_copy(unsigned long dst_obj, unsigned long src_obj)
{
	return syscall_cryp_obj_copy(dst_obj, src_obj);
}

TEE_Result _utee_cryp_obj_generate_key(unsigned long obj,
					   unsigned long key_size,
					   const struct utee_attribute *params,
					   unsigned long param_count)
{
	return syscall_obj_generate_key(obj, key_size, params, param_count);
}

TEE_Result _utee_cryp_derive_key(unsigned long state,
				 const struct utee_attribute *params,
				 unsigned long param_count,
				 unsigned long derived_key)
{
	return syscall_cryp_derive_key(state, params, param_count, derived_key);
}

TEE_Result _utee_hash_init(unsigned long state, const void *iv, size_t iv_len)
{
	return syscall_hash_init(state, iv, iv_len);
}

TEE_Result _utee_cryp_state_alloc(unsigned long algo, unsigned long op_mode,
				  unsigned long key1, unsigned long key2,
				  uint32_t *state)
{
	return syscall_cryp_state_alloc(algo, op_mode, key1, key2, state);
}

TEE_Result _utee_authenc_update_payload(unsigned long state,
					const void *src_data, size_t src_len,
					void *dest_data, uint64_t *dest_len)
{
	return syscall_authenc_update_payload(state, src_data, src_len, dest_data, dest_len);
}

TEE_Result _utee_authenc_dec_final(unsigned long state, const void *src_data,
				   size_t src_len, void *dest_data,
				   uint64_t *dest_len, const void *tag,
				   size_t tag_len)
{
	return syscall_authenc_dec_final(state, src_data, src_len, dest_data, dest_len, tag, tag_len);
}

TEE_Result _utee_cryp_state_copy(unsigned long dst, unsigned long src)
{
	return syscall_cryp_state_copy(dst, src);
}

TEE_Result _utee_cryp_state_free(unsigned long state)
{
	return syscall_cryp_state_free(state);
}

TEE_Result _utee_cryp_random_number_generate(void *buf, size_t blen)
{
	return syscall_cryp_random_number_generate(buf, blen);
}

TEE_Result _utee_cipher_init(unsigned long state, const void *iv,
				 size_t iv_len)
{
	return syscall_cipher_init(state, iv, iv_len);
}

TEE_Result _utee_cipher_update(unsigned long state, const void *src,
				   size_t src_len, void *dest, uint64_t *dest_len)
{
	return syscall_cipher_update(state, src, src_len, dest, dest_len);
}

TEE_Result _utee_cipher_final(unsigned long state, const void *src,
				  size_t src_len, void *dest, uint64_t *dest_len)
{
	return syscall_cipher_final(state, src, src_len, dest, dest_len);
}

TEE_Result _utee_hash_final(unsigned long state, const void *chunk,
				size_t chunk_size, void *hash, uint64_t *hash_len)
{
	return syscall_hash_final(state, chunk, chunk_size, hash, hash_len);
}

TEE_Result _utee_authenc_enc_final(unsigned long state, const void *src_data,
				   size_t src_len, void *dest_data,
				   uint64_t *dest_len, void *tag,
				   uint64_t *tag_len)
{
	return syscall_authenc_enc_final(state, src_data, src_len, dest_data, dest_len, tag, tag_len);
}

TEE_Result _utee_storage_alloc_enum(uint32_t *obj_enum)
{
	return syscall_storage_alloc_enum(obj_enum);
}

TEE_Result _utee_storage_free_enum(unsigned long obj_enum)
{
	return syscall_storage_free_enum(obj_enum);
}

TEE_Result _utee_storage_reset_enum(unsigned long obj_enum)
{
	return syscall_storage_reset_enum(obj_enum);
}

TEE_Result _utee_storage_start_enum(unsigned long obj_enum,
				    unsigned long storage_id)
{
	return syscall_storage_start_enum(obj_enum, storage_id);
}

TEE_Result _utee_storage_next_enum(unsigned long obj_enum,
				   struct utee_object_info *info,
				   void *obj_id, uint64_t *len)
{
	return syscall_storage_next_enum(obj_enum, info, obj_id, len);
}

TEE_Result _utee_storage_obj_open(unsigned long storage_id,
				  const void *object_id, size_t object_id_len,
				  unsigned long flags, uint32_t *obj)
{
	return syscall_storage_obj_open(storage_id, object_id, object_id_len, flags, obj);
}

TEE_Result _utee_storage_obj_create(unsigned long storage_id,
					const void *object_id,
					size_t object_id_len, unsigned long flags,
					unsigned long attr, const void *data,
					size_t len, uint32_t *obj)
{
	return syscall_storage_obj_create(storage_id, object_id, object_id_len, flags, attr, data, len, obj);
}

TEE_Result _utee_storage_obj_del(unsigned long obj)
{
	return syscall_storage_obj_del(obj);
}

TEE_Result _utee_storage_obj_rename(unsigned long obj, const void *new_obj_id,
					size_t new_obj_id_len)
{
	return syscall_storage_obj_rename(obj, new_obj_id, new_obj_id_len);
}

TEE_Result _utee_storage_obj_read(unsigned long obj, void *data, size_t len,
				  uint64_t *count)
{
	return syscall_storage_obj_read(obj, data, len, count);
}

TEE_Result _utee_storage_obj_write(unsigned long obj, const void *data,
				   size_t len)
{
	return syscall_storage_obj_write(obj, data, len);
}

TEE_Result _utee_storage_obj_trunc(unsigned long obj, size_t len)
{
	return syscall_storage_obj_trunc(obj, len);
}

TEE_Result _utee_storage_obj_seek(unsigned long obj, int32_t offset,
				  unsigned long whence)
{
	return syscall_storage_obj_seek(obj, offset, whence);
}

TEE_Result _utee_get_time(unsigned long cat, TEE_Time *time)
{
	return syscall_get_time(cat, time);
}

void _utee_panic(unsigned long code)
{
	__panic(code);
}
