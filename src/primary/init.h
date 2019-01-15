/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _PRIMARY_INIT_H_
#define _PRIMARY_INIT_H_

#include <stdint.h>

#define MBUF_OVERHEAD (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_MBUF_DATA_SIZE 2048
#define MBUF_SIZE (RX_MBUF_DATA_SIZE + MBUF_OVERHEAD)

/*
 * Define a client structure with all needed info, including
 * stats from the clients.
 */
struct client {
	struct rte_ring *rx_q;
	unsigned int client_id;
	/*
	 * These stats hold how many packets the client will actually receive,
	 * and how many packets were dropped because the client's queue was full
	 * The port-info stats, in contrast, record how many packets were
	 * received or transmitted on an actual NIC port.
	 */
	struct {
		uint64_t rx;
		uint64_t rx_drop;
	} stats;
};

extern struct client *clients;

/* the shared port information: port numbers, rx and tx stats etc. */
extern struct port_info *ports;

int init(int argc, char *argv[]);

void check_all_ports_link_status(struct port_info *ports, uint16_t port_num,
		uint32_t port_mask);

int init_port(uint16_t port_num, struct rte_mempool *pktmbuf_pool);

#endif /* ifndef _PRIMARY_INIT_H_ */
