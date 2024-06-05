/*
 * Copyright (c) 2021, Linaro Limited
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

#include <kernel/notif.h>

TEE_Result notif_wait(uint32_t value)
{
	/* This notif_wait(uint32_t) function is the core part of
	 * wq_wait_final() function, and the wq_wait_final() function will using
	 * this function to implement wait for the lock become available.
	 * So if we left current function implementation empty,
	 * then the wq_wait_final() function will enter busy-loop, and
	 * current thread will block execution.
	 * So in order to wait for the lock become available, we need to
	 * yield current cpu out, we could perform this by calling print syslog
	 * (the syslog in TEE is implemented by syslog-rpmsg, and invoke syslog
	 * will make current TEE switch to REE, thus yield current cpu out),
	 * or just perform some sleep, which could also yield current cpu out.
	 *
	 * The following yield cpu implementation is referred to:
	 * openamp/libmetal/lib/processor/arm/cpu.h$metal_cpu_yield() function
	 */
	usleep(1000);
	return TEE_SUCCESS;
}

TEE_Result notif_send_sync(uint32_t value)
{
	return TEE_SUCCESS;
}
