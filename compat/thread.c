/*
 * Copyright (c) 2016-2022, Linaro Limited
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * Copyright (c) 2020-2021, Arm Limited
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

#include <sys/types.h>
#include <kernel/thread.h>
#include <nuttx/irq.h>

static struct thread_specific_data tsd;

uint32_t thread_enter_user_mode(unsigned long a0, unsigned long a1,
		unsigned long a2, unsigned long a3, unsigned long user_sp,
		unsigned long entry_func, bool is_32bit,
		uint32_t *exit_status0, uint32_t *exit_status1)
{
	return TEE_SUCCESS;
}

uint32_t thread_get_exceptions(void)
{
	return THREAD_EXCP_FOREIGN_INTR;
}

uint32_t thread_mask_exceptions(uint32_t exceptions)
{
	return up_irq_save();
}

void thread_unmask_exceptions(uint32_t state)
{
	up_irq_restore(state);
}

short int thread_get_id(void)
{
	return gettid();
}

short int thread_get_id_may_fail(void)
{
	return gettid();
}

bool thread_is_in_normal_mode(void)
{
	return true;
}

uint32_t thread_rpc_cmd(uint32_t cmd, size_t num_params,
		struct thread_param *params)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

struct thread_specific_data *thread_get_tsd(void)
{
	return &tsd;
}

void thread_set_foreign_intr(bool enable)
{
}
