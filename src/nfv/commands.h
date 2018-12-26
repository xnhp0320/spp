/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018  Nippon Telegraph and Telephone Corporation.
 */

#ifndef NFV_COMMANDS_H
#define NFV_COMMANDS_H

#include "common.h"
#include "nfv.h"
#include "command_utils.h"

/*
 * Return actual port ID which is assigned by system internally, or PORT_RESET
 * if port is not found.
 */
static uint16_t
find_port_id(int id, enum port_type type)
{
	uint16_t port_id = PORT_RESET;
	uint16_t i;

	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (port_map[i].port_type != type)
			continue;

		if (port_map[i].id == id) {
			port_id = i;
			break;
		}
	}

	return port_id;
}

static int
do_del(char *res_uid)
{
	uint16_t port_id = PORT_RESET;
	char *p_type;
	int p_id;
	int res;

	res = parse_resource_uid(res_uid, &p_type, &p_id);
	if (res < 0) {
		RTE_LOG(ERR, APP,
			"Failed to parse resource UID\n");
		return -1;
	}

	if (!strcmp(p_type, "vhost")) {
		port_id = find_port_id(p_id, VHOST);
		if (port_id == PORT_RESET)
			return -1;

	} else if (!strcmp(p_type, "ring")) {
		RTE_LOG(DEBUG, APP, "Del ring id %d\n", p_id);
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

/* Return -1 as an error if given patch is invalid */
static int
add_patch(uint16_t in_port, uint16_t out_port)
{
	if (!is_valid_port(in_port) || !is_valid_port(out_port))
		return -1;

	/* Populate in port data */
	ports_fwd_array[in_port].in_port_id = in_port;
	ports_fwd_array[in_port].rx_func = &rte_eth_rx_burst;
	ports_fwd_array[in_port].tx_func = &rte_eth_tx_burst;
	ports_fwd_array[in_port].out_port_id = out_port;

	/* Populate out port data */
	ports_fwd_array[out_port].in_port_id = out_port;
	ports_fwd_array[out_port].rx_func = &rte_eth_rx_burst;
	ports_fwd_array[out_port].tx_func = &rte_eth_tx_burst;

	RTE_LOG(DEBUG, APP, "STATUS: in port %d in_port_id %d\n", in_port,
		ports_fwd_array[in_port].in_port_id);
	RTE_LOG(DEBUG, APP, "STATUS: in port %d patch out port id %d\n",
		in_port, ports_fwd_array[in_port].out_port_id);
	RTE_LOG(DEBUG, APP, "STATUS: outport %d in_port_id %d\n", out_port,
		ports_fwd_array[out_port].in_port_id);

	return 0;
}

/*
 * Create ring PMD with given ring_id.
 */
static int
add_ring_pmd(int ring_id)
{
	struct rte_ring *ring;
	int res;
	char rx_queue_name[32];  /* Prefix and number like as 'eth_ring_0' */

	memset(rx_queue_name, '\0', sizeof(rx_queue_name));
	sprintf(rx_queue_name, "%s", get_rx_queue_name(ring_id));

	/* Look up ring with provided ring_id */
	ring = rte_ring_lookup(rx_queue_name);
	if (ring == NULL) {
		RTE_LOG(ERR, APP,
			"Failed to get RX ring %s - is primary running?\n",
			rx_queue_name);
		return -1;
	}
	RTE_LOG(INFO, APP, "Looked up ring '%s'\n", rx_queue_name);

	/* create ring pmd*/
	res = rte_eth_from_ring(ring);
	if (res < 0) {
		RTE_LOG(ERR, APP,
			"Cannot create eth dev with rte_eth_from_ring()\n");
		return -1;
	}
	RTE_LOG(INFO, APP, "Created ring PMD: %d\n", res);

	return res;
}

static int
add_vhost_pmd(int index)
{
	struct rte_eth_conf port_conf = {
		.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
	};
	struct rte_mempool *mp;
	uint16_t vhost_port_id;
	int nr_queues = 1;
	const char *name;
	char devargs[64];
	char *iface;
	uint16_t q;
	int ret;
#define NR_DESCS 128

	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		rte_exit(EXIT_FAILURE, "Cannot get mempool for mbufs\n");

	/* eth_vhost0 index 0 iface /tmp/sock0 on numa 0 */
	name = get_vhost_backend_name(index);
	iface = get_vhost_iface_name(index);

	sprintf(devargs, "%s,iface=%s,queues=%d", name, iface, nr_queues);
	ret = dev_attach_by_devargs(devargs, &vhost_port_id);
	if (ret < 0)
		return ret;

	ret = rte_eth_dev_configure(vhost_port_id, nr_queues, nr_queues,
		&port_conf);
	if (ret < 0)
		return ret;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_rx_queue_setup(vhost_port_id, q, NR_DESCS,
			rte_eth_dev_socket_id(vhost_port_id), NULL, mp);
		if (ret < 0)
			return ret;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_tx_queue_setup(vhost_port_id, q, NR_DESCS,
			rte_eth_dev_socket_id(vhost_port_id), NULL);
		if (ret < 0)
			return ret;
	}

	/* Start the Ethernet port. */
	ret = rte_eth_dev_start(vhost_port_id);
	if (ret < 0)
		return ret;

	RTE_LOG(DEBUG, APP, "vhost port id %d\n", vhost_port_id);

	return vhost_port_id;
}

/*
 * Open pcap files with given index for rx and tx.
 * Index is given as a argument of 'patch' command.
 * This function returns a port ID if it is succeeded,
 * or negative int if failed.
 */
static int
add_pcap_pmd(int index)
{
	struct rte_eth_conf port_conf = {
		.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
	};

	struct rte_mempool *mp;
	const char *name;
	char devargs[256];
	uint16_t pcap_pmd_port_id;
	uint16_t nr_queues = 1;
	int ret;

	// PCAP file path
	char rx_fpath[128];
	char tx_fpath[128];

	FILE *rx_fp;

	sprintf(rx_fpath, PCAP_IFACE_RX, index);
	sprintf(tx_fpath, PCAP_IFACE_TX, index);

	// create rx pcap file if it does not exist
	rx_fp = fopen(rx_fpath, "r");
	if (rx_fp == NULL) {
		ret = create_pcap_rx(rx_fpath);
		if (ret < 0)
			return ret;
	}

	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		rte_exit(EXIT_FAILURE, "Cannon get mempool for mbuf\n");

	name = get_pcap_pmd_name(index);
	sprintf(devargs,
			"%s,rx_pcap=%s,tx_pcap=%s",
			name, rx_fpath, tx_fpath);
	ret = dev_attach_by_devargs(devargs, &pcap_pmd_port_id);

	if (ret < 0)
		return ret;

	ret = rte_eth_dev_configure(
			pcap_pmd_port_id, nr_queues, nr_queues, &port_conf);

	if (ret < 0)
		return ret;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	uint16_t q;
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_rx_queue_setup(
				pcap_pmd_port_id, q, NR_DESCS,
				rte_eth_dev_socket_id(pcap_pmd_port_id),
				NULL, mp);
		if (ret < 0)
			return ret;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_tx_queue_setup(
				pcap_pmd_port_id, q, NR_DESCS,
				rte_eth_dev_socket_id(pcap_pmd_port_id),
				NULL);
		if (ret < 0)
			return ret;
	}

	ret = rte_eth_dev_start(pcap_pmd_port_id);

	if (ret < 0)
		return ret;

	RTE_LOG(DEBUG, APP, "pcap port id %d\n", pcap_pmd_port_id);

	return pcap_pmd_port_id;
}

static int
add_null_pmd(int index)
{
	struct rte_eth_conf port_conf = {
			.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
	};

	struct rte_mempool *mp;
	const char *name;
	char devargs[64];
	uint16_t null_pmd_port_id;
	uint16_t nr_queues = 1;

	int ret;

	mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	if (mp == NULL)
		rte_exit(EXIT_FAILURE, "Cannon get mempool for mbuf\n");

	name = get_null_pmd_name(index);
	sprintf(devargs, "%s", name);
	ret = dev_attach_by_devargs(devargs, &null_pmd_port_id);
	if (ret < 0)
		return ret;

	ret = rte_eth_dev_configure(
			null_pmd_port_id, nr_queues, nr_queues,
			&port_conf);
	if (ret < 0)
		return ret;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	uint16_t q;
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_rx_queue_setup(
				null_pmd_port_id, q, NR_DESCS,
				rte_eth_dev_socket_id(
					null_pmd_port_id), NULL, mp);
		if (ret < 0)
			return ret;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_tx_queue_setup(
				null_pmd_port_id, q, NR_DESCS,
				rte_eth_dev_socket_id(
					null_pmd_port_id),
				NULL);
		if (ret < 0)
			return ret;
	}

	ret = rte_eth_dev_start(null_pmd_port_id);
	if (ret < 0)
		return ret;

	RTE_LOG(DEBUG, APP, "null port id %d\n", null_pmd_port_id);

	return null_pmd_port_id;
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
			RTE_LOG(INFO, APP, "Creating socket...\n");
			*sock = socket(AF_INET, SOCK_STREAM, 0);
			if (*sock < 0)
				rte_exit(EXIT_FAILURE, "socket error\n");

			/*Create of the tcp socket*/
			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_addr.s_addr = inet_addr(server_ip);
			servaddr.sin_port = htons(server_port);
		}

		RTE_LOG(INFO, APP, "Trying to connect ... socket %d\n", *sock);
		ret = connect(*sock, (struct sockaddr *) &servaddr,
				sizeof(servaddr));
		if (ret < 0) {
			RTE_LOG(ERR, APP, "Connection Error");
			return ret;
		}

		RTE_LOG(INFO, APP, "Connected\n");
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
		RTE_LOG(DEBUG, APP, "token %d = %s\n", max_token,
			token_list[max_token]);
		max_token++;
		token_list[max_token] = strtok(NULL, " ");
	}

	if (max_token == 0)
		return 0;

	if (!strcmp(token_list[0], "status")) {
		RTE_LOG(DEBUG, APP, "status\n");
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
		RTE_LOG(DEBUG, APP, "exit\n");
		RTE_LOG(DEBUG, APP, "stop\n");
		cmd = STOP;
		ret = -1;

	} else if (!strcmp(token_list[0], "stop")) {
		RTE_LOG(DEBUG, APP, "stop\n");
		cmd = STOP;

	} else if (!strcmp(token_list[0], "forward")) {
		RTE_LOG(DEBUG, APP, "forward\n");
		cmd = FORWARD;

	} else if (!strcmp(token_list[0], "add")) {
		RTE_LOG(DEBUG, APP, "Received add command\n");
		if (do_add(token_list[1]) < 0)
			RTE_LOG(ERR, APP, "Failed to do_add()\n");

	} else if (!strcmp(token_list[0], "patch")) {
		RTE_LOG(DEBUG, APP, "patch\n");

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
				RTE_LOG(ERR, APP, "%s\n", err_msg);
			} else if (in_port == PORT_RESET) {
				char err_msg[128];
				memset(err_msg, '\0', sizeof(err_msg));
				sprintf(err_msg, "%s '%s:%d'",
					"Patch not found, in_port",
					in_p_type, in_p_id);
				RTE_LOG(ERR, APP, "%s\n", err_msg);
			} else if (out_port == PORT_RESET) {
				char err_msg[128];
				memset(err_msg, '\0', sizeof(err_msg));
				sprintf(err_msg, "%s '%s:%d'",
					"Patch not found, out_port",
					out_p_type, out_p_id);
				RTE_LOG(ERR, APP, "%s\n", err_msg);
			}

			if (add_patch(in_port, out_port) == 0)
				RTE_LOG(INFO, APP,
					"Patched '%s:%d' and '%s:%d'\n",
					in_p_type, in_p_id,
					out_p_type, out_p_id);

			else
				RTE_LOG(ERR, APP, "Failed to patch\n");
			ret = 0;
		}

	} else if (!strcmp(token_list[0], "del")) {
		RTE_LOG(DEBUG, APP, "Received del command\n");

		cmd = STOP;

		if (do_del(token_list[1]) < 0)
			RTE_LOG(ERR, APP, "Failed to do_del()\n");
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
		RTE_LOG(DEBUG, APP, "Receive count: %d\n", ret);
		if (ret < 0)
			RTE_LOG(ERR, APP, "Receive Fail");
		else
			RTE_LOG(INFO, APP, "Receive 0\n");

		RTE_LOG(INFO, APP, "Assume Server closed connection\n");
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
		RTE_LOG(ERR, APP, "send failed");
		*connected = 0;
		return -1;
	}

	RTE_LOG(INFO, APP, "To Server: %s\n", str);

	return 0;
}

#endif
