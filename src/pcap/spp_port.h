/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef __SPP_PCAP_PORT_H__
#define __SPP_PCAP_PORT_H__

/**
 * @file
 * SPP Port ability
 *
 * Provide about the ability per port.
 */

#include "spp_proc.h"

/**
 * Wrapper function for rte_eth_rx_burst().
 *
 * @param port_id
 *  The port identifier of the Ethernet device.
 * @param queue_id
 *  The index of the receive queue from which to retrieve input packets.
 *  SPP is fixed at 0.
 * @param rx_pkts
 *  The address of an array of pointers to *rte_mbuf* structures that
 *  must be large enough to store *nb_pkts* pointers in it.
 * @param nb_pkts
 *  The maximum number of packets to retrieve.
 *
 * @return
 *  The number of packets actually retrieved, which is the number
 *  of pointers to *rte_mbuf* structures effectively supplied to the
 *  *rx_pkts* array.
 */
extern uint16_t spp_eth_rx_burst(uint16_t port_id, uint16_t queue_id,
		struct rte_mbuf **rx_pkts, const uint16_t nb_pkts);

/**
 * Wrapper function for rte_eth_tx_burst().
 *
 * @param port_id
 *  The port identifier of the Ethernet device.
 * @param queue_id
 *  The index of the transmit queue through which output packets must be sent.
 *  SPP is fixed at 0.
 * @param tx_pkts
 *  The address of an array of *nb_pkts* pointers to *rte_mbuf* structures
 *  which contain the output packets.
 * @param nb_pkts
 *  The maximum number of packets to transmit.
 *
 * @return
 *  The number of output packets actually stored in transmit descriptors of
 *  the transmit ring. The return value can be less than the value of the
 *  *tx_pkts* parameter when the transmit ring is full or has been filled up.
 */
extern uint16_t spp_eth_tx_burst(uint16_t port_id, uint16_t queue_id,
		struct rte_mbuf **tx_pkts, uint16_t nb_pkts);

#endif /*  __SPP_PCAP_PORT_H__ */
