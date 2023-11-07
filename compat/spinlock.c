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

#include <kernel/spinlock.h>
#include <stdatomic.h>

void __cpu_spin_lock(unsigned int *lock)
{
	while (atomic_flag_test_and_set(lock));
}

void __cpu_spin_unlock(unsigned int *lock)
{
	atomic_flag_clear(lock);
}

unsigned int __cpu_spin_trylock(unsigned int *lock)
{
	return atomic_flag_test_and_set(lock);
}
