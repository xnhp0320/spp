/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_eth_ring.h>
#include <rte_log.h>

#include "spp_proc.h"

#define RTE_LOGTYPE_SPP_PROC RTE_LOGTYPE_USER2

/* Manage data to addoress */
struct manage_data_addr_info {
	struct startup_param	  *p_startup_param;
	struct iface_info	  *p_iface_info;
	struct core_mng_info	  *p_core_info;
	int			  *p_capture_request;
	int			  *p_capture_status;
	unsigned int		  main_lcore_id;
};

/* Declare global variables */
/* Logical core ID for main process */
static struct manage_data_addr_info g_mng_data_addr;

/* generation of the ring port */
int
add_ring_pmd(int ring_id)
{
	struct rte_ring *ring;
	int ring_port_id;
	uint16_t port_id = PORT_RESET;
	char dev_name[RTE_ETH_NAME_MAX_LEN];

	/* Lookup ring of given id */
	ring = rte_ring_lookup(get_rx_queue_name(ring_id));
	if (unlikely(ring == NULL)) {
		RTE_LOG(ERR, SPP_PROC,
			"Cannot get RX ring - is server process running?\n");
		return SPP_RET_NG;
	}

	/* Create ring pmd */
	snprintf(dev_name, RTE_ETH_NAME_MAX_LEN - 1, "net_ring_%s", ring->name);
	/* check whether a port already exists. */
	ring_port_id = rte_eth_dev_get_port_by_name(dev_name, &port_id);
	if (port_id == PORT_RESET) {
		ring_port_id = rte_eth_from_ring(ring);
		if (ring_port_id < 0) {
			RTE_LOG(ERR, SPP_PROC, "Cannot create eth dev with "
						"rte_eth_from_ring()\n");
			return SPP_RET_NG;
		}
	} else {
		ring_port_id = port_id;
		rte_eth_dev_start(ring_port_id);
	}
	RTE_LOG(INFO, SPP_PROC, "ring port add. (no = %d / port = %d)\n",
			ring_id, ring_port_id);
	return ring_port_id;
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

	RTE_LOG(ERR, SPP_PROC,
			"Status check time out. (status = %d)\n", status);
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
	case RING:
		return &iface_info->ring[iface_no];
	default:
		return NULL;
	}
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
		p_iface_info->ring[port_cnt].iface_type = UNDEF;
		p_iface_info->ring[port_cnt].iface_no   = port_cnt;
		p_iface_info->ring[port_cnt].dpdk_port  = -1;
		p_iface_info->ring[port_cnt].class_id.vlantag.vid =
				ETH_VLAN_ID_MAX;
	}
}

/* Initialize g_core_info */
static void
init_core_info(void)
{
	struct core_mng_info *p_core_info = g_mng_data_addr.p_core_info;
	memset(p_core_info, 0x00,
			sizeof(struct core_mng_info)*RTE_MAX_LCORE);
	set_all_core_status(SPP_CORE_STOP);
	*g_mng_data_addr.p_capture_request = SPP_CAPTURE_IDLE;
	*g_mng_data_addr.p_capture_status = SPP_CAPTURE_IDLE;
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

/* Setup management info for spp_pcap */
int
init_mng_data(void)
{
	/* Initialize interface and core information */
	init_iface_info();
	init_core_info();

	int ret_nic = set_nic_interface();
	if (unlikely(ret_nic != SPP_RET_OK))
		return SPP_RET_NG;

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
	default:
		return SPP_RET_NG;
	}

	sprintf(port, "%s:%d", iface_type_str, iface_no);

	return SPP_RET_OK;
}

/* Set mange data address */
int spp_set_mng_data_addr(struct startup_param *startup_param_addr,
			  struct iface_info *iface_addr,
			  struct core_mng_info *core_mng_addr,
			  int *capture_request_addr,
			  int *capture_status_addr,
			  unsigned int main_lcore_id)
{
	if (startup_param_addr == NULL || iface_addr == NULL ||
			core_mng_addr == NULL ||
			capture_request_addr == NULL ||
			capture_status_addr == NULL ||
			main_lcore_id == 0xffffffff)
		return SPP_RET_NG;

	g_mng_data_addr.p_startup_param = startup_param_addr;
	g_mng_data_addr.p_iface_info = iface_addr;
	g_mng_data_addr.p_core_info = core_mng_addr;
	g_mng_data_addr.p_capture_request = capture_request_addr;
	g_mng_data_addr.p_capture_status = capture_status_addr;
	g_mng_data_addr.main_lcore_id = main_lcore_id;

	return SPP_RET_OK;
}

/* Get manage data address */
void spp_get_mng_data_addr(struct startup_param **startup_param_addr,
			   struct iface_info **iface_addr,
			   struct core_mng_info **core_mng_addr,
			   int **capture_request_addr,
			   int **capture_status_addr)
{

	if (startup_param_addr != NULL)
		*startup_param_addr = g_mng_data_addr.p_startup_param;
	if (iface_addr != NULL)
		*iface_addr = g_mng_data_addr.p_iface_info;
	if (core_mng_addr != NULL)
		*core_mng_addr = g_mng_data_addr.p_core_info;
	if (capture_request_addr != NULL)
		*capture_request_addr = g_mng_data_addr.p_capture_request;
	if (capture_status_addr != NULL)
		*capture_status_addr = g_mng_data_addr.p_capture_status;

}
