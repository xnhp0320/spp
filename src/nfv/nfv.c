/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 */

#include <arpa/inet.h>
#include <getopt.h>

#include <rte_eth_ring.h>
#include <rte_eth_vhost.h>
#include <rte_memzone.h>

#include "common.h"

static sig_atomic_t on = 1;

/*
 * our client id number - tells us which rx queue to read, and NIC TX
 * queue to write to.
 */
static uint16_t client_id;
static char *server_ip;
static int server_port;

static enum cmd_type cmd = STOP;

static struct port_map port_map[RTE_MAX_ETHPORTS];

/* the port details */
struct port_info *ports;

static struct port ports_fwd_array[RTE_MAX_ETHPORTS];

/* It is used to convert port name from string type to enum */
struct porttype_map {
	const char     *port_name;
	enum port_type port_type;
};

struct porttype_map portmap[] = {
	{ .port_name = "phy",   .port_type = PHY, },
	{ .port_name = "ring",  .port_type = RING, },
	{ .port_name = "vhost", .port_type = VHOST, },
	{ .port_name = "pcap", .port_type = PCAP, },
	{ .port_name = "nullpmd", .port_type = NULLPMD, },
	{ .port_name = NULL,    .port_type = UNDEF, },
};

/* Return a type of port as a enum member of porttype_map structure. */
static enum port_type get_port_type(char *portname)
{
	for (int i = 0; portmap[i].port_name != NULL; i++) {
		const char *port_name = portmap[i].port_name;
		if (strncmp(portname, port_name, strlen(port_name)) == 0)
			return portmap[i].port_type;
	}
	return UNDEF;
}

/*
 * print a usage message
 */
static void
usage(const char *progname)
{
	RTE_LOG(INFO, APP,
		"Usage: %s [EAL args] -- -n <client_id>\n\n", progname);
}

/*
 * Parse the application arguments to the client app.
 */
static int
parse_app_args(int argc, char *argv[])
{
	int option_index, opt;
	char **argvopt = argv;
	const char *progname = argv[0];
	static struct option lgopts[] = { {0} };
	int ret;

	while ((opt = getopt_long(argc, argvopt, "n:s:", lgopts,
			&option_index)) != EOF) {
		switch (opt) {
		case 'n':
			if (parse_num_clients(&client_id, optarg) != 0) {
				usage(progname);
				return -1;
			}
			break;
		case 's':
			ret = parse_server(&server_ip, &server_port, optarg);
			if (ret != 0) {
				usage(progname);
				return -1;
			}
			break;
		default:
			usage(progname);
			return -1;
		}
	}

	return 0;
}

static void
forward(void)
{
	uint16_t nb_rx;
	uint16_t nb_tx;
	int in_port;
	int out_port;
	uint16_t buf;
	int i;

	/* Go through every possible port numbers*/
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		struct rte_mbuf *bufs[MAX_PKT_BURST];

		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		if (ports_fwd_array[i].out_port_id == PORT_RESET)
			continue;

		/* if status active, i count is in port*/
		in_port = i;
		out_port = ports_fwd_array[i].out_port_id;

		/* Get burst of RX packets, from first port of pair. */
		/*first port rx, second port tx*/
		nb_rx = ports_fwd_array[in_port].rx_func(in_port, 0, bufs,
			MAX_PKT_BURST);
		if (unlikely(nb_rx == 0))
			continue;

		port_map[in_port].stats->rx += nb_rx;

		/* Send burst of TX packets, to second port of pair. */
		nb_tx = ports_fwd_array[out_port].tx_func(out_port, 0, bufs,
			nb_rx);

		port_map[out_port].stats->tx += nb_tx;

		/* Free any unsent packets. */
		if (unlikely(nb_tx < nb_rx)) {
			port_map[out_port].stats->tx_drop += nb_rx - nb_tx;
			for (buf = nb_tx; buf < nb_rx; buf++)
				rte_pktmbuf_free(bufs[buf]);
		}
	}
}

/* main processing loop */
static void
nfv_loop(void)
{
	unsigned int lcore_id = rte_lcore_id();

	RTE_LOG(INFO, APP, "entering main loop on lcore %u\n", lcore_id);

	while (1) {
		if (unlikely(cmd == STOP)) {
			sleep(1);
			/*RTE_LOG(INFO, APP, "Idling\n");*/
			continue;
		} else if (cmd == FORWARD) {
			forward();
		}
	}
}

/* leading to nfv processing loop */
static int
main_loop(void *dummy __rte_unused)
{
	nfv_loop();
	return 0;
}

static void
port_map_init_one(unsigned int i)
{
	port_map[i].id = PORT_RESET;
	port_map[i].port_type = UNDEF;
	port_map[i].stats = &port_map[i].default_stats;
}

static void
port_map_init(void)
{
	unsigned int i;

	for (i = 0; i < RTE_MAX_ETHPORTS; i++)
		port_map_init_one(i);
}

static void
forward_array_init_one(unsigned int i)
{
	ports_fwd_array[i].in_port_id = PORT_RESET;
	ports_fwd_array[i].out_port_id = PORT_RESET;
}

/* initialize forward array with default value*/
static void
forward_array_init(void)
{
	unsigned int i;

	/* initialize port forward array*/
	for (i = 0; i < RTE_MAX_ETHPORTS; i++)
		forward_array_init_one(i);
}

static void
forward_array_reset(void)
{
	unsigned int i;

	/* initialize port forward array*/
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (ports_fwd_array[i].in_port_id != PORT_RESET) {
			ports_fwd_array[i].out_port_id = PORT_RESET;
			RTE_LOG(INFO, APP, "Port ID %d\n", i);
			RTE_LOG(INFO, APP, "out_port_id %d\n",
				ports_fwd_array[i].out_port_id);
		}
	}
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

static void
forward_array_remove(int port_id)
{
	unsigned int i;

	/* Update ports_fwd_array */
	forward_array_init_one(port_id);

	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		if (ports_fwd_array[i].out_port_id == port_id) {
			ports_fwd_array[i].out_port_id = PORT_RESET;
			break;
		}
	}
}

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

		dev_detach_by_port_id(port_id);

	} else if (!strcmp(p_type, "ring")) {
		RTE_LOG(DEBUG, APP, "Del ring id %d\n", p_id);
		RTE_LOG(INFO, APP, "Del ring id %d\n", p_id);
		port_id = find_port_id(p_id, RING);
		RTE_LOG(INFO, APP, "Del port id %d\n", port_id);
		if (port_id == PORT_RESET)
			return -1;

		rte_eth_dev_stop(port_id);
		rte_eth_dev_close(port_id);

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

/* Return 0 if invalid */
static int
is_valid_port(uint16_t port_id)
{
	if (port_id > RTE_MAX_ETHPORTS)
		return 0;

	return port_map[port_id].id != PORT_RESET;
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
	/* 1201 add */
	uint16_t port_id = 65535;
	char name[RTE_ETH_NAME_MAX_LEN];
	snprintf(name, RTE_ETH_NAME_MAX_LEN - 1, "net_ring_%s", ring->name);
	RTE_LOG(INFO, APP, "name=%s\n", name);
	res = rte_eth_dev_get_port_by_name(name, &port_id);
	RTE_LOG(INFO, APP, "res=%d port_id=%d\n", res, port_id);
	if (port_id == 65535) {
		res = rte_eth_from_ring(ring);
		if (res < 0) {
			RTE_LOG(ERR, APP,
				"Cannot create eth dev with "
				"rte_eth_from_ring()\n");
			return -1;
		}
	} else {
		res = port_id;
		rte_eth_dev_start(res);
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
 * Create an empty rx pcap file to given path if it does not exit
 * Return 0 for succeeded, or -1 for failed.
 */
static int
create_pcap_rx(char *rx_fpath)
{
	int res;
	FILE *tmp_fp;
	char cmd_str[256];

	// empty file is required for 'text2pcap' command for
	// creating a pcap file.
	char template[] = "/tmp/spp-emptyfile.txt";

	// create empty file if it is not exist
	tmp_fp = fopen(template, "r");
	if (tmp_fp == NULL) {
		(tmp_fp = fopen(template, "w"));
		if (tmp_fp == NULL) {
			RTE_LOG(ERR, APP, "Failed to open %s\n", template);
			return -1;
		}
	}

	sprintf(cmd_str, "text2pcap %s %s", template, rx_fpath);
	res = system(cmd_str);
	if (res != 0) {
		RTE_LOG(ERR, APP,
				"Failed to create pcap device %s\n",
				rx_fpath);
		return -1;
	}
	RTE_LOG(INFO, APP, "PCAP device created\n");
	fclose(tmp_fp);
	return 0;
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
	else
		port_id = (uint16_t) res;

	port_map[port_id].id = p_id;
	port_map[port_id].port_type = type;
	port_map[port_id].stats = &ports->client_stats[p_id];

	/* Update ports_fwd_array with port id */
	ports_fwd_array[port_id].in_port_id = port_id;

	return 0;
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

/*
 * Application main function - loops through
 * receiving and processing packets. Never returns
 */
int
main(int argc, char *argv[])
{
	const struct rte_memzone *mz;
	int sock = SOCK_RESET;
	unsigned int lcore_id;
	unsigned int nb_ports;
	int connected = 0;
	char str[MSG_SIZE];
	unsigned int i;
	int flg_exit;  // used as res of parse_command() to exit if -1
	int ret;

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		return -1;

	rte_log_set_level(RTE_LOGTYPE_EAL, RTE_LOG_DEBUG);
	argc -= ret;
	argv += ret;

	if (parse_app_args(argc, argv) < 0)
		rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");

	/* initialize port forward array*/
	forward_array_init();
	port_map_init();

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports > RTE_MAX_ETHPORTS)
		nb_ports = RTE_MAX_ETHPORTS;

	/* set up array for port data */
	if (rte_eal_process_type() == RTE_PROC_SECONDARY) {
		mz = rte_memzone_lookup(MZ_PORT_INFO);
		if (mz == NULL)
			rte_exit(EXIT_FAILURE,
				"Cannot get port info structure\n");
		ports = mz->addr;
	} else { /* RTE_PROC_PRIMARY */
		mz = rte_memzone_reserve(MZ_PORT_INFO, sizeof(*ports),
			rte_socket_id(), NO_FLAGS);
		if (mz == NULL)
			rte_exit(EXIT_FAILURE,
				"Cannot reserve memzone for port info\n");
		memset(mz->addr, 0, sizeof(*ports));
		ports = mz->addr;
	}

	RTE_LOG(INFO, APP, "Number of Ports: %d\n", nb_ports);

	/* update port_forward_array with active port */
	for (i = 0; i < nb_ports; i++) {
		if (!rte_eth_dev_is_valid_port(i))
			continue;

		/* Update ports_fwd_array with phy port*/
		ports_fwd_array[i].in_port_id = i;
		port_map[i].port_type = PHY;
		port_map[i].id = i;
		port_map[i].stats = &ports->port_stats[i];
	}

	lcore_id = 0;
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch(main_loop, NULL, lcore_id);
	}

	RTE_LOG(INFO, APP, "My ID %d start handling message\n", client_id);
	RTE_LOG(INFO, APP, "[Press Ctrl-C to quit ...]\n");

	/* send and receive msg loop */
	while (on) {
		ret = do_connection(&connected, &sock);
		if (ret < 0) {
			sleep(1);
			continue;
		}

		ret = do_receive(&connected, &sock, str);
		if (ret < 0)
			continue;

		RTE_LOG_DP(DEBUG, APP, "Received string: %s\n", str);
		RTE_LOG(DEBUG, APP, "Received string: %s\n", str);

		flg_exit = parse_command(str);

		/*Send the message back to client*/
		ret = do_send(&connected, &sock, str);

		if (flg_exit < 0)  /* terminate process if exit is called */
			break;
		else if (ret < 0)
			continue;
	}

	/* exit */
	close(sock);
	sock = SOCK_RESET;
	RTE_LOG(INFO, APP, "spp_nfv exit.\n");
	return 0;
}
