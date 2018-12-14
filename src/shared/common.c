/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 */

#include <rte_cycles.h>
#include "common.h"

/* Check the link status of all ports in up to 9s, and print them finally */
void
check_all_ports_link_status(struct port_info *ports, uint16_t port_num,
		uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t count, all_ports_up;
	uint16_t portid;
	struct rte_eth_link link;

	RTE_LOG(INFO, APP, "\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if ((port_mask & (1 << ports->id[portid])) == 0)
				continue;

			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(ports->id[portid], &link);

			/* clear all_ports_up flag if any link down */
			if (link.link_status == 0) {
				all_ports_up = 0;
				break;
			}
		}

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		} else {
			printf("done\n");
			break;
		}
	}

	/* all ports up or timed out */
	for (portid = 0; portid < port_num; portid++) {
		if ((port_mask & (1 << ports->id[portid])) == 0)
			continue;

		memset(&link, 0, sizeof(link));
		rte_eth_link_get_nowait(ports->id[portid], &link);

		/* print link status */
		if (link.link_status)
			RTE_LOG(INFO, APP,
				"Port %d Link Up - speed %u Mbps - %s\n",
				ports->id[portid], link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					"full-duplex\n" : "half-duplex\n");
		else
			RTE_LOG(INFO, APP,
				"Port %d Link Down\n", ports->id[portid]);
	}
}

/**
 * Initialise an individual port:
 * - configure number of rx and tx rings
 * - set up each rx ring, to pull from the main mbuf pool
 * - set up each tx ring
 * - start the port and report its status to stdout
 */
int
init_port(uint16_t port_num, struct rte_mempool *pktmbuf_pool)
{
	/* for port configuration all features are off by default */
	const struct rte_eth_conf port_conf = {
		.rxmode = {
			.mq_mode = ETH_MQ_RX_RSS,
		},
	};
	const uint16_t rx_rings = 1, tx_rings = 1;
	const uint16_t rx_ring_size = RTE_MP_RX_DESC_DEFAULT;
	const uint16_t tx_ring_size = RTE_MP_TX_DESC_DEFAULT;
	uint16_t q;
	int retval;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_conf local_port_conf = port_conf;
	struct rte_eth_txconf txq_conf;

	RTE_LOG(INFO, APP, "Port %u init ... ", port_num);
	fflush(stdout);

	rte_eth_dev_info_get(port_num, &dev_info);
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
		local_port_conf.txmode.offloads |=
			DEV_TX_OFFLOAD_MBUF_FAST_FREE;
	txq_conf = dev_info.default_txconf;
	txq_conf.offloads = local_port_conf.txmode.offloads;

	/*
	 * Standard DPDK port initialisation - config port, then set up
	 * rx and tx rings
	 */
	retval = rte_eth_dev_configure(port_num, rx_rings, tx_rings,
		&port_conf);
	if (retval != 0)
		return retval;

	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port_num, q, rx_ring_size,
			rte_eth_dev_socket_id(port_num), NULL, pktmbuf_pool);
		if (retval < 0)
			return retval;
	}

	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port_num, q, tx_ring_size,
			rte_eth_dev_socket_id(port_num), &txq_conf);
		if (retval < 0)
			return retval;
	}

	rte_eth_promiscuous_enable(port_num);

	retval = rte_eth_dev_start(port_num);
	if (retval < 0)
		return retval;

	RTE_LOG(INFO, APP, "Port %d Init done\n", port_num);

	return 0;
}

/**
 * The ports to be used by the application are passed in
 * the form of a bitmask. This function parses the bitmask
 * and places the port numbers to be used into the port[]
 * array variable
 */
int
parse_portmask(struct port_info *ports, uint16_t max_ports,
		const char *portmask)
{
	char *end = NULL;
	unsigned long pm;
	uint16_t count = 0;

	if (portmask == NULL || *portmask == '\0')
		return -1;

	/* convert parameter to a number and verify */
	pm = strtoul(portmask, &end, 16);
	if (end == NULL || *end != '\0' || pm == 0)
		return -1;

	/* loop through bits of the mask and mark ports */
	while (pm != 0) {
		if (pm & 0x01) { /* bit is set in mask, use port */
			if (count >= max_ports)
				RTE_LOG(WARNING, APP,
					"port %u not present - ignoring\n",
					count);
			else
				ports->id[ports->num_ports++] = count;
		}
		pm = (pm >> 1);
		count++;
	}

	return 0;
}

/**
 * Take the number of clients parameter passed to the app
 * and convert to a number to store in the num_clients variable
 */
int
parse_num_clients(uint16_t *num_clients, const char *clients)
{
	char *end = NULL;
	unsigned long temp;

	if (clients == NULL || *clients == '\0')
		return -1;

	temp = strtoul(clients, &end, 10);
	if (end == NULL || *end != '\0' || temp == 0)
		return -1;

	*num_clients = (uint16_t)temp;
	return 0;
}

int
parse_server(char **server_ip, int *server_port, char *server_addr)
{
	const char delim[2] = ":";
	char *token;

	if (server_addr == NULL || *server_addr == '\0')
		return -1;

	*server_ip = strtok(server_addr, delim);
	RTE_LOG(DEBUG, APP, "server ip %s\n", *server_ip);

	token = strtok(NULL, delim);
	RTE_LOG(DEBUG, APP, "token %s\n", token);
	if (token == NULL || *token == '\0')
		return -1;

	RTE_LOG(DEBUG, APP, "token %s\n", token);
	*server_port = atoi(token);
	return 0;
}

/**
 * Retieve port type and ID from resource UID. For example, resource UID
 * 'ring:0' is  parsed to retrieve port tyep 'ring' and ID '0'.
 */
int
parse_resource_uid(char *str, char **port_type, int *port_id)
{
	char *token;
	char delim[] = ":";
	char *endp;

	RTE_LOG(DEBUG, APP, "Parsing resource UID: '%s\n'", str);
	if (strstr(str, delim) == NULL) {
		RTE_LOG(ERR, APP, "Invalid resource UID: '%s'\n", str);
		return -1;
	}
	RTE_LOG(DEBUG, APP, "Delimiter %s is included\n", delim);

	*port_type = strtok(str, delim);

	token = strtok(NULL, delim);
	*port_id = strtol(token, &endp, 10);

	if (*endp) {
		RTE_LOG(ERR, APP, "Bad integer value: %s\n", str);
		return -1;
	}

	return 0;
}

int
spp_atoi(const char *str, int *val)
{
	char *end;

	*val = strtol(str, &end, 10);

	if (*end) {
		RTE_LOG(ERR, APP, "Bad integer value: %s\n", str);
		return -1;
	}

	return 0;
}

/*
 * Get status of spp_nfv or spp_vm as JSON format. It consists of running
 * status and patch info of ports.
 *
 * Here is an example of well-formatted JSON status to better understand.
 * Actual status has no spaces and new lines inserted as
 * '{"status":"running","ports":[{"src":"phy:0","dst":"ring:0"},...]}'
 *
 *   {
 *     "status": "running",
 *     "ports": ["phy:0", "phy:1", "ring:0", "vhost:0"],
 *     "patches": [
 *       {"src":"phy:0","dst": "ring:0"},
 *       {"src":"ring:0","dst": "vhost:0"}
 *     ]
 *   }
 */
void
get_sec_stats_json(char *str, uint16_t client_id,
		const char *running_stat,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	sprintf(str, "{\"client-id\":%d,", client_id);

	sprintf(str + strlen(str), "\"status\":");
	sprintf(str + strlen(str), "\"%s\",", running_stat);

	append_port_info_json(str, ports_fwd_array, port_map);
	sprintf(str + strlen(str), ",");

	append_patch_info_json(str, ports_fwd_array, port_map);
	sprintf(str + strlen(str), "}");

	// make sure to be terminated with null character
	sprintf(str + strlen(str), "%c", '\0');
}

/*
 * Append patch info to sec status. It is called from get_sec_stats_json()
 * to add a JSON formatted patch info to given 'str'. Here is an example.
 *
 *     "ports": ["phy:0", "phy:1", "ring:0", "vhost:0"]
 */
int
append_port_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	unsigned int i;
	unsigned int has_port = 0;  // for checking having port at last

	sprintf(str + strlen(str), "\"ports\":[");
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {

		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		has_port = 1;
		switch (port_map[i].port_type) {
		case PHY:
			sprintf(str + strlen(str), "\"phy:%u\",",
					port_map[i].id);
			break;
		case RING:
			sprintf(str + strlen(str), "\"ring:%u\",",
				port_map[i].id);
			break;
		case VHOST:
			sprintf(str + strlen(str), "\"vhost:%u\",",
				port_map[i].id);
			break;
		case PCAP:
			sprintf(str + strlen(str), "\"pcap:%u\",",
					port_map[i].id);
			break;
		case NULLPMD:
			sprintf(str + strlen(str), "\"nullpmd:%u\",",
					port_map[i].id);
			break;
		case UNDEF:
			/* TODO(yasufum) Need to remove print for undefined ? */
			sprintf(str + strlen(str), "\"udf\",");
			break;
		}
	}

	// Check if it has at least one port to remove ",".
	if (has_port == 0) {
		sprintf(str + strlen(str), "]");
	} else {  // Remove last ','
		sprintf(str + strlen(str) - 1, "]");
	}

	return 0;
}

/*
 * Append patch info to sec status. It is called from get_sec_stats_json()
 * to add a JSON formatted patch info to given 'str'. Here is an example.
 *
 *     "patches": [
 *       {"src":"phy:0","dst": "ring:0"},
 *       {"src":"ring:0","dst": "vhost:0"}
 *      ]
 */
int
append_patch_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	unsigned int i;
	unsigned int has_patch = 0;  // for checking having patch at last

	char patch_str[128];
	sprintf(str + strlen(str), "\"patches\":[");
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {

		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		RTE_LOG(INFO, APP, "Port ID %d\n", i);
		RTE_LOG(INFO, APP, "Status %d\n",
			ports_fwd_array[i].in_port_id);

		memset(patch_str, '\0', sizeof(patch_str));

		sprintf(patch_str, "{\"src\":");

		switch (port_map[i].port_type) {
		case PHY:
			RTE_LOG(INFO, APP, "Type: PHY\n");
			sprintf(patch_str + strlen(patch_str),
					"\"phy:%u\",",
					port_map[i].id);
			break;
		case RING:
			RTE_LOG(INFO, APP, "Type: RING\n");
			sprintf(patch_str + strlen(patch_str),
					"\"ring:%u\",",
					port_map[i].id);
			break;
		case VHOST:
			RTE_LOG(INFO, APP, "Type: VHOST\n");
			sprintf(patch_str + strlen(patch_str),
					"\"vhost:%u\",",
					port_map[i].id);
			break;
		case PCAP:
			RTE_LOG(INFO, APP, "Type: PCAP\n");
			sprintf(patch_str + strlen(patch_str),
					"\"pcap:%u\",",
					port_map[i].id);
			break;
		case NULLPMD:
			RTE_LOG(INFO, APP, "Type: NULLPMD\n");
			sprintf(patch_str + strlen(patch_str),
					"\"nullpmd:%u\",",
					port_map[i].id);
			break;
		case UNDEF:
			RTE_LOG(INFO, APP, "Type: UDF\n");
			/* TODO(yasufum) Need to remove print for undefined ? */
			sprintf(patch_str + strlen(patch_str),
					"\"udf\",");
			break;
		}

		sprintf(patch_str + strlen(patch_str), "\"dst\":");

		RTE_LOG(INFO, APP, "Out Port ID %d\n",
				ports_fwd_array[i].out_port_id);

		if (ports_fwd_array[i].out_port_id == PORT_RESET) {
			//sprintf(patch_str + strlen(patch_str), "%s", "\"\"");
			continue;
		} else {
			has_patch = 1;
			unsigned int j = ports_fwd_array[i].out_port_id;
			switch (port_map[j].port_type) {
			case PHY:
				RTE_LOG(INFO, APP, "Type: PHY\n");
				sprintf(patch_str + strlen(patch_str),
						"\"phy:%u\"",
						port_map[j].id);
				break;
			case RING:
				RTE_LOG(INFO, APP, "Type: RING\n");
				sprintf(patch_str + strlen(patch_str),
						"\"ring:%u\"",
						port_map[j].id);
				break;
			case VHOST:
				RTE_LOG(INFO, APP, "Type: VHOST\n");
				sprintf(patch_str + strlen(patch_str),
						"\"vhost:%u\"",
						port_map[j].id);
				break;
			case PCAP:
				RTE_LOG(INFO, APP, "Type: PCAP\n");
				sprintf(patch_str + strlen(patch_str),
						"\"pcap:%u\"",
						port_map[j].id);
				break;
			case NULLPMD:
				RTE_LOG(INFO, APP, "Type: NULLPMD\n");
				sprintf(patch_str + strlen(patch_str),
						"\"nullpmd:%u\"",
						port_map[j].id);
				break;
			case UNDEF:
				RTE_LOG(INFO, APP, "Type: UDF\n");
				/*
				 * TODO(yasufum) Need to remove print for
				 * undefined ?
				 */
				sprintf(patch_str + strlen(patch_str),
						"\"udf\"");
				break;
			}
		}

		sprintf(patch_str + strlen(patch_str), "},");

		if (has_patch != 0)
			sprintf(str + strlen(str), "%s", patch_str);
	}


	// Check if it has at least one patch to remove ",".
	if (has_patch == 0) {
		sprintf(str + strlen(str), "]");
	} else {  // Remove last ','
		sprintf(str + strlen(str) - 1, "]");
	}

	return 0;
}

/* attach the new device, then store port_id of the device */
int
dev_attach_by_devargs(const char *devargs, uint16_t *port_id)
{
	int ret = -1;
	struct rte_devargs da;

	memset(&da, 0, sizeof(da));

	/* parse devargs */
	if (rte_devargs_parse(&da, devargs))
		return -1;

	RTE_LOG(INFO, APP, "devargs=%s\n", devargs);
	ret = rte_dev_probe(devargs);
	if (ret < 0) {
		free(da.args);
		return ret;
	}

	ret = rte_eth_dev_get_port_by_name(da.name, port_id);

	free(da.args);

	return ret;
}

/* detach the device, then store the name of the device */
int
dev_detach_by_port_id(uint16_t port_id)
{
	struct rte_device *dev;
	struct rte_bus *bus;
	uint32_t dev_flags;
	int ret = -1;

	if (rte_eth_devices[port_id].data == NULL) {
		RTE_LOG(INFO, APP,
			"rte_eth_devices[%d].data is  NULL\n", port_id);
		return 0;
	}
	dev_flags = rte_eth_devices[port_id].data->dev_flags;
	if (dev_flags & RTE_ETH_DEV_BONDED_SLAVE) {
		RTE_LOG(ERR, APP,
			"Port %"PRIu16" is bonded, cannot detach\n", port_id);
		return -ENOTSUP;
	}

	dev = rte_eth_devices[port_id].device;
	if (dev == NULL)
		return -EINVAL;

	bus = rte_bus_find_by_device(dev);
	if (bus == NULL)
		return -ENOENT;

	//ret = rte_eal_hotplug_remove(bus->name, dev->name);
	rte_eth_dev_close(port_id);
	ret = rte_dev_remove(dev);
	if (ret < 0)
		return ret;

	rte_eth_dev_release_port(&rte_eth_devices[port_id]);

	return 0;
}
