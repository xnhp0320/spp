/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _NFV_COMMANDS_H_
#define _NFV_COMMANDS_H_

#include "shared/secondary/add_port.h"

#define RTE_LOGTYPE_SPP_NFV RTE_LOGTYPE_USER1

static int
do_del(char *res_uid)
{
	uint16_t port_id = PORT_RESET;
	char *p_type;
	int p_id;
	int res;

	res = parse_resource_uid(res_uid, &p_type, &p_id);
	if (res < 0) {
		RTE_LOG(ERR, SPP_NFV,
			"Failed to parse resource UID\n");
		return -1;
	}

	if (!strcmp(p_type, "vhost")) {
		port_id = find_port_id(p_id, VHOST);
		if (port_id == PORT_RESET)
			return -1;

	} else if (!strcmp(p_type, "ring")) {
		RTE_LOG(DEBUG, SPP_NFV, "Del ring id %d\n", p_id);
		port_id = find_port_id(p_id, RING);
		if (port_id == PORT_RESET)
			return -1;

		dev_detach_by_port_id(port_id);

	} else if (!strcmp(p_type, "pcap")) {
		port_id = find_port_id(p_id, PCAP);
		if (port_id == PORT_RESET)
			return -1;

		dev_detach_by_port_id(port_id);

	} else if (!strcmp(p_type, "nullpmd")) {
		port_id = find_port_id(p_id, NULLPMD);
		if (port_id == PORT_RESET)
			return -1;

		dev_detach_by_port_id(port_id);

	}

	forward_array_remove(port_id);
	port_map_init_one(port_id);

	return 0;
}

/**
 * Add a port to this process. Port is described with resource UID which is a
 * combination of port type and ID like as 'ring:0'.
 */
static int
do_add(char *res_uid)
{
	enum port_type type = UNDEF;
	uint16_t port_id = PORT_RESET;
	char *p_type;
	int p_id;
	int res;

	res = parse_resource_uid(res_uid, &p_type, &p_id);
	if (res < 0)
		return -1;

	if (!strcmp(p_type, "vhost")) {
		type = VHOST;
		res = add_vhost_pmd(p_id);

	} else if (!strcmp(p_type, "ring")) {
		type = RING;
		res = add_ring_pmd(p_id);

	} else if (!strcmp(p_type, "pcap")) {
		type = PCAP;
		res = add_pcap_pmd(p_id);

	} else if (!strcmp(p_type, "nullpmd")) {
		type = NULLPMD;
		res = add_null_pmd(p_id);
	}

	if (res < 0)
		return -1;

	port_id = (uint16_t) res;
	port_map[port_id].id = p_id;
	port_map[port_id].port_type = type;
	port_map[port_id].stats = &ports->client_stats[p_id];

	/* Update ports_fwd_array with port id */
	ports_fwd_array[port_id].in_port_id = port_id;

	return 0;
}

static int
do_connection(int *connected, int *sock)
{
	static struct sockaddr_in servaddr;
	int ret = 0;

	if (*connected == 0) {
		if (*sock < 0) {
			RTE_LOG(INFO, SPP_NFV, "Creating socket...\n");
			*sock = socket(AF_INET, SOCK_STREAM, 0);
			if (*sock < 0)
				rte_exit(EXIT_FAILURE, "socket error\n");

			/*Create of the tcp socket*/
			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_addr.s_addr = inet_addr(server_ip);
			servaddr.sin_port = htons(server_port);
		}

		RTE_LOG(INFO,
			SPP_NFV, "Trying to connect ... socket %d\n", *sock);
		ret = connect(*sock, (struct sockaddr *) &servaddr,
				sizeof(servaddr));
		if (ret < 0) {
			RTE_LOG(ERR, SPP_NFV, "Connection Error");
			return ret;
		}

		RTE_LOG(INFO, SPP_NFV, "Connected\n");
		*connected = 1;
	}

	return ret;
}

/* Return -1 if exit command is called to terminate the process */
static int
parse_command(char *str)
{
	char *token_list[MAX_PARAMETER] = {NULL};
	int max_token = 0;
	int ret = 0;

	if (!str)
		return 0;

	/* tokenize user command from controller */
	token_list[max_token] = strtok(str, " ");
	while (token_list[max_token] != NULL) {
		RTE_LOG(DEBUG, SPP_NFV, "token %d = %s\n", max_token,
			token_list[max_token]);
		max_token++;
		token_list[max_token] = strtok(NULL, " ");
	}

	if (max_token == 0)
		return 0;

	if (!strcmp(token_list[0], "status")) {
		RTE_LOG(DEBUG, SPP_NFV, "status\n");
		memset(str, '\0', MSG_SIZE);
		if (cmd == FORWARD)
			get_sec_stats_json(str, client_id, "running",
					ports_fwd_array, port_map);
		else
			get_sec_stats_json(str, client_id, "idling",
					ports_fwd_array, port_map);

	} else if (!strcmp(token_list[0], "_get_client_id")) {
		memset(str, '\0', MSG_SIZE);
		sprintf(str, "%d", client_id);

	} else if (!strcmp(token_list[0], "_set_client_id")) {
		int id;

		if (spp_atoi(token_list[1], &id) >= 0)
			client_id = id;

	} else if (!strcmp(token_list[0], "exit")) {
		RTE_LOG(DEBUG, SPP_NFV, "exit\n");
		RTE_LOG(DEBUG, SPP_NFV, "stop\n");
		cmd = STOP;
		ret = -1;

	} else if (!strcmp(token_list[0], "stop")) {
		RTE_LOG(DEBUG, SPP_NFV, "stop\n");
		cmd = STOP;

	} else if (!strcmp(token_list[0], "forward")) {
		RTE_LOG(DEBUG, SPP_NFV, "forward\n");
		cmd = FORWARD;

	} else if (!strcmp(token_list[0], "add")) {
		RTE_LOG(DEBUG, SPP_NFV, "Received add command\n");
		if (do_add(token_list[1]) < 0)
			RTE_LOG(ERR, SPP_NFV, "Failed to do_add()\n");

	} else if (!strcmp(token_list[0], "patch")) {
		RTE_LOG(DEBUG, SPP_NFV, "patch\n");

		if (max_token <= 1)
			return 0;

		if (strncmp(token_list[1], "reset", 5) == 0) {
			/* reset forward array*/
			forward_array_reset();
		} else {
			uint16_t in_port;
			uint16_t out_port;

			if (max_token <= 2)
				return 0;

			char *in_p_type;
			char *out_p_type;
			int in_p_id;
			int out_p_id;

			parse_resource_uid(token_list[1], &in_p_type, &in_p_id);
			in_port = find_port_id(in_p_id,
					get_port_type(in_p_type));

			parse_resource_uid(token_list[2],
					&out_p_type, &out_p_id);
			out_port = find_port_id(out_p_id,
					get_port_type(out_p_type));

			if (in_port == PORT_RESET && out_port == PORT_RESET) {
				char err_msg[128];
				memset(err_msg, '\0', sizeof(err_msg));
				sprintf(err_msg, "%s '%s:%d' and '%s:%d'",
					"Patch not found, both of",
					in_p_type, in_p_id,
					out_p_type, out_p_id);
				RTE_LOG(ERR, SPP_NFV, "%s\n", err_msg);
			} else if (in_port == PORT_RESET) {
				char err_msg[128];
				memset(err_msg, '\0', sizeof(err_msg));
				sprintf(err_msg, "%s '%s:%d'",
					"Patch not found, in_port",
					in_p_type, in_p_id);
				RTE_LOG(ERR, SPP_NFV, "%s\n", err_msg);
			} else if (out_port == PORT_RESET) {
				char err_msg[128];
				memset(err_msg, '\0', sizeof(err_msg));
				sprintf(err_msg, "%s '%s:%d'",
					"Patch not found, out_port",
					out_p_type, out_p_id);
				RTE_LOG(ERR, SPP_NFV, "%s\n", err_msg);
			}

			if (add_patch(in_port, out_port) == 0)
				RTE_LOG(INFO, SPP_NFV,
					"Patched '%s:%d' and '%s:%d'\n",
					in_p_type, in_p_id,
					out_p_type, out_p_id);

			else
				RTE_LOG(ERR, SPP_NFV, "Failed to patch\n");
			ret = 0;
		}

	} else if (!strcmp(token_list[0], "del")) {
		RTE_LOG(DEBUG, SPP_NFV, "Received del command\n");

		cmd = STOP;

		if (do_del(token_list[1]) < 0)
			RTE_LOG(ERR, SPP_NFV, "Failed to do_del()\n");
	}

	return ret;
}

static int
do_receive(int *connected, int *sock, char *str)
{
	int ret;

	memset(str, '\0', MSG_SIZE);

	ret = recv(*sock, str, MSG_SIZE, 0);
	if (ret <= 0) {
		RTE_LOG(DEBUG, SPP_NFV, "Receive count: %d\n", ret);
		if (ret < 0)
			RTE_LOG(ERR, SPP_NFV, "Receive Fail");
		else
			RTE_LOG(INFO, SPP_NFV, "Receive 0\n");

		RTE_LOG(INFO, SPP_NFV, "Assume Server closed connection\n");
		close(*sock);
		*sock = SOCK_RESET;
		*connected = 0;
		return -1;
	}

	return 0;
}

static int
do_send(int *connected, int *sock, char *str)
{
	int ret;

	ret = send(*sock, str, MSG_SIZE, 0);
	if (ret == -1) {
		RTE_LOG(ERR, SPP_NFV, "send failed");
		*connected = 0;
		return -1;
	}

	RTE_LOG(INFO, SPP_NFV, "To Server: %s\n", str);

	return 0;
}

#endif // _NFV_COMMANDS_H_
