/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _NFV_COMMAND_UTILS_H_
#define _NFV_COMMAND_UTILS_H_

#include "common.h"
#include "secondary.h"

#define RTE_LOGTYPE_SPP_NFV RTE_LOGTYPE_USER1

// The number of receive descriptors to allocate for the receive ring.
#define NR_DESCS 128

#define PCAP_IFACE_RX "/tmp/spp-rx%d.pcap"
#define PCAP_IFACE_TX "/tmp/spp-tx%d.pcap"

static void
forward_array_init_one(unsigned int i)
{
	ports_fwd_array[i].in_port_id = PORT_RESET;
	ports_fwd_array[i].out_port_id = PORT_RESET;
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

/* Return 0 if invalid */
static int
is_valid_port(uint16_t port_id)
{
	if (port_id > RTE_MAX_ETHPORTS)
		return 0;

	return port_map[port_id].id != PORT_RESET;
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

	RTE_LOG(DEBUG, SPP_NFV, "STATUS: in port %d in_port_id %d\n", in_port,
		ports_fwd_array[in_port].in_port_id);
	RTE_LOG(DEBUG, SPP_NFV, "STATUS: in port %d patch out port id %d\n",
		in_port, ports_fwd_array[in_port].out_port_id);
	RTE_LOG(DEBUG, SPP_NFV, "STATUS: outport %d in_port_id %d\n", out_port,
		ports_fwd_array[out_port].in_port_id);

	return 0;
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
			RTE_LOG(ERR, SPP_NFV, "Failed to open %s\n", template);
			return -1;
		}
	}

	sprintf(cmd_str, "text2pcap %s %s", template, rx_fpath);
	res = system(cmd_str);
	if (res != 0) {
		RTE_LOG(ERR, SPP_NFV,
				"Failed to create pcap device %s\n",
				rx_fpath);
		return -1;
	}
	RTE_LOG(INFO, SPP_NFV, "PCAP device created\n");
	fclose(tmp_fp);
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
		RTE_LOG(ERR, SPP_NFV,
			"Failed to get RX ring %s - is primary running?\n",
			rx_queue_name);
		return -1;
	}
	RTE_LOG(INFO, SPP_NFV, "Looked up ring '%s'\n", rx_queue_name);

	/* create ring pmd*/
	res = rte_eth_from_ring(ring);
	if (res < 0) {
		RTE_LOG(ERR, SPP_NFV,
			"Cannot create eth dev with rte_eth_from_ring()\n");
		return -1;
	}
	RTE_LOG(INFO, SPP_NFV, "Created ring PMD: %d\n", res);

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

	RTE_LOG(DEBUG, SPP_NFV, "vhost port id %d\n", vhost_port_id);

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

	RTE_LOG(DEBUG, SPP_NFV, "pcap port id %d\n", pcap_pmd_port_id);

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

	RTE_LOG(DEBUG, SPP_NFV, "null port id %d\n", null_pmd_port_id);

	return null_pmd_port_id;
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
			RTE_LOG(INFO, SPP_NFV, "Port ID %d\n", i);
			RTE_LOG(INFO, SPP_NFV, "out_port_id %d\n",
				ports_fwd_array[i].out_port_id);
		}
	}
}

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

#endif // _NFV_COMMAND_UTILS_H_
