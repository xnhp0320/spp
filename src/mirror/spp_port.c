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

/* Port ability management information */
struct port_ability_mng_info {
	volatile int ref_index; /* Index to reference area */
	volatile int upd_index; /* Index to update area    */
	struct spp_port_ability ability[SPP_INFO_AREA_MAX]
				[SPP_PORT_ABILITY_MAX];
				/* Port ability information */
};

/* Port ability port information */
struct port_ability_port_mng_info {
	/* Interface type (phy/vhost/ring) */
	enum port_type iface_type;

	/* Interface number */
	int            iface_no;

	/* Management data of port ability for receiving */
	struct port_ability_mng_info rx;

	/* Management data of port ability for sending */
	struct port_ability_mng_info tx;
};

/* Information for port management. */
struct port_ability_port_mng_info g_port_mng_info[RTE_MAX_ETHPORTS];

/* Initialize port ability. */
void
spp_port_ability_init(void)
{
	int cnt = 0;
	memset(g_port_mng_info, 0x00, sizeof(g_port_mng_info));
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		g_port_mng_info[cnt].rx.ref_index = 0;
		g_port_mng_info[cnt].rx.upd_index = 1;
		g_port_mng_info[cnt].tx.ref_index = 0;
		g_port_mng_info[cnt].tx.upd_index = 1;
	}
}

/* Get information of port ability. */
inline void
spp_port_ability_get_info(
		int port_id, enum spp_port_rxtx rxtx,
		struct spp_port_ability **info)
{
	struct port_ability_mng_info *mng = NULL;

	switch (rxtx) {
	case SPP_PORT_RXTX_RX:
		mng = &g_port_mng_info[port_id].rx;
		break;
	case SPP_PORT_RXTX_TX:
		mng = &g_port_mng_info[port_id].tx;
		break;
	default:
		/* Not used. */
		break;
	}
	*info = mng->ability[mng->ref_index];
}

/* Change index of management information. */
void
spp_port_ability_change_index(
		enum port_ability_chg_index_type type,
		int port_id, enum spp_port_rxtx rxtx)
{
	int cnt;
	static int num_rx;
	static int rx_list[RTE_MAX_ETHPORTS];
	static int num_tx;
	static int tx_list[RTE_MAX_ETHPORTS];
	struct port_ability_mng_info *mng = NULL;

	if (type == PORT_ABILITY_CHG_INDEX_UPD) {
		switch (rxtx) {
		case SPP_PORT_RXTX_RX:
			mng = &g_port_mng_info[port_id].rx;
			mng->upd_index = mng->ref_index;
			rx_list[num_rx++] = port_id;
			break;
		case SPP_PORT_RXTX_TX:
			mng = &g_port_mng_info[port_id].tx;
			mng->upd_index = mng->ref_index;
			tx_list[num_tx++] = port_id;
			break;
		default:
			/* Not used. */
			break;
		}
		return;
	}

	for (cnt = 0; cnt < num_rx; cnt++) {
		mng = &g_port_mng_info[rx_list[cnt]].rx;
		mng->ref_index = (mng->upd_index+1)%SPP_INFO_AREA_MAX;
		rx_list[cnt] = 0;
	}
	for (cnt = 0; cnt < num_tx; cnt++) {
		mng = &g_port_mng_info[tx_list[cnt]].tx;
		mng->ref_index = (mng->upd_index+1)%SPP_INFO_AREA_MAX;
		tx_list[cnt] = 0;
	}

	num_rx = 0;
	num_tx = 0;
}

/* Set ability data of port ability. */
static void
port_ability_set_ability(
		struct spp_port_info *port,
		enum spp_port_rxtx rxtx)
{
	int in_cnt, out_cnt = 0;
	int port_id = port->dpdk_port;
	struct port_ability_port_mng_info *port_mng =
						&g_port_mng_info[port_id];
	struct port_ability_mng_info *mng         = NULL;
	struct spp_port_ability      *in_ability  = port->ability;
	struct spp_port_ability      *out_ability = NULL;

	port_mng->iface_type = port->iface_type;
	port_mng->iface_no   = port->iface_no;

	switch (rxtx) {
	case SPP_PORT_RXTX_RX:
		mng = &port_mng->rx;
		break;
	case SPP_PORT_RXTX_TX:
		mng = &port_mng->tx;
		break;
	default:
		/* Not used. */
		break;
	}

	out_ability = mng->ability[mng->upd_index];
	memset(out_ability, 0x00, sizeof(struct spp_port_ability)
			* SPP_PORT_ABILITY_MAX);
	for (in_cnt = 0; in_cnt < SPP_PORT_ABILITY_MAX; in_cnt++) {
		if (in_ability[in_cnt].rxtx != rxtx)
			continue;

		memcpy(&out_ability[out_cnt], &in_ability[in_cnt],
				sizeof(struct spp_port_ability));

		out_cnt++;
	}

	spp_port_ability_change_index(PORT_ABILITY_CHG_INDEX_UPD,
			port_id, rxtx);
}

/* Update port capability. */
void
spp_port_ability_update(const struct spp_component_info *component)
{
	int cnt;
	struct spp_port_info *port = NULL;
	for (cnt = 0; cnt < component->num_rx_port; cnt++) {
		port = component->rx_ports[cnt];
		port_ability_set_ability(port, SPP_PORT_RXTX_RX);
	}

	for (cnt = 0; cnt < component->num_tx_port; cnt++) {
		port = component->tx_ports[cnt];
		port_ability_set_ability(port, SPP_PORT_RXTX_TX);
	}
}

/* Wrapper function for rte_eth_rx_burst(). */
inline uint16_t
spp_eth_rx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **rx_pkts, const uint16_t nb_pkts)
{
	uint16_t nb_rx = 0;
	nb_rx = rte_eth_rx_burst(port_id, 0, rx_pkts, nb_pkts);
	if (unlikely(nb_rx == 0))
		return 0;

#ifdef SPP_RINGLATENCYSTATS_ENABLE
	if (g_port_mng_info[port_id].iface_type == RING)
		spp_ringlatencystats_calculate_latency(
				g_port_mng_info[port_id].iface_no,
				rx_pkts, nb_rx);
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	return nb_rx;
}

/* Wrapper function for rte_eth_tx_burst(). */
inline uint16_t
spp_eth_tx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
#ifdef SPP_RINGLATENCYSTATS_ENABLE
	if (g_port_mng_info[port_id].iface_type == RING)
		spp_ringlatencystats_add_time_stamp(
				g_port_mng_info[port_id].iface_no,
				tx_pkts, nb_pkts);
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	return rte_eth_tx_burst(port_id, 0, tx_pkts, nb_pkts);
}
