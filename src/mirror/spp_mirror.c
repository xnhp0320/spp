/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <rte_common.h>
#include <rte_cycles.h>

#include "shared/common.h"
#include "shared/secondary/utils.h"

#include "spp_proc.h"
#include "spp_mirror.h"
#include "command_proc.h"
#include "spp_port.h"

/* Declare global variables */
#define RTE_LOGTYPE_MIRROR RTE_LOGTYPE_USER1

#define SPP_MIRROR_POOL_NAME "spp_mirror_pool"
#define SPP_MIRROR_POOL_NAME_MAX 32
#define MAX_PKT_MIRROR 4096
#define MEMPOOL_CACHE_SIZE 256
#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024

/* A set of port info of rx and tx */
struct mirror_rxtx {
	struct spp_port_info rx; /* rx port */
	struct spp_port_info tx; /* tx port */
};

/* Information on the path used for mirror. */
struct mirror_path {
	char name[SPP_NAME_STR_LEN];	/* component name	   */
	volatile enum spp_component_type type;
					/* component type	   */
	int num_rx;			/* number of receive ports */
	int num_tx;			/* number of mirror ports  */
	struct mirror_rxtx ports[RTE_MAX_ETHPORTS];
					/* port used for mirror	   */
};

/* Information for mirror. */
struct mirror_info {
	volatile int ref_index; /* index to reference area */
	volatile int upd_index; /* index to update area    */
	struct mirror_path path[SPP_INFO_AREA_MAX];
				/* Information of data path */
};

/*  */
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* Logical core ID for main process */
static unsigned int g_main_lcore_id = 0xffffffff;

/* Execution parameter of spp_mirror */
static struct startup_param g_startup_param;

/* Interface management information */
static struct iface_info g_iface_info;

/* Component management information */
static struct spp_component_info g_component_info[RTE_MAX_LCORE];

/* Core management information */
static struct core_mng_info g_core_info[RTE_MAX_LCORE];

/* Array of update indicator for core management information */
static int g_change_core[RTE_MAX_LCORE];

/* Array of update indicator for component management information */
static int g_change_component[RTE_MAX_LCORE];

/* Backup information for cancel command */
static struct cancel_backup_info g_backup_info;

/* mirror info */
static struct mirror_info g_mirror_info[RTE_MAX_LCORE];

/* mirror mbuf pool */
static struct rte_mempool *g_mirror_pool;

/* Print help message */
static void
usage(const char *progname)
{
	RTE_LOG(INFO, MIRROR, "Usage: %s [EAL args] --"
			" --client-id CLIENT_ID"
			" -s SERVER_IP:SERVER_PORT"
			" [--vhost-client]\n"
			" --client-id CLIENT_ID   : My client ID\n"
			" -s SERVER_IP:SERVER_PORT  : "
				"Access information to the server\n"
			" --vhost-client            : Run vhost on client\n"
			, progname);
}

/**
 * Convert string of given client id to integer
 *
 * If succeeded, client id of integer is assigned to client_id and
 * return SPP_RET_OK. Or return -SPP_RET_NG if failed.
 */
static int
parse_app_client_id(const char *client_id_str, int *client_id)
{
	int id = 0;
	char *endptr = NULL;

	id = strtol(client_id_str, &endptr, 0);
	if (unlikely(client_id_str == endptr) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	if (id >= RTE_MAX_LCORE)
		return SPP_RET_NG;

	*client_id = id;
	RTE_LOG(DEBUG, MIRROR, "Set client id = %d\n", *client_id);
	return SPP_RET_OK;
}

/* Parse options for server IP and port */
static int
parse_app_server(const char *server_str, char *server_ip, int *server_port)
{
	const char delim[2] = ":";
	unsigned int pos = 0;
	int port = 0;
	char *endptr = NULL;

	pos = strcspn(server_str, delim);
	if (pos >= strlen(server_str))
		return SPP_RET_NG;

	port = strtol(&server_str[pos+1], &endptr, 0);
	if (unlikely(&server_str[pos+1] == endptr) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	memcpy(server_ip, server_str, pos);
	server_ip[pos] = '\0';
	*server_port = port;
	RTE_LOG(DEBUG, MIRROR, "Set server IP   = %s\n", server_ip);
	RTE_LOG(DEBUG, MIRROR, "Set server port = %d\n", *server_port);
	return SPP_RET_OK;
}

/* Parse options for client app */
static int
parse_app_args(int argc, char *argv[])
{
	int cnt;
	int proc_flg = 0;
	int server_flg = 0;
	int option_index, opt;
	const int argcopt = argc;
	char *argvopt[argcopt];
	const char *progname = argv[0];
	static struct option lgopts[] = {
			{ "client-id", required_argument, NULL,
					SPP_LONGOPT_RETVAL_CLIENT_ID },
			{ "vhost-client", no_argument, NULL,
					SPP_LONGOPT_RETVAL_VHOST_CLIENT },
			{ 0 },
	};

	/**
	 * Save argv to argvopt to avoid losing the order of options
	 * by getopt_long()
	 */
	for (cnt = 0; cnt < argcopt; cnt++)
		argvopt[cnt] = argv[cnt];

	/* Clear startup parameters */
	memset(&g_startup_param, 0x00, sizeof(g_startup_param));

	/* Check options of application */
	optind = 0;
	opterr = 0;
	while ((opt = getopt_long(argc, argvopt, "s:", lgopts,
			&option_index)) != EOF) {
		switch (opt) {
		case SPP_LONGOPT_RETVAL_CLIENT_ID:
			if (parse_app_client_id(optarg,
						&g_startup_param.client_id) !=
						SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			proc_flg = 1;
			break;
		case SPP_LONGOPT_RETVAL_VHOST_CLIENT:
			g_startup_param.vhost_client = 1;
			break;
		case 's':
			if (parse_app_server(optarg, g_startup_param.server_ip,
					     &g_startup_param.server_port) !=
					     SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			server_flg = 1;
			break;
		default:
			usage(progname);
			return SPP_RET_NG;
		}
	}

	/* Check mandatory parameters */
	if ((proc_flg == 0) || (server_flg == 0)) {
		usage(progname);
		return SPP_RET_NG;
	}
	g_startup_param.secondary_type = SECONDARY_TYPE_MIRROR;
	RTE_LOG(INFO, MIRROR,
			"app opts (client_id=%d,type=%d,"
			"server=%s:%d,vhost_client=%d,)\n",
			g_startup_param.client_id,
			g_startup_param.secondary_type,
			g_startup_param.server_ip,
			g_startup_param.server_port,
			g_startup_param.vhost_client);
	return SPP_RET_OK;
}

/* mirror mbuf pool create */
static int
mirror_pool_create(int id)
{
	unsigned int nb_mbufs;
	char pool_name[SPP_MIRROR_POOL_NAME_MAX];

	nb_mbufs = RTE_MAX(
	    (uint16_t)(nb_rxd + nb_txd + MAX_PKT_BURST + MEMPOOL_CACHE_SIZE),
									8192U);
	sprintf(pool_name, "%s_%d", SPP_MIRROR_POOL_NAME, id);
	g_mirror_pool = rte_mempool_lookup(pool_name);
	if (g_mirror_pool == NULL) {
#ifdef SPP_MIRROR_SHALLOWCOPY
		g_mirror_pool = rte_pktmbuf_pool_create(pool_name,
						nb_mbufs,
						MEMPOOL_CACHE_SIZE,
						0,
						RTE_PKTMBUF_HEADROOM,
						rte_socket_id());
#else
		g_mirror_pool = rte_pktmbuf_pool_create(pool_name,
						nb_mbufs,
						MEMPOOL_CACHE_SIZE,
						0,
						RTE_MBUF_DEFAULT_BUF_SIZE,
						rte_socket_id());
#endif /* SPP_MIRROR_SHALLOWCOPY */
	}
	if (g_mirror_pool == NULL) {
		RTE_LOG(ERR, MIRROR, "Cannot init mbuf pool\n");
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/* Clear info */
static void
mirror_proc_init(void)
{
	int cnt = 0;
	memset(&g_mirror_info, 0x00, sizeof(g_mirror_info));
	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		g_mirror_info[cnt].ref_index = 0;
		g_mirror_info[cnt].upd_index = 1;
	}
}

/* Update mirror info */
int
spp_mirror_update(struct spp_component_info *component)
{
	int cnt = 0;
	int num_rx = component->num_rx_port;
	int num_tx = component->num_tx_port;
	struct mirror_info *info = &g_mirror_info[component->component_id];
	struct mirror_path *path = &info->path[info->upd_index];

	/* mirror component allows only one receiving port. */
	if (unlikely(num_rx > 1)) {
		RTE_LOG(ERR, MIRROR,
			"Component[%d] Setting error. (type = %d, rx = %d)\n",
			component->component_id, component->type, num_rx);
		return SPP_RET_NG;
	}

	/* Component allows only tewo transmit port. */
	if (unlikely(num_tx > 2)) {
		RTE_LOG(ERR, MIRROR,
			"Component[%d] Setting error. (type = %d, tx = %d)\n",
			component->component_id, component->type, num_tx);
		return SPP_RET_NG;
	}

	memset(path, 0x00, sizeof(struct mirror_path));

	RTE_LOG(INFO, MIRROR,
			"Component[%d] Start update component. "
			"(name = %s, type = %d)\n",
			component->component_id,
			component->name,
			component->type);

	memcpy(&path->name, component->name, SPP_NAME_STR_LEN);
	path->type = component->type;
	path->num_rx = component->num_rx_port;
	path->num_tx = component->num_tx_port;
	for (cnt = 0; cnt < num_rx; cnt++)
		memcpy(&path->ports[cnt].rx, component->rx_ports[cnt],
				sizeof(struct spp_port_info));

	/* Transmit port is set according with larger num_rx / num_tx. */
	for (cnt = 0; cnt < num_tx; cnt++)
		memcpy(&path->ports[cnt].tx, component->tx_ports[cnt],
				sizeof(struct spp_port_info));

	info->upd_index = info->ref_index;
	while (likely(info->ref_index == info->upd_index))
		rte_delay_us_block(SPP_CHANGE_UPDATE_INTERVAL);

	RTE_LOG(INFO, MIRROR,
			"Component[%d] Complete update component. "
			"(name = %s, type = %d)\n",
			component->component_id,
			component->name,
			component->type);

	return SPP_RET_OK;
}

/* Change index of mirror info */
static inline void
change_mirror_index(int id)
{
	struct mirror_info *info = &g_mirror_info[id];
	if (info->ref_index == info->upd_index) {
	/* Change reference index of port ability. */
		spp_port_ability_change_index(PORT_ABILITY_CHG_INDEX_REF, 0, 0);
		info->ref_index = (info->upd_index+1)%SPP_INFO_AREA_MAX;
	}
}

/**
 * Mirroring packets as mirror_proc
 *
 * Behavior of forwarding is defined as core_info->type which is given
 * as an argument of void and typecasted to spp_config_info.
 */
static int
mirror_proc(int id)
{
	int cnt, buf;
	int nb_rx = 0;
	int nb_tx = 0;
	int nb_tx1 = 0;
	int nb_tx2 = 0;
	struct mirror_info *info = &g_mirror_info[id];
	struct mirror_path *path = NULL;
	struct spp_port_info *rx = NULL;
	struct spp_port_info *tx = NULL;
	struct rte_mbuf *bufs[MAX_PKT_BURST];
	struct rte_mbuf *copybufs[MAX_PKT_BURST];
	struct rte_mbuf *org_mbuf = NULL;

	change_mirror_index(id);
	path = &info->path[info->ref_index];

	/* Practice condition check */
	if (!(path->num_tx == 2 && path->num_rx == 1))
		return SPP_RET_OK;

	rx = &path->ports[0].rx;
	/* Receive packets */
	nb_rx = spp_eth_rx_burst(rx->dpdk_port, 0, bufs, MAX_PKT_BURST);
	if (unlikely(nb_rx == 0))
		return SPP_RET_OK;

	/* mirror */
	tx = &path->ports[1].tx;
	if (tx->dpdk_port >= 0) {
		nb_tx2 = 0;
		for (cnt = 0; cnt < nb_rx; cnt++) {
			org_mbuf = bufs[cnt];
			rte_prefetch0(rte_pktmbuf_mtod(org_mbuf, void *));
#ifdef SPP_MIRROR_SHALLOWCOPY
			/* Shallow Copy */
			copybufs[cnt] = rte_pktmbuf_clone(org_mbuf,
							g_mirror_pool);
#else
			struct rte_mbuf *mirror_mbuf = NULL;
			struct rte_mbuf **mirror_mbufs = &mirror_mbuf;
			struct rte_mbuf *copy_mbuf = NULL;
			/* Deep Copy */
			do {
				copy_mbuf = rte_pktmbuf_alloc(g_mirror_pool);
				if (unlikely(copy_mbuf == NULL)) {
					rte_pktmbuf_free(mirror_mbuf);
					mirror_mbuf = NULL;
					RTE_LOG(INFO, MIRROR,
						"copy mbuf alloc NG!\n");
					break;
				}

				copy_mbuf->data_off = org_mbuf->data_off;
				copy_mbuf->data_len = org_mbuf->data_len;
				copy_mbuf->port = org_mbuf->port;
				copy_mbuf->vlan_tci = org_mbuf->vlan_tci;
				copy_mbuf->tx_offload = org_mbuf->tx_offload;
				copy_mbuf->hash = org_mbuf->hash;

				copy_mbuf->next = NULL;
				copy_mbuf->pkt_len = org_mbuf->pkt_len;
				copy_mbuf->nb_segs = org_mbuf->nb_segs;
				copy_mbuf->ol_flags = org_mbuf->ol_flags;
				copy_mbuf->packet_type = org_mbuf->packet_type;

				rte_memcpy(rte_pktmbuf_mtod(copy_mbuf, char *),
					rte_pktmbuf_mtod(org_mbuf, char *),
					org_mbuf->data_len);

				*mirror_mbufs = copy_mbuf;
				mirror_mbufs = &copy_mbuf->next;
			} while ((org_mbuf = org_mbuf->next) != NULL);
			copybufs[cnt] = mirror_mbuf;

#endif /* SPP_MIRROR_SHALLOWCOPY */
		}
		if (cnt != 0)
			nb_tx2 = spp_eth_tx_burst(tx->dpdk_port, 0,
							copybufs, cnt);
	}

	/* orginal */
	tx = &path->ports[0].tx;
	if (tx->dpdk_port >= 0)
		nb_tx1 = spp_eth_tx_burst(tx->dpdk_port, 0, bufs, nb_rx);
	nb_tx = nb_tx1;

	if (nb_tx1 != nb_tx2)
		RTE_LOG(INFO, MIRROR,
			"mirror paket drop nb_rx=%d nb_tx1=%d nb_tx2=%d\n",
							nb_rx, nb_tx1, nb_tx2);

	/* Discard remained packets to release mbuf */
	if (nb_tx1 < nb_tx2)
		nb_tx = nb_tx2;
	if (unlikely(nb_tx < nb_rx)) {
		for (buf = nb_tx; buf < nb_rx; buf++)
			rte_pktmbuf_free(bufs[buf]);
	}
	if (unlikely(nb_tx2 < nb_rx)) {
		for (buf = nb_tx2; buf < nb_rx; buf++)
			rte_pktmbuf_free(copybufs[buf]);
	}
	return SPP_RET_OK;
}

/* Mirror get component status */
int
spp_mirror_get_component_status(
		unsigned int lcore_id, int id,
		struct spp_iterate_core_params *params)
{
	int ret = SPP_RET_NG;
	int cnt;
	const char *component_type = NULL;
	struct mirror_info *info = &g_mirror_info[id];
	struct mirror_path *path = &info->path[info->ref_index];
	struct spp_port_index rx_ports[RTE_MAX_ETHPORTS];
	struct spp_port_index tx_ports[RTE_MAX_ETHPORTS];

	if (unlikely(path->type == SPP_COMPONENT_UNUSE)) {
		RTE_LOG(ERR, MIRROR,
				"Component[%d] Not used. "
				"(status)(core = %d, type = %d)\n",
				id, lcore_id, path->type);
		return SPP_RET_NG;
	}

	component_type = TYPE_MIRROR_STR;

	memset(rx_ports, 0x00, sizeof(rx_ports));
	for (cnt = 0; cnt < path->num_rx; cnt++) {
		rx_ports[cnt].iface_type = path->ports[cnt].rx.iface_type;
		rx_ports[cnt].iface_no   = path->ports[cnt].rx.iface_no;
	}

	memset(tx_ports, 0x00, sizeof(tx_ports));
	for (cnt = 0; cnt < path->num_tx; cnt++) {
		tx_ports[cnt].iface_type = path->ports[cnt].tx.iface_type;
		tx_ports[cnt].iface_no   = path->ports[cnt].tx.iface_no;
	}

	/* Set the information with the function specified by the command. */
	ret = (*params->element_proc)(
		params, lcore_id,
		path->name, component_type,
		path->num_rx, rx_ports, path->num_tx, tx_ports);
	if (unlikely(ret != 0))
		return SPP_RET_NG;

	return SPP_RET_OK;
}

/* Main process of slave core */
static int
slave_main(void *arg __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	int cnt = 0;
	unsigned int lcore_id = rte_lcore_id();
	enum spp_core_status status = SPP_CORE_STOP;
	struct core_mng_info *info = &g_core_info[lcore_id];
	struct core_info *core = get_core_info(lcore_id);

	RTE_LOG(INFO, MIRROR, "Core[%d] Start.\n", lcore_id);
	set_core_status(lcore_id, SPP_CORE_IDLE);

	while ((status = spp_get_core_status(lcore_id)) !=
			SPP_CORE_STOP_REQUEST) {
		if (status != SPP_CORE_FORWARD)
			continue;

		if (spp_check_core_update(lcore_id) == SPP_RET_OK) {
			/* Setting with the flush command trigger. */
			info->ref_index = (info->upd_index+1) %
					SPP_INFO_AREA_MAX;
			core = get_core_info(lcore_id);
		}

		for (cnt = 0; cnt < core->num; cnt++) {
			/*
			 * mirror returns at once.
			 * It is for processing multiple components.
			 */
			ret = mirror_proc(core->id[cnt]);
			if (unlikely(ret != 0))
				break;
		}
		if (unlikely(ret != 0)) {
			RTE_LOG(ERR, MIRROR,
				"Core[%d] Component Error. (id = %d)\n",
					lcore_id, core->id[cnt]);
			break;
		}
	}

	set_core_status(lcore_id, SPP_CORE_STOP);
	RTE_LOG(INFO, MIRROR, "Core[%d] End.\n", lcore_id);
	return ret;
}

/**
 * Main function
 *
 * Return SPP_RET_NG explicitly if error is occurred.
 */
int
main(int argc, char *argv[])
{
	int ret = SPP_RET_NG;
#ifdef SPP_DEMONIZE
	/* Daemonize process */
	int ret_daemon = daemon(0, 0);
	if (unlikely(ret_daemon != 0)) {
		RTE_LOG(ERR, MIRROR, "daemonize is failed. (ret = %d)\n",
				ret_daemon);
		return ret_daemon;
	}
#endif

	/* Signal handler registration (SIGTERM/SIGINT) */
	signal(SIGTERM, stop_process);
	signal(SIGINT,  stop_process);

	while (1) {
		int ret_dpdk = rte_eal_init(argc, argv);
		if (unlikely(ret_dpdk < 0))
			break;

		argc -= ret_dpdk;
		argv += ret_dpdk;

		/* Parse spp_mirror specific parameters */
		int ret_parse = parse_app_args(argc, argv);
		if (unlikely(ret_parse != 0))
			break;

		/* Get lcore id of main thread to set its status after */
		g_main_lcore_id = rte_lcore_id();

		/* set manage address */
		if (spp_set_mng_data_addr(&g_startup_param,
					  &g_iface_info,
					  g_component_info,
					  g_core_info,
					  g_change_core,
					  g_change_component,
					  &g_backup_info,
					  g_main_lcore_id) < 0) {
			RTE_LOG(ERR, MIRROR, "manage address set is failed.\n");
			break;
		}

		/* create the mbuf pool */
		ret = mirror_pool_create(g_startup_param.client_id);
		if (ret == SPP_RET_NG) {
			RTE_LOG(ERR, MIRROR,
					"mirror mnuf pool create failed.\n");
			return SPP_RET_NG;
		}

		int ret_mng = init_mng_data();
		if (unlikely(ret_mng != 0))
			break;

		mirror_proc_init();
		spp_port_ability_init();

		/* Setup connection for accepting commands from controller */
		int ret_command_init = spp_command_proc_init(
				g_startup_param.server_ip,
				g_startup_param.server_port);
		if (unlikely(ret_command_init != SPP_RET_OK))
			break;

#ifdef SPP_RINGLATENCYSTATS_ENABLE
		int ret_ringlatency = spp_ringlatencystats_init(
				SPP_RING_LATENCY_STATS_SAMPLING_INTERVAL,
				g_iface_info.num_ring);
		if (unlikely(ret_ringlatency != SPP_RET_OK))
			break;
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

		/* Start worker threads of classifier and forwarder */
		unsigned int lcore_id = 0;
		RTE_LCORE_FOREACH_SLAVE(lcore_id) {
			rte_eal_remote_launch(slave_main, NULL, lcore_id);
		}

		/* Set the status of main thread to idle */
		g_core_info[g_main_lcore_id].status = SPP_CORE_IDLE;
		int ret_wait = check_core_status_wait(SPP_CORE_IDLE);
		if (unlikely(ret_wait != 0))
			break;

		/* Start forwarding */
		set_all_core_status(SPP_CORE_FORWARD);
#ifdef SPP_MIRROR_SHALLOWCOPY
		RTE_LOG(INFO, MIRROR,
			"My ID %d start handling messagei(ShallowCopy)\n", 0);
#else
		RTE_LOG(INFO, MIRROR,
			"My ID %d start handling message(DeepCopy)\n", 0);
#endif /* #ifdef SPP_MIRROR_SHALLOWCOPY */
		RTE_LOG(INFO, MIRROR, "[Press Ctrl-C to quit ...]\n");

		/* Backup the management information after initialization */
		backup_mng_info(&g_backup_info);

		/* Enter loop for accepting commands */
		int ret_do = 0;
#ifndef USE_UT_SPP_VF
		while (likely(g_core_info[g_main_lcore_id].status !=
				SPP_CORE_STOP_REQUEST)) {
#else
		{
#endif
			/* Receive command */
			ret_do = spp_command_proc_do();
			if (unlikely(ret_do != SPP_RET_OK))
				break;
			/*
			 * To avoid making CPU busy, this thread waits
			 * here for 100 ms.
			 */
			usleep(100);

#ifdef SPP_RINGLATENCYSTATS_ENABLE
			print_ring_latency_stats();
#endif /* SPP_RINGLATENCYSTATS_ENABLE */
		}

		if (unlikely(ret_do != SPP_RET_OK)) {
			set_all_core_status(SPP_CORE_STOP_REQUEST);
			break;
		}

		ret = SPP_RET_OK;
		break;
	}

	/* Finalize to exit */
	if (g_main_lcore_id == rte_lcore_id()) {
		g_core_info[g_main_lcore_id].status = SPP_CORE_STOP;
		int ret_core_end = check_core_status_wait(SPP_CORE_STOP);
		if (unlikely(ret_core_end != 0))
			RTE_LOG(ERR, MIRROR, "Core did not stop.\n");

		/*
		 * Remove vhost sock file if it is not running
		 *  in vhost-client mode
		 */
		del_vhost_sockfile(g_iface_info.vhost);
	}

#ifdef SPP_RINGLATENCYSTATS_ENABLE
	spp_ringlatencystats_uninit();
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	RTE_LOG(INFO, MIRROR, "spp_mirror exit.\n");
	return ret;
}
