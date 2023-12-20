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

#include <initcall.h>
#include <rpmb_fs.h>
#include <stdlib.h>
#include <tee_api_types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <mm/mobj.h>
#include <netinet/in.h>
#include <nuttx/drivers/rpmsgblk.h>
#include <nuttx/mmcsd.h>
#include <sys/ioctl.h>

/* Request */
#define RPMB_REQ_DATA(req) ((void *)((struct rpmb_req *)(req) + 1))
#define RPMB_CID_SZ 16
#define RPMB_DATA_FRAME_SIZE 512

/* Response to device info request */
struct rpmb_dev_info {
	uint8_t cid[RPMB_CID_SZ];
	uint8_t rpmb_size_mult;	/* EXT CSD-slice 168: RPMB Size */
	uint8_t rel_wr_sec_c;	/* EXT CSD-slice 222: Reliable Write Sector */
				/*                    Count */
	uint8_t ret_code;
#define RPMB_CMD_GET_DEV_INFO_RET_OK     0x00
#define RPMB_CMD_GET_DEV_INFO_RET_ERROR  0x01
};

/*
 * This structure is shared with OP-TEE and the MMC ioctl layer.
 * It is the "data frame for RPMB access" defined by JEDEC, minus the
 * start and stop bits.
 */
struct rpmb_data_frame {
	uint8_t stuff_bytes[196];
	uint8_t key_mac[32];
	uint8_t data[256];
	uint8_t nonce[16];
	uint32_t write_counter;
	uint16_t address;
	uint16_t block_count;
	uint16_t op_result;
#define RPMB_RESULT_OK					0x00
#define RPMB_RESULT_GENERAL_FAILURE			0x01
#define RPMB_RESULT_AUTH_FAILURE			0x02
#define RPMB_RESULT_ADDRESS_FAILURE			0x04
#define RPMB_RESULT_AUTH_KEY_NOT_PROGRAMMED		0x07
	uint16_t msg_type;
#define RPMB_MSG_TYPE_REQ_AUTH_KEY_PROGRAM		0x0001
#define RPMB_MSG_TYPE_REQ_WRITE_COUNTER_VAL_READ	0x0002
#define RPMB_MSG_TYPE_REQ_AUTH_DATA_WRITE		0x0003
#define RPMB_MSG_TYPE_REQ_AUTH_DATA_READ		0x0004
#define RPMB_MSG_TYPE_REQ_RESULT_READ			0x0005
#define RPMB_MSG_TYPE_RESP_AUTH_KEY_PROGRAM		0x0100
#define RPMB_MSG_TYPE_RESP_WRITE_COUNTER_VAL_READ	0x0200
#define RPMB_MSG_TYPE_RESP_AUTH_DATA_WRITE		0x0300
#define RPMB_MSG_TYPE_RESP_AUTH_DATA_READ		0x0400
};

static pthread_mutex_t rpmb_mutex = PTHREAD_MUTEX_INITIALIZER;

/* mmc_ioc_cmd.opcode */
#define MMC_READ_MULTIPLE_BLOCK		18
#define MMC_WRITE_MULTIPLE_BLOCK	25

/* mmc_ioc_cmd.flags */
#define MMC_RSP_PRESENT		(1 << 0)
#define MMC_RSP_136     	(1 << 1)		/* 136 bit response */
#define MMC_RSP_CRC		(1 << 2)		/* Expect valid CRC */
#define MMC_RSP_OPCODE		(1 << 4)		/* Response contains opcode */

#define MMC_RSP_R1      	(MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)

#define MMC_RSP_SPI_S1		(1 << 7)		/* one status byte */
#define MMC_RSP_SPI_R1		(MMC_RSP_SPI_S1)

#define MMC_CMD_ADTC		(1 << 5)		/* Addressed data transfer command */

/* mmc_ioc_cmd.write_flag */
#define MMC_CMD23_ARG_REL_WR	(1 << 31)		/* CMD23 reliable write */

#define MMC_SEND_EXT_CSD	8			/* adtc				R1  */

/* Maximum number of commands used in a multiple ioc command request */
#define RPMB_MAX_IOC_MULTI_CMDS	3

#define IOCTL(fd, request, ...)						\
	({								\
		int ret;						\
		ret = ioctl((fd), (request), ##__VA_ARGS__);		\
		if (ret < 0)						\
			EMSG("ioctl ret=%d errno=%d", ret, errno);	\
		ret;							\
	})

static void rpmb_mutex_lock(pthread_mutex_t *mu)
{
	int e = pthread_mutex_lock(mu);

	if (e) {
		EMSG("pthread_mutex_lock: %s", strerror(e));
		EMSG("terminating...");
		exit(EXIT_FAILURE);
	}
}

static void rpmb_mutex_unlock(pthread_mutex_t *mu)
{
	int e = pthread_mutex_unlock(mu);

	if (e) {
		EMSG("pthread_mutex_unlock: %s", strerror(e));
		EMSG("terminating...");
		exit(EXIT_FAILURE);
	}
}

static void convert_param_to_mobj(struct thread_param *param, struct mobj *obj)
{
	obj->buffer = param->u.memref.mobj->buffer + param->u.memref.offs;
	obj->size = param->u.memref.size;
}

static inline void set_mmc_io_cmd(struct mmc_ioc_cmd *cmd, unsigned int blocks,
				  uint32_t opcode, int write_flag)
{
	cmd->blksz = 512;
	cmd->blocks = blocks;
	cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	cmd->opcode = opcode;
	cmd->write_flag = write_flag;
}

static int mmc_rpmb_fd(uint16_t dev_id)
{
	static int id;
	static int fd = -1;
	char path[PATH_MAX] = { 0 };

	DMSG("dev_id = %u", dev_id);
	if (fd < 0) {
#ifdef CONFIG_BLK_RPMSG
		rpmsgblk_register(CONFIG_OPTEE_RPMB_REMOTE_CPU, "/dev/mmcsd0rpmb", NULL);
#endif
		snprintf(path, sizeof(path), "/dev/mmcsd%urpmb", dev_id);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			EMSG("Could not open %s (%s)", path, strerror(errno));
			return -1;
		}
		id = dev_id;
	}
	if (id != dev_id) {
		EMSG("Only one MMC device is supported");
		return -1;
	}
	return fd;
}

static uint32_t rpmb_data_req(int fd, struct rpmb_data_frame *req_frm,
			      size_t req_nfrm, struct rpmb_data_frame *rsp_frm,
			      size_t rsp_nfrm)
{
	TEE_Result res = TEE_SUCCESS;
	int st = 0;
	size_t i = 0;
	uint16_t msg_type = ntohs(req_frm->msg_type);
	struct mmc_ioc_multi_cmd *mcmd = NULL;
	struct mmc_ioc_cmd *cmd = NULL;

	for (i = 1; i < req_nfrm; i++) {
		if (req_frm[i].msg_type != msg_type) {
			EMSG("All request frames shall be of the same type");
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	DMSG("Req: %zu frame(s) of type 0x%04x", req_nfrm, msg_type);
	DMSG("Rsp: %zu frame(s)", rsp_nfrm);

	mcmd = (struct mmc_ioc_multi_cmd *)
		calloc(1, sizeof(struct mmc_ioc_multi_cmd) +
		       RPMB_MAX_IOC_MULTI_CMDS * sizeof(struct mmc_ioc_cmd));
	if (!mcmd)
		return TEE_ERROR_OUT_OF_MEMORY;

	DMSG("msg_type = %d", msg_type);
	switch(msg_type) {
	case RPMB_MSG_TYPE_REQ_AUTH_KEY_PROGRAM:
	case RPMB_MSG_TYPE_REQ_AUTH_DATA_WRITE:
		if (rsp_nfrm != 1) {
			EMSG("Expected only one response frame");
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		mcmd->num_of_cmds = 3;

		/* Send write request frame(s) */
		cmd = &mcmd->cmds[0];
		set_mmc_io_cmd(cmd, req_nfrm, MMC_WRITE_MULTIPLE_BLOCK,
			       1 | MMC_CMD23_ARG_REL_WR);

		/*
		 * Black magic: tested on a HiKey board with a HardKernel eMMC
		 * module. When postsleep values are zero, the kernel logs
		 * random errors: "mmc_blk_ioctl_cmd: Card Status=0x00000E00"
		 * and ioctl() fails.
		 */
		cmd->postsleep_min_us = 20000;
		cmd->postsleep_max_us = 50000;
		mmc_ioc_cmd_set_data((*cmd), (uintptr_t)req_frm);

		/* Send result request frame */
		cmd = &mcmd->cmds[1];
		set_mmc_io_cmd(cmd, req_nfrm, MMC_WRITE_MULTIPLE_BLOCK, 1);
		memset(rsp_frm, 0, 1);
		rsp_frm->msg_type = htons(RPMB_MSG_TYPE_REQ_RESULT_READ);
		mmc_ioc_cmd_set_data((*cmd), (uintptr_t)rsp_frm);

		/* Read response frame */
		cmd = &mcmd->cmds[2];
		set_mmc_io_cmd(cmd, rsp_nfrm, MMC_READ_MULTIPLE_BLOCK, 0);
		mmc_ioc_cmd_set_data((*cmd), (uintptr_t)rsp_frm);
		break;

	case RPMB_MSG_TYPE_REQ_WRITE_COUNTER_VAL_READ:
		if (rsp_nfrm != 1) {
			EMSG("Expected only one response frame");
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

	case RPMB_MSG_TYPE_REQ_AUTH_DATA_READ:
		if (req_nfrm != 1) {
			EMSG("Expected only one request frame");
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		mcmd->num_of_cmds = 2;

		/* Send request frame */
		cmd = &mcmd->cmds[0];
		set_mmc_io_cmd(cmd, req_nfrm, MMC_WRITE_MULTIPLE_BLOCK, 1);
		mmc_ioc_cmd_set_data((*cmd), (uintptr_t)req_frm);

		/* Read response frames */
		cmd = &mcmd->cmds[1];
		set_mmc_io_cmd(cmd, rsp_nfrm, MMC_READ_MULTIPLE_BLOCK, 0);
		mmc_ioc_cmd_set_data((*cmd), (uintptr_t)rsp_frm);
		break;

	default:
		EMSG("Unsupported message type: %d", msg_type);
		res = TEE_ERROR_GENERIC;
		goto out;
	}

	st = IOCTL(fd, MMC_IOC_MULTI_CMD, mcmd);
	if (st < 0)
		res = TEE_ERROR_GENERIC;

out:
	free(mcmd);
	return res;
}

static uint32_t rpmb_data_request_internal(void *req, size_t req_size,
					      void *rsp, size_t rsp_size)
{
	struct rpmb_req *sreq = req;
	size_t req_nfrm = 0;
	size_t rsp_nfrm = 0;
	uint16_t dev_id = sreq->dev_id;
	uint32_t res = 0;
	int fd = 0;

	if (req_size < sizeof(*sreq))
		return TEE_ERROR_BAD_PARAMETERS;

	req_nfrm = (req_size - sizeof(struct rpmb_req)) / 512;
	rsp_nfrm = rsp_size / 512;
	fd = mmc_rpmb_fd(dev_id);
	if (fd < 0)
		return TEE_ERROR_BAD_PARAMETERS;
	res = rpmb_data_req(fd, RPMB_REQ_DATA(req), req_nfrm, rsp,
				rsp_nfrm);

	return res;
}

static uint32_t read_cid(uint16_t dev_id, uint8_t *cid)
{
	TEE_Result res = TEE_SUCCESS;
	char path[PATH_MAX] = {0};
	int fd;
	size_t n;

	snprintf(path, sizeof(path), "/proc/mmcsd/cid%d", dev_id);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		EMSG("Could not open %s (%s)", path, strerror(errno));
		return fd;
	}

	n = read(fd, cid, RPMB_CID_SZ);
	if (n != RPMB_CID_SZ) {
		EMSG("Read CID error");
		if (errno)
			EMSG("%s", strerror(errno));

		res = TEE_ERROR_NO_DATA;
	}

	close(fd);
	return res;
}

static TEE_Result read_extcsd(int fd, uint8_t *ext_csd)
{
	TEE_Result res = TEE_SUCCESS;
	struct mmc_ioc_cmd idata;
	memset(&idata, 0, sizeof(idata));
	memset(ext_csd, 0, RPMB_DATA_FRAME_SIZE);
	idata.write_flag = 0;
	idata.opcode = MMC_SEND_EXT_CSD;
	idata.arg = 0;
	idata.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
	idata.blksz = RPMB_DATA_FRAME_SIZE;
	idata.blocks = 1;
	mmc_ioc_cmd_set_data(idata, ext_csd);

	res = IOCTL(fd, MMC_IOC_CMD, &idata);
	if (res < 0)
		res = TEE_ERROR_GENERIC;

	return res;
}

static uint32_t rpmb_get_dev_info_internal(uint16_t dev_id, struct rpmb_dev_info *info)
{
	TEE_Result res = TEE_SUCCESS;
	uint8_t ext_csd[RPMB_DATA_FRAME_SIZE];
	int fd;

	fd = mmc_rpmb_fd(dev_id);
	if (fd < 0)
		return TEE_ERROR_BAD_PARAMETERS;

	res = read_cid(dev_id, info->cid);
	if (res != TEE_SUCCESS)
		return res;

	res = read_extcsd(fd, ext_csd);
	if (res != TEE_SUCCESS)
		return res;

	info->rpmb_size_mult = ext_csd[168];
	info->rel_wr_sec_c = ext_csd[222];
	info->ret_code = RPMB_CMD_GET_DEV_INFO_RET_OK;

	return res;
}

TEE_Result rpmb_data_request(size_t num_params, struct thread_param *params)
{
	uint32_t ret = 0;
	struct mobj req;
	struct mobj rsp;

	if (num_params != 2 ||
		params[0].attr != THREAD_PARAM_ATTR_MEMREF_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_OUT) {
			return TEE_ERROR_BAD_PARAMETERS;
	}

	memset(&req, 0, sizeof(req));
	memset(&rsp, 0, sizeof(rsp));

	convert_param_to_mobj(&params[0], &req);
	convert_param_to_mobj(&params[1], &rsp);

	rpmb_mutex_lock(&rpmb_mutex);
	ret = rpmb_data_request_internal(req.buffer, req.size, rsp.buffer, rsp.size);
	rpmb_mutex_unlock(&rpmb_mutex);
	return ret;
}

TEE_Result rpmb_get_dev_info(size_t num_params, struct thread_param *params)
{
	uint32_t ret = 0;
	struct rpmb_req *req;
	struct rpmb_rsp *rsp;

	if (num_params != 2 ||
		params[0].attr != THREAD_PARAM_ATTR_MEMREF_IN ||
		params[1].attr != THREAD_PARAM_ATTR_MEMREF_OUT) {
			return TEE_ERROR_BAD_PARAMETERS;
	}

	req = (struct rpmb_req *)params[0].u.memref.mobj->buffer;
	rsp = (struct rpmb_rsp *)params[1].u.memref.mobj->buffer;

	rpmb_mutex_lock(&rpmb_mutex);
	ret = rpmb_get_dev_info_internal(req->dev_id, (struct rpmb_dev_info *)rsp);
	rpmb_mutex_unlock(&rpmb_mutex);
	return ret;
}
