/*
 * Copyright (C) 2023 Xiaomi Corporation
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netpacket/rpmsg.h>
#include <optee_msg.h>
#include <initcall.h>
#include <trace.h>
#include <tee/entry_std.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define OPTEE_MAX_PARAM_NUM      6
#define OPTEE_SERVER_REMOTE_PATH "optee"

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/


/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int optee_bind(void)
{
#if defined(CONFIG_OPTEE_SERVER_RPMSG)
	const int family = AF_RPMSG;
	const struct sockaddr_rpmsg addr = {
		.rp_family = AF_RPMSG,
		.rp_cpu = "",
		.rp_name = OPTEE_SERVER_REMOTE_PATH,
	};
	const socklen_t addrlen = sizeof(struct sockaddr_rpmsg);

	DMSG("socket address: --family=rpmsg --name=%s\n",
	     OPTEE_SERVER_REMOTE_PATH);
#elif defined(CONFIG_OPTEE_SERVER_LOCAL)
	const int family = AF_UNIX;
	const struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = OPTEE_SERVER_REMOTE_PATH,
	};
	const socklen_t addrlen = sizeof(struct sockaddr_un);

	DMSG("socket address: --family=unix --path=%s\n",
	     OPTEE_SERVER_REMOTE_PATH);
#endif

	int fd = socket(family, SOCK_STREAM, 0);
	if (fd < 0) {
		EMSG("socket failed(%d)\n", errno);
		return -1;
	}

	int ret = bind(fd, (const struct sockaddr *)&addr, addrlen);
	if (ret < 0) {
		EMSG("bind failed(%d)\n", errno);
		close(fd);
		return -1;
	}

	ret = listen(fd, SOMAXCONN);
	if (ret < 0) {
		EMSG("listen failed(%d)\n", errno);
		close(fd);
		return -1;
	}

	return fd;
}

static int optee_recv(int fd, void *msg, size_t size)
{
	while (size > 0) {
		ssize_t n = recv(fd, msg, size, 0);
		if (n <= 0) {
			EMSG("recv failed(%d)\n", errno);
			return -1;
		}

		msg += n;
		size -= n;
	}

	return 0;
}

static int optee_send(int fd, void *msg, size_t size)
{
	while (size > 0) {
		ssize_t n = send(fd, msg, size, 0);
		if (n <= 0) {
			EMSG("send failed(%d)\n", errno);
			return -1;
		}

		msg += n;
		size -= n;
	}

	return 0;
}

static void *optee_thread(void *arg)
{
	char buffer[OPTEE_MSG_GET_ARG_SIZE(OPTEE_MAX_PARAM_NUM)];
	struct optee_msg_arg *msg = (struct optee_msg_arg *)buffer;
	struct optee_msg_param *param = (struct optee_msg_param *)(msg + 1);

	void *shm_buf = NULL;
	size_t shm_buf_size = 0;

	int connfd = (intptr_t)arg;

	while (1) {
		/* Receive struct optee_msg_arg */
		int ret = optee_recv(connfd, msg, sizeof(*msg));
		if (ret < 0)
			break;

		if (msg->num_params > 0) {
			/* Receive struct optee_msg_param */
			ret = optee_recv(connfd, param,
					 sizeof(*param) * msg->num_params);
			if (ret < 0)
				break;
		}

		size_t shm_size[OPTEE_MAX_PARAM_NUM];
		size_t shm_total = 0;

		for (uint32_t i = 0; i < msg->num_params; i++) {
			uint32_t attr = param[i].attr & OPTEE_MSG_ATTR_TYPE_MASK;
			if (attr == OPTEE_MSG_ATTR_TYPE_RMEM_INOUT ||
			    attr == OPTEE_MSG_ATTR_TYPE_RMEM_INPUT ||
			    attr == OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT) {
				shm_size[i] = param[i].u.rmem.size;
				shm_total += param[i].u.rmem.size;
			}
		}

		void *shm_tmp = shm_buf;
		if (shm_total > shm_buf_size) {
			shm_tmp = realloc(shm_buf, shm_total);
			if (shm_tmp == NULL) {
				EMSG("realloc failed\n");
				break;
			}

			shm_buf = shm_tmp;
			shm_buf_size = shm_total;
		}

		ret = optee_recv(connfd, shm_tmp, shm_total);
		if (ret < 0)
			break;

		void *shm_end = shm_tmp + shm_total;
		for (uint32_t i = 0; i < msg->num_params; i++) {
			uint32_t attr = param[i].attr & OPTEE_MSG_ATTR_TYPE_MASK;
			if (attr == OPTEE_MSG_ATTR_TYPE_RMEM_INOUT ||
			    attr == OPTEE_MSG_ATTR_TYPE_RMEM_INPUT) {
				param[i].u.rmem.shm_ref = (uintptr_t)shm_tmp;
				shm_tmp += shm_size[i];
			} else if (attr == OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT) {
				shm_end -= shm_size[i];
				param[i].u.rmem.shm_ref = (uintptr_t)shm_end;
			}
		}

		/* Call optee-os entry function */
		ret = tee_entry_std(msg, msg->num_params);
		if (ret < 0) {
			EMSG("optee_ioctl failed(%d)\n", ret);
			break;
		}

		/* Send optee_msg_arg and optee_msg_param */
		ret = optee_send(connfd, msg,
				 OPTEE_MSG_GET_ARG_SIZE(msg->num_params));
		if (ret < 0)
			break;

		for (uint32_t i = 0; i < msg->num_params; i++) {
			uint32_t attr = param[i].attr & OPTEE_MSG_ATTR_TYPE_MASK;
			if (attr == OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT ||
			    attr == OPTEE_MSG_ATTR_TYPE_RMEM_INOUT) {
				shm_tmp = (const void *)(uintptr_t)param[i].u.rmem.shm_ref;
				/* Send inout and out data of shered memory */
				ret = optee_send(connfd, shm_tmp,
						 MIN(shm_size[i], param[i].u.rmem.size));
				if (ret < 0)
					break;
			}
		}
	}

	free(shm_buf);
	close(connfd);
	return 0;
}

static void optee_server(int fd)
{
	pthread_attr_t attr;

	int status = pthread_attr_init(&attr);
	if (status != 0) {
		EMSG("pthread_attr_init failed(%d)\n", status);
		return;
	}

	status = pthread_attr_setstacksize(&attr,
					   CONFIG_OPTEE_NATIVE_STACKSIZE);
	if (status != 0) {
		EMSG("pthread_attr_setstacksize failed(%d)\n", status);
		return;
	}

	status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (status != 0) {
		EMSG("pthread_attr_setdetachstate failed(%d)\n", status);
		return;
	}

	while (1) {
		DMSG("waiting tee client...\n");
		int newfd = accept(fd, NULL, NULL);
		if (newfd < 0)
			continue;
		DMSG("accepted, newfd: %d\n", newfd);

		status = pthread_create(NULL, &attr, optee_thread,
					(void *)(intptr_t)newfd);
		if (status != 0) {
			EMSG("pthread_create failed(%d)\n", status);
			close(newfd);
		}
	}
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
	/* Initialize optee-os modules */
	call_initcalls();

	int fd = optee_bind();
	if (fd >= 0) {
		optee_server(fd);
		close(fd);
	}

	return 0;
}
