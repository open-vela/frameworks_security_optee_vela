/*
 * Copyright (c) 2017, EPAM Systems
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

#include <kernel/msg_param.h>
#include <mm/mobj.h>
#include <stdio.h>

struct mobj *msg_param_mobj_from_noncontig(paddr_t buf_ptr, size_t size,
					   uint64_t shm_ref, bool map_buffer)
{
	return shm_ref;
}

void mobj_reg_shm_unguard(struct mobj *mobj)
{
}
