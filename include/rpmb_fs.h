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

#ifndef RPMB_FS_H
#define RPMB_FS_H

#include <kernel/thread.h>

struct rpmb_req {
	uint16_t cmd;
#define RPMB_CMD_DATA_REQ      0x00
#define RPMB_CMD_GET_DEV_INFO  0x01
	uint16_t dev_id;
	uint16_t block_count;
	/* Optional data frames (rpmb_data_frame) follow */
};

TEE_Result rpmb_data_request(size_t num_params, struct thread_param *params);

TEE_Result rpmb_get_dev_info(size_t num_params, struct thread_param *params);

#endif /* RPMB_FS_H */
