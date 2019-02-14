/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2018 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_branch_prediction.h>

#include "shared/common.h"
#include "string_buffer.h"
#include "command_conn.h"

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_COMMAND_PROC RTE_LOGTYPE_USER2

/* one receive message size */
#define MESSAGE_BUFFER_BLOCK_SIZE 2048

/* controller's IP address */
static char g_controller_ip[128] = "";

/* controller's port number */
static int g_controller_port;

/* initialize command connection */
int
spp_command_conn_init(const char *controller_ip, int controller_port)
{
	strcpy(g_controller_ip, controller_ip);
	g_controller_port = controller_port;

	return SPP_RET_OK;
}

/* connect to controller */
int
spp_connect_to_controller(int *sock)
{
	static struct sockaddr_in controller_addr;
	int ret = SPP_RET_NG;
	int sock_flg = 0;

	if (likely(*sock >= 0))
		return SPP_RET_OK;

	/* create socket */
	RTE_LOG(INFO, SPP_COMMAND_PROC, "Creating socket...\n");
	*sock = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(*sock < 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Cannot create TCP socket. errno=%d\n", errno);
		return SPP_CONNERR_TEMPORARY;
	}

	memset(&controller_addr, 0, sizeof(controller_addr));
	controller_addr.sin_family = AF_INET;
	controller_addr.sin_addr.s_addr = inet_addr(g_controller_ip);
	controller_addr.sin_port = htons(g_controller_port);

	/* connect to */
	RTE_LOG(INFO, SPP_COMMAND_PROC, "Trying to connect ... socket=%d\n",
			*sock);
	ret = connect(*sock, (struct sockaddr *)&controller_addr,
			sizeof(controller_addr));
	if (unlikely(ret < 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Cannot connect to controller. errno=%d\n",
				errno);
		/* Wait to retry */
		usleep(CONN_RETRY_USEC);

		close(*sock);
		*sock = -1;
		return SPP_CONNERR_TEMPORARY;
	}

	RTE_LOG(INFO, SPP_COMMAND_PROC, "Connected\n");

	/* set non-blocking */
	sock_flg = fcntl(*sock, F_GETFL, 0);
	fcntl(*sock, F_SETFL, sock_flg | O_NONBLOCK);

	return SPP_RET_OK;
}

/* receive message */
int
spp_receive_message(int *sock, char **strbuf)
{
	int ret = SPP_RET_NG;
	int n_rx = 0;
	char *new_strbuf = NULL;

	char rx_buf[MESSAGE_BUFFER_BLOCK_SIZE];
	size_t rx_buf_sz = MESSAGE_BUFFER_BLOCK_SIZE;

	ret = recv(*sock, rx_buf, rx_buf_sz, 0);
	if (unlikely(ret <= 0)) {
		if (likely(ret == 0)) {
			RTE_LOG(INFO, SPP_COMMAND_PROC, "Controller has "
					"performed an shutdown.");
		} else if (unlikely(errno != EAGAIN && errno != EWOULDBLOCK)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Receive failure. errno=%d\n", errno);
		} else {
			/* no receive message */
			return SPP_RET_OK;
		}

		RTE_LOG(INFO, SPP_COMMAND_PROC, "Assume Server closed "
							"connection.\n");
		close(*sock);
		*sock = -1;
		return SPP_CONNERR_TEMPORARY;
	}

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "Receive message. count=%d\n", ret);
	n_rx = ret;

	new_strbuf = spp_strbuf_append(*strbuf, rx_buf, n_rx);
	if (unlikely(new_strbuf == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Cannot allocate memory for receive data.\n");
		return SPP_CONNERR_FATAL;
	}

	*strbuf = new_strbuf;

	return n_rx;
}

/* send message */
int
spp_send_message(int *sock, const char *message, size_t message_len)
{
	int ret = SPP_RET_NG;

	ret = send(*sock, message, message_len, 0);
	if (unlikely(ret == -1)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC, "Send failure. ret=%d\n", ret);
		close(*sock);
		*sock = -1;
		return SPP_CONNERR_TEMPORARY;
	}

	return SPP_RET_OK;
}
