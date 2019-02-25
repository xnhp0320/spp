/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>
#include <rte_net_crc.h>

#include "spp_port.h"
#include "ringlatencystats.h"

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_PORT RTE_LOGTYPE_USER2

/* Wrapper function for rte_eth_rx_burst(). */
inline uint16_t
spp_eth_rx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **rx_pkts, const uint16_t nb_pkts)
{
	return rte_eth_rx_burst(port_id, 0, rx_pkts, nb_pkts);
}

/* Wrapper function for rte_eth_tx_burst(). */
inline uint16_t
spp_eth_tx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
	return rte_eth_tx_burst(port_id, 0, tx_pkts, nb_pkts);
}
