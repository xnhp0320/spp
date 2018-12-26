/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018  Nippon Telegraph and Telephone Corporation.
 */

#ifndef NFV_COMMAND_UTILS_H
#define NFV_COMMAND_UTILS_H

#include "common.h"
#include "nfv.h"

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

#endif
