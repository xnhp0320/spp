/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2018 Nippon Telegraph and Telephone Corporation
 */

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>
#include <rte_net_crc.h>

#include "spp_port.h"
#include "ringlatencystats.h"

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

/* Information for VLAN tag management. */
struct port_ability_port_mng_info g_port_mng_info[RTE_MAX_ETHPORTS];

/* TPID of VLAN. */
static uint16_t g_vlan_tpid;

/* Initialize port ability. */
void
spp_port_ability_init(void)
{
	int cnt = 0;
	g_vlan_tpid = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);
	memset(g_port_mng_info, 0x00, sizeof(g_port_mng_info));
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		g_port_mng_info[cnt].rx.ref_index = 0;
		g_port_mng_info[cnt].rx.upd_index = 1;
		g_port_mng_info[cnt].tx.ref_index = 0;
		g_port_mng_info[cnt].tx.upd_index = 1;
	}
}

/* Get information of port ability. */
void
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

/* Calculation and Setting of FCS. */
static inline void
set_fcs_packet(struct rte_mbuf *pkt)
{
	uint32_t *fcs = NULL;
	fcs = rte_pktmbuf_mtod_offset(pkt, uint32_t *, pkt->data_len);
	*fcs = rte_net_crc_calc(rte_pktmbuf_mtod(pkt, void *),
			pkt->data_len, RTE_NET_CRC32_ETH);
}

/* Add VLAN tag to packet. */
static inline int
add_vlantag_packet(
		struct rte_mbuf *pkt,
		const union spp_ability_data *data)
{
	struct rte_ether_hdr *old_ether = NULL;
	struct rte_ether_hdr *new_ether = NULL;
	struct rte_vlan_hdr  *vlan      = NULL;
	const struct spp_vlantag_info *vlantag = &data->vlantag;

	old_ether = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
	if (old_ether->ether_type == g_vlan_tpid) {
		/* For packets with VLAN tags, only VLAN ID is updated */
		new_ether = old_ether;
		vlan = (struct rte_vlan_hdr *)&new_ether[1];
	} else {
		/* For packets without VLAN tag, add VLAN tag. */
		new_ether = (struct rte_ether_hdr *)rte_pktmbuf_prepend(pkt,
				sizeof(struct rte_vlan_hdr));
		if (unlikely(new_ether == NULL)) {
			RTE_LOG(ERR, PORT, "Failed to "
					"get additional header area.\n");
			return SPP_RET_NG;
		}

		rte_memcpy(new_ether, old_ether, sizeof(struct rte_ether_hdr));
		vlan = (struct rte_vlan_hdr *)&new_ether[1];
		vlan->eth_proto = new_ether->ether_type;
		new_ether->ether_type = g_vlan_tpid;
	}

	vlan->vlan_tci = vlantag->tci;
	set_fcs_packet(pkt);
	return SPP_RET_OK;
}

/* Add VLAN tag to all packets. */
static inline int
add_vlantag_all_packets(
		struct rte_mbuf **pkts, int nb_pkts,
		const union spp_ability_data *data)
{
	int ret = SPP_RET_OK;
	int cnt = 0;
	for (cnt = 0; cnt < nb_pkts; cnt++) {
		ret = add_vlantag_packet(pkts[cnt], data);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, PORT,
					"Failed to add VLAN tag."
					"(pkts %d/%d)\n", cnt, nb_pkts);
			break;
		}
	}
	return cnt;
}

/* Delete VLAN tag to packet. */
static inline int
del_vlantag_packet(
		struct rte_mbuf *pkt,
		const union spp_ability_data *data __attribute__ ((unused)))
{
	struct rte_ether_hdr *old_ether = NULL;
	struct rte_ether_hdr *new_ether = NULL;
	uint32_t *old, *new;

	old_ether = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
	if (old_ether->ether_type == g_vlan_tpid) {
		/* For packets without VLAN tag, delete VLAN tag. */
		new_ether = (struct rte_ether_hdr *)rte_pktmbuf_adj(pkt,
				sizeof(struct rte_vlan_hdr));
		if (unlikely(new_ether == NULL)) {
			RTE_LOG(ERR, PORT, "Failed to "
					"delete unnecessary header area.\n");
			return SPP_RET_NG;
		}

		old = (uint32_t *)old_ether;
		new = (uint32_t *)new_ether;
		new[2] = old[2];
		new[1] = old[1];
		new[0] = old[0];
		old[0] = 0;
		set_fcs_packet(pkt);
	}
	return SPP_RET_OK;
}

/* Delete VLAN tag to all packets. */
static inline int
del_vlantag_all_packets(
		struct rte_mbuf **pkts, int nb_pkts,
		const union spp_ability_data *data)
{
	int ret = SPP_RET_OK;
	int cnt = 0;
	for (cnt = 0; cnt < nb_pkts; cnt++) {
		ret = del_vlantag_packet(pkts[cnt], data);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, PORT,
					"Failed to del VLAN tag."
					"(pkts %d/%d)\n", cnt, nb_pkts);
			break;
		}
	}
	return cnt;
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
	struct spp_vlantag_info      *tag         = NULL;

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

		switch (out_ability[out_cnt].ope) {
		case SPP_PORT_ABILITY_OPE_ADD_VLANTAG:
			tag = &out_ability[out_cnt].data.vlantag;
			tag->tci = rte_cpu_to_be_16(SPP_VLANTAG_CALC_TCI(
					tag->vid, tag->pcp));
			break;
		case SPP_PORT_ABILITY_OPE_DEL_VLANTAG:
		default:
			/* Nothing to do. */
			break;
		}

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

/* Definition of functions that operate port abilities. */
typedef int (*port_ability_func)(
		struct rte_mbuf **pkts, int nb_pkts,
		const union spp_ability_data *data);

/* List of functions per port ability. */
port_ability_func port_ability_function_list[] = {
	NULL,                    /* None */
	add_vlantag_all_packets, /* Add VLAN tag */
	del_vlantag_all_packets, /* Del VLAN tag */
	NULL                     /* Termination */
};

/* Each packet operation of port capability. */
static inline int
port_ability_each_operation(uint16_t port_id,
		struct rte_mbuf **pkts, const uint16_t nb_pkts,
		enum spp_port_rxtx rxtx)
{
	int cnt, buf;
	int ok_pkts = nb_pkts;
	struct spp_port_ability *info = NULL;

	spp_port_ability_get_info(port_id, rxtx, &info);
	if (unlikely(info[0].ope == SPP_PORT_ABILITY_OPE_NONE))
		return nb_pkts;

	for (cnt = 0; cnt < SPP_PORT_ABILITY_MAX; cnt++) {
		if (info[cnt].ope == SPP_PORT_ABILITY_OPE_NONE)
			break;

		ok_pkts = port_ability_function_list[info[cnt].ope](
				pkts, ok_pkts, &info->data);
	}

	/* Discard remained packets to release mbuf. */
	if (unlikely(ok_pkts < nb_pkts)) {
		for (buf = ok_pkts; buf < nb_pkts; buf++)
			rte_pktmbuf_free(pkts[buf]);
	}

	return ok_pkts;
}

/* Wrapper function for rte_eth_rx_burst(). */
uint16_t
spp_eth_rx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **rx_pkts, const uint16_t nb_pkts)
{
	uint16_t nb_rx = 0;
	nb_rx = rte_eth_rx_burst(port_id, 0, rx_pkts, nb_pkts);
	if (unlikely(nb_rx == 0))
		return SPP_RET_OK;

#ifdef SPP_RINGLATENCYSTATS_ENABLE
	if (g_port_mng_info[port_id].iface_type == RING)
		spp_ringlatencystats_calculate_latency(
				g_port_mng_info[port_id].iface_no,
				rx_pkts, nb_pkts);
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	return port_ability_each_operation(port_id, rx_pkts, nb_rx,
			SPP_PORT_RXTX_RX);
}

/* Wrapper function for rte_eth_tx_burst(). */
uint16_t
spp_eth_tx_burst(
		uint16_t port_id, uint16_t queue_id  __attribute__ ((unused)),
		struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
	uint16_t nb_tx = 0;
	nb_tx = port_ability_each_operation(port_id, tx_pkts, nb_pkts,
			SPP_PORT_RXTX_TX);
	if (unlikely(nb_tx == 0))
		return SPP_RET_OK;

#ifdef SPP_RINGLATENCYSTATS_ENABLE
	if (g_port_mng_info[port_id].iface_type == RING)
		spp_ringlatencystats_add_time_stamp(
				g_port_mng_info[port_id].iface_no,
				tx_pkts, nb_pkts);
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	return rte_eth_tx_burst(port_id, 0, tx_pkts, nb_tx);
}
