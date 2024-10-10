/*
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

#include <trace.h>
#include <syslog.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>

const char trace_ext_prefix[] = "TC";
int trace_level = TRACE_LEVEL;

int trace_ext_get_core_id(void)
{
	return up_cpu_index();
}

int trace_ext_get_thread_id(void)
{
	return gettid();
}

#if TRACE_LEVEL > 0

void trace_ext_puts(const char *str)
{
	syslog(LOG_INFO, "%s", str);
}

#else

void trace_ext_puts(const char *str)
{
}

#endif
