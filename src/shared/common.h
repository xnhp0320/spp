/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <limits.h>

#include <rte_branch_prediction.h>
#include <rte_common.h>
#include <rte_config.h>
#include <rte_eal.h>
#include <rte_devargs.h>
#include <rte_ethdev.h>
#include <rte_ethdev_driver.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#define MSG_SIZE 2048  /* socket buffer len */

#define SOCK_RESET  -1
#define PORT_RESET  UINT16_MAX

// Maximum number of rings.
#define MAX_CLIENT  99

// The number of tokens in a command line.
#define MAX_PARAMETER 10

#define NO_FLAGS 0

/*
 * When doing reads from the NIC or the client queues,
 * use this batch size
 */
//#define PACKET_READ_SIZE 32

/*
 * TODO(yasufum) move it from common.h used only for spp_nfv, spp_vf and
 * spp_mirror.
 */
#define MAX_PKT_BURST 32

// TODO(yasufum) move it from common.h used only for primary and spp_vm.
#define MBUFS_PER_CLIENT 1536
#define MBUFS_PER_PORT 1536
#define MBUF_CACHE_SIZE 512

#define MBUF_OVERHEAD (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_MBUF_DATA_SIZE 2048
#define MBUF_SIZE (RX_MBUF_DATA_SIZE + MBUF_OVERHEAD)

#define RTE_MP_RX_DESC_DEFAULT 512
#define RTE_MP_TX_DESC_DEFAULT 512

/* Command. */
enum cmd_type {
	STOP,
	START,
	FORWARD,
};

/*
 * Shared port info, including statistics information for display by server.
 * Structure will be put in a memzone.
 * - All port id values share one cache line as this data will be read-only
 * during operation.
 * - All rx statistic values share cache lines, as this data is written only
 * by the server process. (rare reads by stats display)
 * - The tx statistics have values for all ports per cache line, but the stats
 * themselves are written by the clients, so we have a distinct set, on
 * different cache lines for each client to use.
 */

struct stats {
	uint64_t rx;
	uint64_t rx_drop;
	uint64_t tx;
	uint64_t tx_drop;
} __rte_cache_aligned;

struct port_info {
	uint16_t num_ports;
	uint16_t id[RTE_MAX_ETHPORTS];
	struct stats port_stats[RTE_MAX_ETHPORTS];
	struct stats client_stats[MAX_CLIENT];
};

enum port_type {
	PHY,
	RING,
	VHOST,
	PCAP,
	NULLPMD,
	UNDEF,
};

struct port_map {
	int id;
	enum port_type port_type;
	struct stats *stats;
	struct stats default_stats;
};

struct port {
	uint16_t in_port_id;
	uint16_t out_port_id;
	uint16_t (*rx_func)(uint16_t, uint16_t, struct rte_mbuf **, uint16_t);
	uint16_t (*tx_func)(uint16_t, uint16_t, struct rte_mbuf **, uint16_t);
};

/* define common names for structures shared between server and client */
#define MP_CLIENT_RXQ_NAME "eth_ring%u"
#define PKTMBUF_POOL_NAME "MProc_pktmbuf_pool"
#define VM_PKTMBUF_POOL_NAME "VM_Proc_pktmbuf_pool"
#define VM_MZ_PORT_INFO "VM_Proc_port_info"
#define MZ_PORT_INFO "MProc_port_info"

#define VHOST_BACKEND_NAME "eth_vhost%u"
#define VHOST_IFACE_NAME "/tmp/sock%u"

#define PCAP_PMD_DEV_NAME "eth_pcap%u"
#define PCAP_IFACE_RX "/tmp/spp-rx%d.pcap"
#define PCAP_IFACE_TX "/tmp/spp-tx%d.pcap"

#define NULL_PMD_DEV_NAME "eth_null%u"

/*
 * Given the rx queue name template above, get the queue name
 */
static inline const char *
get_rx_queue_name(unsigned int id)
{
	/*
	 * buffer for return value. Size calculated by %u being replaced
	 * by maximum 3 digits (plus an extra byte for safety)
	 */
	static char buffer[sizeof(MP_CLIENT_RXQ_NAME) + 2];

	snprintf(buffer, sizeof(buffer) - 1, MP_CLIENT_RXQ_NAME, id);
	return buffer;
}

static inline const char *
get_vhost_backend_name(unsigned int id)
{
	/*
	 * buffer for return value. Size calculated by %u being replaced
	 * by maximum 3 digits (plus an extra byte for safety)
	 */
	static char buffer[sizeof(VHOST_BACKEND_NAME) + 2];

	snprintf(buffer, sizeof(buffer) - 1, VHOST_BACKEND_NAME, id);
	return buffer;
}

static inline char *
get_vhost_iface_name(unsigned int id)
{
	/*
	 * buffer for return value. Size calculated by %u being replaced
	 * by maximum 3 digits (plus an extra byte for safety)
	 */
	static char buffer[sizeof(VHOST_IFACE_NAME) + 2];

	snprintf(buffer, sizeof(buffer) - 1, VHOST_IFACE_NAME, id);
	return buffer;
}

static inline const char *
get_pcap_pmd_name(int id)
{
	static char buffer[sizeof(PCAP_PMD_DEV_NAME) + 2];
	snprintf(buffer, sizeof(buffer) - 1, PCAP_PMD_DEV_NAME, id);
	return buffer;
}

static inline const char *
get_null_pmd_name(int id)
{
	static char buffer[sizeof(NULL_PMD_DEV_NAME) + 2];
	snprintf(buffer, sizeof(buffer) - 1, NULL_PMD_DEV_NAME, id);
	return buffer;
}

/* Set log level of type RTE_LOGTYPE_USER* to given level. */
int set_user_log_level(int num_user_log, uint32_t log_level);

/* Set log level of type RTE_LOGTYPE_USER* to RTE_LOG_DEBUG. */
int set_user_log_debug(int num_user_log);

void check_all_ports_link_status(struct port_info *ports, uint16_t port_num,
		uint32_t port_mask);

int init_port(uint16_t port_num, struct rte_mempool *pktmbuf_pool);

int parse_portmask(struct port_info *ports, uint16_t max_ports,
		const char *portmask);
int parse_num_clients(uint16_t *num_clients, const char *clients);
int parse_server(char **server_ip, int *server_port, char *server_addr);

/* Get status of spp_nfv or spp_vm as JSON format. */
void get_sec_stats_json(char *str, uint16_t client_id,
		const char *running_stat,
		struct port *ports_fwd_array,
		struct port_map *port_map);

/* Append port info to sec status, called from get_sec_stats_json(). */
int append_port_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map);

/* Append patch info to sec status, called from get_sec_stats_json(). */
int append_patch_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map);

int parse_resource_uid(char *str, char **port_type, int *port_id);
int spp_atoi(const char *str, int *val);

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/**
 * Attach a new Ethernet device specified by arguments.
 *
 * @param devargs
 *  A pointer to a strings array describing the new device
 *  to be attached. The strings should be a pci address like
 *  '0000:01:00.0' or virtual device name like 'net_pcap0'.
 * @param port_id
 *  A pointer to a port identifier actually attached.
 * @return
 *  0 on success and port_id is filled, negative on error
 */
int
dev_attach_by_devargs(const char *devargs, uint16_t *port_id);

/**
 * Detach a Ethernet device specified by port identifier.
 * This function must be called when the device is in the
 * closed state.
 *
 * @param port_id
 *   The port identifier of the device to detach.
 * @return
 *  0 on success and devname is filled, negative on error
 */
int dev_detach_by_port_id(uint16_t port_id);

#endif
