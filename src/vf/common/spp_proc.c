/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_eth_ring.h>
#include <rte_eth_vhost.h>
#include <rte_cycles.h>
#include <rte_log.h>
#include <rte_branch_prediction.h>

#include "spp_proc.h"
#include "spp_port.h"

#include "shared/secondary/add_port.h"
#include "shared/secondary/utils.h"

#ifdef SPP_VF_MODULE
#include "../spp_forward.h"
#include "../classifier_mac.h"
#endif /* SPP_VF_MODULE */
#ifdef SPP_MIRROR_MODULE
#include "../../mirror/spp_mirror.h"
#endif /* SPP_MIRROR_MODULE */

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/* Manage data to addoress */
struct manage_data_addr_info {
	struct startup_param	  *p_startup_param;
	struct iface_info	  *p_iface_info;
	struct spp_component_info *p_component_info;
	struct core_mng_info	  *p_core_info;
	int			  *p_change_core;
	int			  *p_change_component;
	struct cancel_backup_info *p_backup_info;
	unsigned int		  main_lcore_id;
};

/* Declare global variables */
/* Logical core ID for main process */
static struct manage_data_addr_info g_mng_data_addr;

/**
 * Make a hexdump of an array data in every 4 byte.
 * This function is used to dump core_info or component info.
 */
void
dump_buff(const char *name, const void *addr, const size_t size)
{
	size_t cnt = 0;
	size_t max = (size / sizeof(unsigned int)) +
			((size % sizeof(unsigned int)) != 0);
	const uint32_t *buff = addr;

	if ((name != NULL) && (name[0] != '\0'))
		RTE_LOG(DEBUG, APP, "dump buff. (%s)\n", name);

	for (cnt = 0; cnt < max; cnt += 16) {
		RTE_LOG(DEBUG, APP, "[%p]"
				" %08x %08x %08x %08x %08x %08x %08x %08x"
				" %08x %08x %08x %08x %08x %08x %08x %08x",
				&buff[cnt],
				buff[cnt+0], buff[cnt+1],
				buff[cnt+2], buff[cnt+3],
				buff[cnt+4], buff[cnt+5],
				buff[cnt+6], buff[cnt+7],
				buff[cnt+8], buff[cnt+9],
				buff[cnt+10], buff[cnt+11],
				buff[cnt+12], buff[cnt+13],
				buff[cnt+14], buff[cnt+15]);
	}
}

/* generation of the ring port */
int
spp_vf_add_ring_pmd(int ring_id)
{
	struct rte_ring *ring;
	int ring_port_id;

	/* Lookup ring of given id */
	ring = rte_ring_lookup(get_rx_queue_name(ring_id));
	if (unlikely(ring == NULL)) {
		RTE_LOG(ERR, APP,
			"Cannot get RX ring - is server process running?\n");
		return SPP_RET_NG;
	}

	/* Create ring pmd */
	ring_port_id = rte_eth_from_ring(ring);
	RTE_LOG(INFO, APP, "ring port add. (no = %d / port = %d)\n",
			ring_id, ring_port_id);
	return ring_port_id;
}

/* generation of the vhost port */
int
spp_vf_add_vhost_pmd(int index, int client)
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
	if (unlikely(mp == NULL)) {
		RTE_LOG(ERR, APP, "Cannot get mempool for mbufs. "
				"(name = %s)\n", PKTMBUF_POOL_NAME);
		return SPP_RET_NG;
	}

	/* eth_vhost0 index 0 iface /tmp/sock0 on numa 0 */
	name = get_vhost_backend_name(index);
	iface = get_vhost_iface_name(index);

	sprintf(devargs, "%s,iface=%s,queues=%d,client=%d",
			name, iface, nr_queues, client);
	ret = dev_attach_by_devargs(devargs, &vhost_port_id);
	if (unlikely(ret < 0)) {
		RTE_LOG(ERR, APP, "spp_rte_eth_dev_attach error. "
				"(ret = %d)\n", ret);
		return ret;
	}

	ret = rte_eth_dev_configure(vhost_port_id, nr_queues, nr_queues,
		&port_conf);
	if (unlikely(ret < 0)) {
		RTE_LOG(ERR, APP, "rte_eth_dev_configure error. "
				"(ret = %d)\n", ret);
		return ret;
	}

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_rx_queue_setup(vhost_port_id, q, NR_DESCS,
			rte_eth_dev_socket_id(vhost_port_id), NULL, mp);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, APP,
				"rte_eth_rx_queue_setup error. (ret = %d)\n",
				ret);
			return ret;
		}
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < nr_queues; q++) {
		ret = rte_eth_tx_queue_setup(vhost_port_id, q, NR_DESCS,
			rte_eth_dev_socket_id(vhost_port_id), NULL);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, APP,
				"rte_eth_tx_queue_setup error. (ret = %d)\n",
				ret);
			return ret;
		}
	}

	/* Start the Ethernet port. */
	ret = rte_eth_dev_start(vhost_port_id);
	if (unlikely(ret < 0)) {
		RTE_LOG(ERR, APP, "rte_eth_dev_start error. (ret = %d)\n",
			ret);
		return ret;
	}

	RTE_LOG(INFO, APP, "vhost port add. (no = %d / port = %d)\n",
			index, vhost_port_id);
	return vhost_port_id;
}

/* Get core status */
enum spp_core_status
spp_get_core_status(unsigned int lcore_id)
{
	return (g_mng_data_addr.p_core_info + lcore_id)->status;
}

/**
 * Check status of all of cores is same as given
 *
 * It returns SPP_RET_NG as status mismatch if status is not same.
 * If core is in use, status will be checked.
 */
static int
check_core_status(enum spp_core_status status)
{
	unsigned int lcore_id = 0;
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if ((g_mng_data_addr.p_core_info + lcore_id)->status !=
								status) {
			/* Status is mismatched */
			return SPP_RET_NG;
		}
	}
	return SPP_RET_OK;
}

int
check_core_status_wait(enum spp_core_status status)
{
	int cnt = 0;
	for (cnt = 0; cnt < SPP_CORE_STATUS_CHECK_MAX; cnt++) {
		sleep(1);
		int ret = check_core_status(status);
		if (ret == 0)
			return SPP_RET_OK;
	}

	RTE_LOG(ERR, APP, "Status check time out. (status = %d)\n", status);
	return SPP_RET_NG;
}

/* Set core status */
void
set_core_status(unsigned int lcore_id,
		enum spp_core_status status)
{
	(g_mng_data_addr.p_core_info + lcore_id)->status = status;
}

/* Set all core to given status */
void
set_all_core_status(enum spp_core_status status)
{
	unsigned int lcore_id = 0;
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		(g_mng_data_addr.p_core_info + lcore_id)->status = status;
	}
}

/**
 * Set all of component status to SPP_CORE_STOP_REQUEST if received signal
 * is SIGTERM or SIGINT
 */
void
stop_process(int signal)
{
	if (unlikely(signal != SIGTERM) &&
			unlikely(signal != SIGINT)) {
		return;
	}

	(g_mng_data_addr.p_core_info + g_mng_data_addr.main_lcore_id)->status =
							SPP_CORE_STOP_REQUEST;
	set_all_core_status(SPP_CORE_STOP_REQUEST);
}

/**
 * Return port info of given type and num of interface
 *
 * It returns NULL value if given type is invalid.
 */
struct spp_port_info *
get_iface_info(enum port_type iface_type, int iface_no)
{
	struct iface_info *iface_info = g_mng_data_addr.p_iface_info;

	switch (iface_type) {
	case PHY:
		return &iface_info->nic[iface_no];
	case VHOST:
		return &iface_info->vhost[iface_no];
	case RING:
		return &iface_info->ring[iface_no];
	default:
		return NULL;
	}
}

/* Dump of core information */
void
dump_core_info(const struct core_mng_info *core_info)
{
	char str[SPP_NAME_STR_LEN];
	const struct core_mng_info *info = NULL;
	unsigned int lcore_id = 0;
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		info = &core_info[lcore_id];
		RTE_LOG(DEBUG, APP, "core[%d] status=%d, ref=%d, upd=%d\n",
				lcore_id, info->status,
				info->ref_index, info->upd_index);

		sprintf(str, "core[%d]-0 type=%d, num=%d", lcore_id,
				info->core[0].type, info->core[0].num);
		dump_buff(str, info->core[0].id,
				sizeof(int)*info->core[0].num);

		sprintf(str, "core[%d]-1 type=%d, num=%d", lcore_id,
				info->core[1].type, info->core[1].num);
		dump_buff(str, info->core[1].id,
				sizeof(int)*info->core[1].num);
	}
}

/* Dump of component information */
void
dump_component_info(const struct spp_component_info *component_info)
{
	char str[SPP_NAME_STR_LEN];
	const struct spp_component_info *component = NULL;
	int cnt = 0;
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		component = &component_info[cnt];
		if (component->type == SPP_COMPONENT_UNUSE)
			continue;

		RTE_LOG(DEBUG, APP, "component[%d] name=%s, type=%d, "
				"core=%u, index=%d\n",
				cnt, component->name, component->type,
				component->lcore_id, component->component_id);

		sprintf(str, "component[%d] rx=%d", cnt,
				component->num_rx_port);
		dump_buff(str, component->rx_ports,
			sizeof(struct spp_port_info *)*component->num_rx_port);

		sprintf(str, "component[%d] tx=%d", cnt,
				component->num_tx_port);
		dump_buff(str, component->tx_ports,
			sizeof(struct spp_port_info *)*component->num_tx_port);
	}
}

/* Dump of interface information */
void
dump_interface_info(const struct iface_info *iface_info)
{
	const struct spp_port_info *port = NULL;
	int cnt = 0;
	RTE_LOG(DEBUG, APP, "interface phy=%d, vhost=%d, ring=%d\n",
			iface_info->num_nic,
			iface_info->num_vhost,
			iface_info->num_ring);
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		port = &iface_info->nic[cnt];
		if (port->iface_type == UNDEF)
			continue;

		RTE_LOG(DEBUG, APP, "phy  [%d] type=%d, no=%d, port=%d, "
				"vid = %u, mac=%08lx(%s)\n",
				cnt, port->iface_type, port->iface_no,
				port->dpdk_port,
				port->class_id.vlantag.vid,
				port->class_id.mac_addr,
				port->class_id.mac_addr_str);
	}
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		port = &iface_info->vhost[cnt];
		if (port->iface_type == UNDEF)
			continue;

		RTE_LOG(DEBUG, APP, "vhost[%d] type=%d, no=%d, port=%d, "
				"vid = %u, mac=%08lx(%s)\n",
				cnt, port->iface_type, port->iface_no,
				port->dpdk_port,
				port->class_id.vlantag.vid,
				port->class_id.mac_addr,
				port->class_id.mac_addr_str);
	}
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		port = &iface_info->ring[cnt];
		if (port->iface_type == UNDEF)
			continue;

		RTE_LOG(DEBUG, APP, "ring [%d] type=%d, no=%d, port=%d, "
				"vid = %u, mac=%08lx(%s)\n",
				cnt, port->iface_type, port->iface_no,
				port->dpdk_port,
				port->class_id.vlantag.vid,
				port->class_id.mac_addr,
				port->class_id.mac_addr_str);
	}
}

/* Dump of all management information */
void
dump_all_mng_info(
		const struct core_mng_info *core,
		const struct spp_component_info *component,
		const struct iface_info *interface)
{
	if (rte_log_get_global_level() < RTE_LOG_DEBUG)
		return;

	dump_core_info(core);
	dump_component_info(component);
	dump_interface_info(interface);
}

/* Copy management information */
void
copy_mng_info(
		struct core_mng_info *dst_core,
		struct spp_component_info *dst_component,
		struct iface_info *dst_interface,
		const struct core_mng_info *src_core,
		const struct spp_component_info *src_component,
		const struct iface_info *src_interface,
		enum copy_mng_flg flg)
{
	int upd_index = 0;
	unsigned int lcore_id = 0;

	switch (flg) {
	case COPY_MNG_FLG_UPDCOPY:
		RTE_LCORE_FOREACH_SLAVE(lcore_id) {
			upd_index = src_core[lcore_id].upd_index;
			memcpy(&dst_core[lcore_id].core[upd_index],
					&src_core[lcore_id].core[upd_index],
					sizeof(struct core_info));
		}
		break;
	default:
		/*
		 * Even if the flag is set to None,
		 * copying of core is necessary,
		 * so we will treat all copies as default.
		 */
		memcpy(dst_core, src_core,
				sizeof(struct core_mng_info)*RTE_MAX_LCORE);
		break;
	}

	memcpy(dst_component, src_component,
			sizeof(struct spp_component_info)*RTE_MAX_LCORE);
	memcpy(dst_interface, src_interface,
			sizeof(struct iface_info));
}

/* Backup the management information */
void
backup_mng_info(struct cancel_backup_info *backup)
{
	dump_all_mng_info(g_mng_data_addr.p_core_info,
			g_mng_data_addr.p_component_info,
			g_mng_data_addr.p_iface_info);
	copy_mng_info(backup->core, backup->component, &backup->interface,
			g_mng_data_addr.p_core_info,
			g_mng_data_addr.p_component_info,
			g_mng_data_addr.p_iface_info,
			COPY_MNG_FLG_ALLCOPY);
	dump_all_mng_info(backup->core, backup->component, &backup->interface);
	memset(g_mng_data_addr.p_change_core, 0x00,
				sizeof(int)*RTE_MAX_LCORE);
	memset(g_mng_data_addr.p_change_component, 0x00,
				sizeof(int)*RTE_MAX_LCORE);
}

/**
 * Initialize g_iface_info
 *
 * Clear g_iface_info and set initial value.
 */
static void
init_iface_info(void)
{
	int port_cnt;  /* increment ether ports */
	struct iface_info *p_iface_info = g_mng_data_addr.p_iface_info;
	memset(p_iface_info, 0x00, sizeof(struct iface_info));
	for (port_cnt = 0; port_cnt < RTE_MAX_ETHPORTS; port_cnt++) {
		p_iface_info->nic[port_cnt].iface_type = UNDEF;
		p_iface_info->nic[port_cnt].iface_no   = port_cnt;
		p_iface_info->nic[port_cnt].dpdk_port  = -1;
		p_iface_info->nic[port_cnt].class_id.vlantag.vid =
				ETH_VLAN_ID_MAX;
		p_iface_info->vhost[port_cnt].iface_type = UNDEF;
		p_iface_info->vhost[port_cnt].iface_no   = port_cnt;
		p_iface_info->vhost[port_cnt].dpdk_port  = -1;
		p_iface_info->vhost[port_cnt].class_id.vlantag.vid =
				ETH_VLAN_ID_MAX;
		p_iface_info->ring[port_cnt].iface_type = UNDEF;
		p_iface_info->ring[port_cnt].iface_no   = port_cnt;
		p_iface_info->ring[port_cnt].dpdk_port  = -1;
		p_iface_info->ring[port_cnt].class_id.vlantag.vid =
				ETH_VLAN_ID_MAX;
	}
}

/* Initialize g_component_info */
static void
init_component_info(void)
{
	int cnt;
	memset(g_mng_data_addr.p_component_info, 0x00,
			sizeof(struct spp_component_info)*RTE_MAX_LCORE);
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++)
		(g_mng_data_addr.p_component_info + cnt)->component_id = cnt;
	memset(g_mng_data_addr.p_change_component, 0x00,
			sizeof(int)*RTE_MAX_LCORE);
}

/* Initialize g_core_info */
static void
init_core_info(void)
{
	int cnt = 0;
	struct core_mng_info *p_core_info = g_mng_data_addr.p_core_info;
	memset(p_core_info, 0x00,
			sizeof(struct core_mng_info)*RTE_MAX_LCORE);
	set_all_core_status(SPP_CORE_STOP);
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		(p_core_info + cnt)->ref_index = 0;
		(p_core_info + cnt)->upd_index = 1;
	}
	memset(g_mng_data_addr.p_change_core, 0x00, sizeof(int)*RTE_MAX_LCORE);
}

/* Setup port info of port on host */
static int
set_nic_interface(void)
{
	int nic_cnt = 0;
	struct iface_info *p_iface_info = g_mng_data_addr.p_iface_info;

	/* NIC Setting */
	p_iface_info->num_nic = rte_eth_dev_count_avail();
	if (p_iface_info->num_nic > RTE_MAX_ETHPORTS)
		p_iface_info->num_nic = RTE_MAX_ETHPORTS;

	for (nic_cnt = 0; nic_cnt < p_iface_info->num_nic; nic_cnt++) {
		p_iface_info->nic[nic_cnt].iface_type   = PHY;
		p_iface_info->nic[nic_cnt].dpdk_port = nic_cnt;
	}

	return SPP_RET_OK;
}

/* Setup management info for spp_vf */
int
init_mng_data(void)
{
	/* Initialize interface and core information */
	init_iface_info();
	init_core_info();
	init_component_info();

	int ret_nic = set_nic_interface();
	if (unlikely(ret_nic != SPP_RET_OK))
		return SPP_RET_NG;

	return SPP_RET_OK;
}

#ifdef SPP_RINGLATENCYSTATS_ENABLE
/* Print statistics of time for packet processing in ring interface */
static void
print_ring_latency_stats(void)
{
	/* Clear screen and move cursor to top left */
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H', '\0' };
	const char clr[] = { 27, '[', '2', 'J', '\0' };
	printf("%s%s", clr, topLeft);

	int ring_cnt, stats_cnt;
	struct spp_ringlatencystats_ring_latency_stats stats[RTE_MAX_ETHPORTS];
	memset(&stats, 0x00, sizeof(stats));

	struct iface_info *p_iface_info = g_mng_data_addr.p_iface_info;
	printf("RING Latency\n");
	printf(" RING");
	for (ring_cnt = 0; ring_cnt < RTE_MAX_ETHPORTS; ring_cnt++) {
		if (p_iface_info->ring[ring_cnt].iface_type == UNDEF)
			continue;

		spp_ringlatencystats_get_stats(ring_cnt, &stats[ring_cnt]);
		printf(", %-18d", ring_cnt);
	}
	printf("\n");

	for (stats_cnt = 0; stats_cnt < SPP_RINGLATENCYSTATS_STATS_SLOT_COUNT;
			stats_cnt++) {
		printf("%3dns", stats_cnt);
		for (ring_cnt = 0; ring_cnt < RTE_MAX_ETHPORTS; ring_cnt++) {
			if (p_iface_info->ring[ring_cnt].iface_type == UNDEF)
				continue;

			printf(", 0x%-16lx", stats[ring_cnt].slot[stats_cnt]);
		}
		printf("\n");
	}
}
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

/* Remove sock file if spp is not running */
void
del_vhost_sockfile(struct spp_port_info *vhost)
{
	int cnt;

	/* Do not remove for if it is running in vhost-client mode. */
	if (g_mng_data_addr.p_startup_param->vhost_client != 0)
		return;

	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		if (likely(vhost[cnt].iface_type == UNDEF)) {
			/* Skip removing if it is not using vhost */
			continue;
		}

		remove(get_vhost_iface_name(cnt));
	}
}

/* Get component type of target core */
enum spp_component_type
spp_get_component_type(unsigned int lcore_id)
{
	struct core_mng_info *info = (g_mng_data_addr.p_core_info + lcore_id);
	return info->core[info->ref_index].type;
}

/* Get core ID of target component */
unsigned int
spp_get_component_core(int component_id)
{
	struct spp_component_info *info =
			(g_mng_data_addr.p_component_info + component_id);
	return info->lcore_id;
}

/* Get core information which is in use */
struct core_info *
get_core_info(unsigned int lcore_id)
{
	struct core_mng_info *info = (g_mng_data_addr.p_core_info + lcore_id);
	return &(info->core[info->ref_index]);
}

/* Check core index change */
int
spp_check_core_update(unsigned int lcore_id)
{
	struct core_mng_info *info = (g_mng_data_addr.p_core_info + lcore_id);
	if (info->ref_index == info->upd_index)
		return SPP_RET_OK;
	else
		return SPP_RET_NG;
}

/* Check if component is using port. */
int
spp_check_used_port(
		enum port_type iface_type,
		int iface_no,
		enum spp_port_rxtx rxtx)
{
	int cnt, port_cnt, max = 0;
	struct spp_component_info *component = NULL;
	struct spp_port_info **port_array = NULL;
	struct spp_port_info *port = get_iface_info(iface_type, iface_no);
	struct spp_component_info *component_info =
					g_mng_data_addr.p_component_info;

	if (port == NULL)
		return SPP_RET_NG;

	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		component = (component_info + cnt);
		if (component->type == SPP_COMPONENT_UNUSE)
			continue;

		if (rxtx == SPP_PORT_RXTX_RX) {
			max = component->num_rx_port;
			port_array = component->rx_ports;
		} else if (rxtx == SPP_PORT_RXTX_TX) {
			max = component->num_tx_port;
			port_array = component->tx_ports;
		}
		for (port_cnt = 0; port_cnt < max; port_cnt++) {
			if (unlikely(port_array[port_cnt] == port))
				return cnt;
		}
	}

	return SPP_RET_NG;
}

/* Set component update flag for given port */
void
set_component_change_port(struct spp_port_info *port, enum spp_port_rxtx rxtx)
{
	int ret = 0;
	if ((rxtx == SPP_PORT_RXTX_RX) || (rxtx == SPP_PORT_RXTX_ALL)) {
		ret = spp_check_used_port(port->iface_type, port->iface_no,
				SPP_PORT_RXTX_RX);
		if (ret >= 0)
			*(g_mng_data_addr.p_change_component + ret) = 1;
	}

	if ((rxtx == SPP_PORT_RXTX_TX) || (rxtx == SPP_PORT_RXTX_ALL)) {
		ret = spp_check_used_port(port->iface_type, port->iface_no,
				SPP_PORT_RXTX_TX);
		if (ret >= 0)
			*(g_mng_data_addr.p_change_component + ret) = 1;
	}
}

/* Get unused component id */
int
get_free_component(void)
{
	struct spp_component_info *component_info =
					g_mng_data_addr.p_component_info;

	int cnt = 0;
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if ((component_info + cnt)->type == SPP_COMPONENT_UNUSE)
			return cnt;
	}
	return SPP_RET_NG;
}

/* Get component id for specified component name */
int
spp_get_component_id(const char *name)
{
	struct spp_component_info *component_info =
					g_mng_data_addr.p_component_info;

	int cnt = 0;
	if (name[0] == '\0')
		return SPP_RET_NG;

	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if (strcmp(name, (component_info + cnt)->name) == 0)
			return cnt;
	}
	return SPP_RET_NG;
}

/* Delete component information */
int
del_component_info(int component_id, int component_num, int *componet_array)
{
	int cnt;
	int match = -1;
	int max = component_num;

	for (cnt = 0; cnt < max; cnt++) {
		if (component_id == componet_array[cnt])
			match = cnt;
	}

	if (match < 0)
		return SPP_RET_NG;

	/* Last element is excluded from movement. */
	max--;

	for (cnt = match; cnt < max; cnt++)
		componet_array[cnt] = componet_array[cnt+1];

	/* Last element is cleared. */
	componet_array[cnt] = 0;
	return 0;
}

/* get port element which matches the condition */
int
check_port_element(
		struct spp_port_info *info,
		int num,
		struct spp_port_info *array[])
{
	int cnt = 0;
	int match = SPP_RET_NG;
	for (cnt = 0; cnt < num; cnt++) {
		if (info == array[cnt])
			match = cnt;
	}
	return match;
}

/* search matched port_info from array and delete it */
int
get_del_port_element(
		struct spp_port_info *info,
		int num,
		struct spp_port_info *array[])
{
	int cnt = 0;
	int match = SPP_RET_NG;
	int max = num;

	match = check_port_element(info, num, array);
	if (match < 0)
		return SPP_RET_NG;

	/* Last element is excluded from movement. */
	max--;

	for (cnt = match; cnt < max; cnt++)
		array[cnt] = array[cnt+1];

	/* Last element is cleared. */
	array[cnt] = NULL;
	return SPP_RET_OK;
}

/* Flush initial setting of each interface. */
int
flush_port(void)
{
	int ret = 0;
	int cnt = 0;
	struct spp_port_info *port = NULL;
	struct iface_info *p_iface_info = g_mng_data_addr.p_iface_info;

	/* Initialize added vhost. */
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		port = &p_iface_info->vhost[cnt];
		if ((port->iface_type != UNDEF) && (port->dpdk_port < 0)) {
			ret = spp_vf_add_vhost_pmd(port->iface_no,
				g_mng_data_addr.p_startup_param->vhost_client);
			if (ret < 0)
				return SPP_RET_NG;
			port->dpdk_port = ret;
		}
	}

	/* Initialize added ring. */
	for (cnt = 0; cnt < RTE_MAX_ETHPORTS; cnt++) {
		port = &p_iface_info->ring[cnt];
		if ((port->iface_type != UNDEF) && (port->dpdk_port < 0)) {
			ret = add_ring_pmd(port->iface_no);
			if (ret < 0)
				return SPP_RET_NG;
			port->dpdk_port = ret;
		}
	}
	return SPP_RET_OK;
}

/* Flush changed core. */
void
flush_core(void)
{
	int cnt = 0;
	struct core_mng_info *info = NULL;
	struct core_mng_info *p_core_info = g_mng_data_addr.p_core_info;
	int *p_change_core = g_mng_data_addr.p_change_core;

	/* Changed core has changed index. */
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if (*(p_change_core + cnt) != 0) {
			info = (p_core_info + cnt);
			info->upd_index = info->ref_index;
		}
	}

	/* Waiting for changed core change. */
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if (*(p_change_core + cnt) != 0) {
			info = (p_core_info + cnt);
			while (likely(info->ref_index == info->upd_index))
				rte_delay_us_block(SPP_CHANGE_UPDATE_INTERVAL);

			memcpy(&info->core[info->upd_index],
					&info->core[info->ref_index],
					sizeof(struct core_info));
		}
	}
}

/* Flush change for forwarder or classifier_mac */
int
flush_component(void)
{
	int ret = 0;
	int cnt = 0;
	struct spp_component_info *component_info = NULL;
	int *p_change_component = g_mng_data_addr.p_change_component;
	struct spp_component_info *p_component_info =
					g_mng_data_addr.p_component_info;

	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if (*(p_change_component + cnt) == 0)
			continue;

		component_info = (p_component_info + cnt);
		spp_port_ability_update(component_info);

#ifdef SPP_VF_MODULE
		if (component_info->type == SPP_COMPONENT_CLASSIFIER_MAC)
			ret = spp_classifier_mac_update(component_info);
		else
			ret = spp_forward_update(component_info);
#endif /* SPP_VF_MODULE */
#ifdef SPP_MIRROR_MODULE
		ret = spp_mirror_update(component_info);
#endif /* SPP_MIRROR_MODULE */
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, APP, "Flush error. "
					"( component = %s, type = %d)\n",
					component_info->name,
					component_info->type);
			return SPP_RET_NG;
		}
	}
	return SPP_RET_OK;
}

/**
 * Generate a formatted string of combination from interface type and
 * number and assign to given 'port'
 */
int spp_format_port_string(char *port, enum port_type iface_type, int iface_no)
{
	const char *iface_type_str;

	switch (iface_type) {
	case PHY:
		iface_type_str = SPP_IFTYPE_NIC_STR;
		break;
	case RING:
		iface_type_str = SPP_IFTYPE_RING_STR;
		break;
	case VHOST:
		iface_type_str = SPP_IFTYPE_VHOST_STR;
		break;
	default:
		return SPP_RET_NG;
	}

	sprintf(port, "%s:%d", iface_type_str, iface_no);

	return SPP_RET_OK;
}

/* Change mac address of 'aa:bb:cc:dd:ee:ff' to int64 and return it */
int64_t
spp_change_mac_str_to_int64(const char *mac)
{
	int64_t ret_mac = 0;
	int64_t token_val = 0;
	int token_cnt = 0;
	char tmp_mac[SPP_MIN_STR_LEN];
	char *str = tmp_mac;
	char *saveptr = NULL;
	char *endptr = NULL;

	RTE_LOG(DEBUG, APP, "MAC address change. (mac = %s)\n", mac);

	strcpy(tmp_mac, mac);
	while (1) {
		/* Split by colon(':') */
		char *ret_tok = strtok_r(str, ":", &saveptr);
		if (unlikely(ret_tok == NULL))
			break;

		/* Check for mal-formatted address */
		if (unlikely(token_cnt >= ETHER_ADDR_LEN)) {
			RTE_LOG(ERR, APP, "MAC address format error. "
					"(mac = %s)\n", mac);
			return SPP_RET_NG;
		}

		/* Convert string to hex value */
		int ret_tol = strtol(ret_tok, &endptr, 16);
		if (unlikely(ret_tok == endptr) || unlikely(*endptr != '\0'))
			break;

		/* Append separated value to the result */
		token_val = (int64_t)ret_tol;
		ret_mac |= token_val << (token_cnt * 8);
		token_cnt++;
		str = NULL;
	}

	RTE_LOG(DEBUG, APP, "MAC address change. (mac = %s => 0x%08lx)\n",
			 mac, ret_mac);
	return ret_mac;
}

/* Set mange data address */
int spp_set_mng_data_addr(struct startup_param *startup_param_addr,
			  struct iface_info *iface_addr,
			  struct spp_component_info *component_addr,
			  struct core_mng_info *core_mng_addr,
			  int *change_core_addr,
			  int *change_component_addr,
			  struct cancel_backup_info *backup_info_addr,
			  unsigned int main_lcore_id)
{
	if (startup_param_addr == NULL || iface_addr == NULL ||
			component_addr == NULL || core_mng_addr == NULL ||
			change_core_addr == NULL ||
			change_component_addr == NULL ||
			backup_info_addr == NULL ||
			main_lcore_id == 0xffffffff)
		return SPP_RET_NG;

	g_mng_data_addr.p_startup_param = startup_param_addr;
	g_mng_data_addr.p_iface_info = iface_addr;
	g_mng_data_addr.p_component_info = component_addr;
	g_mng_data_addr.p_core_info = core_mng_addr;
	g_mng_data_addr.p_change_core = change_core_addr;
	g_mng_data_addr.p_change_component = change_component_addr;
	g_mng_data_addr.p_backup_info = backup_info_addr;
	g_mng_data_addr.main_lcore_id = main_lcore_id;

	return SPP_RET_OK;
}

/* Get manage data address */
void spp_get_mng_data_addr(struct startup_param **startup_param_addr,
			   struct iface_info **iface_addr,
			   struct spp_component_info **component_addr,
			   struct core_mng_info **core_mng_addr,
			   int **change_core_addr,
			   int **change_component_addr,
			   struct cancel_backup_info **backup_info_addr)
{

	if (startup_param_addr != NULL)
		*startup_param_addr = g_mng_data_addr.p_startup_param;
	if (iface_addr != NULL)
		*iface_addr = g_mng_data_addr.p_iface_info;
	if (component_addr != NULL)
		*component_addr = g_mng_data_addr.p_component_info;
	if (core_mng_addr != NULL)
		*core_mng_addr = g_mng_data_addr.p_core_info;
	if (change_core_addr != NULL)
		*change_core_addr = g_mng_data_addr.p_change_core;
	if (change_component_addr != NULL)
		*change_component_addr = g_mng_data_addr.p_change_component;
	if (backup_info_addr != NULL)
		*backup_info_addr = g_mng_data_addr.p_backup_info;

}
