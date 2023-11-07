/*
 * Copyright (c) 2014, 2015 Linaro Limited
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

#include <kernel/tee_time.h>
#include <kernel/time_source.h>
#include <sys/time.h>
#include <time.h>

static TEE_Result get_time_system(TEE_Time *time)
{
	struct timespec tv;
	if (!clock_gettime(CLOCK_MONOTONIC, &tv)) {
		/* Convert the struct timespec to a struct TEE_Time */
		time->seconds = tv.tv_sec;
		time->millis = tv.tv_nsec / 1000000;
		return TEE_SUCCESS;
	}
	return TEE_ERROR_ACCESS_DENIED;
}

static const struct time_source system_time_source = {
	.name = "system time",
	.protection_level = 1000,
	.get_sys_time = get_time_system,
};

REGISTER_TIME_SOURCE(system_time_source)
