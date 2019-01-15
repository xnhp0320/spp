/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _NFV_PARAMS_H_
#define _NFV_PARAMS_H_

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

static struct port ports_fwd_array[RTE_MAX_ETHPORTS];

static uint16_t client_id;

/* the port details */
struct port_info *ports;

/*
 * our client id number - tells us which rx queue to read, and NIC TX
 * queue to write to.
 */
static char *server_ip;
static int server_port;

static enum cmd_type cmd;

static struct port_map port_map[RTE_MAX_ETHPORTS];

#endif // _NFV_PARAMS_H_
