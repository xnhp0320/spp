/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include "spp_proc.h"
#include "spp_vf.h"
#include "classifier_mac.h"
#include "spp_forward.h"
#include "command_proc.h"
#include "spp_port.h"

/* Declare global variables */
/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_VF RTE_LOGTYPE_USER2

/* Logical core ID for main process */
static unsigned int g_main_lcore_id = 0xffffffff;

/* Execution parameter of spp_vf */
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

/* Print help message */
static void
usage(const char *progname)
{
	RTE_LOG(INFO, VF, "Usage: %s [EAL args] --"
			" --client-id CLIENT_ID"
			" -s SERVER_IP:SERVER_PORT"
			" [--vhost-client]\n"
			" --client-id CLIENT_ID   : My client ID\n"
			" -s SERVER_IP:SERVER_PORT  :"
			" Access information to the server\n"
			" --vhost-client            : Run vhost on client\n"
			, progname);
}

/**
 * Convert string of given client id to integer
 *
 * If succeeded, client id of integer is assigned to client_id and
 * return SPP_RET_OK Or return SPP_RET_NG if failed.
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
	RTE_LOG(DEBUG, VF, "Set client id = %d\n", *client_id);
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
	if (unlikely(&server_str[pos+1] == endptr) ||
				unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	memcpy(server_ip, server_str, pos);
	server_ip[pos] = '\0';
	*server_port = port;
	RTE_LOG(DEBUG, VF, "Set server IP   = %s\n", server_ip);
	RTE_LOG(DEBUG, VF, "Set server port = %d\n", *server_port);
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
	RTE_LOG(INFO, VF,
			"app opts (client_id=%d,server=%s:%d,"
			"vhost_client=%d)\n",
			g_startup_param.client_id,
			g_startup_param.server_ip,
			g_startup_param.server_port,
			g_startup_param.vhost_client);
	return SPP_RET_OK;
}

/* Main process of slave core */
static int
slave_main(void *arg __attribute__ ((unused)))
{
	int ret = 0;
	int cnt = 0;
	unsigned int lcore_id = rte_lcore_id();
	enum spp_core_status status = SPP_CORE_STOP;
	struct core_mng_info *info = &g_core_info[lcore_id];
	struct core_info *core = get_core_info(lcore_id);

	RTE_LOG(INFO, VF, "Core[%d] Start.\n", lcore_id);
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

		/* It is for processing multiple components. */
		for (cnt = 0; cnt < core->num; cnt++) {
			/* Component classification to call a function. */
			if (spp_get_component_type(core->id[cnt]) ==
					SPP_COMPONENT_CLASSIFIER_MAC) {
				/* Component type for classifier. */
				ret = spp_classifier_mac_do(core->id[cnt]);
				if (unlikely(ret != 0))
					break;
			} else {
				/* Component type for forward or merge. */
				ret = spp_forward(core->id[cnt]);
				if (unlikely(ret != 0))
					break;
			}
		}
		if (unlikely(ret != 0)) {
			RTE_LOG(ERR, VF, "Core[%d] Component Error. "
					"(id = %d)\n",
					lcore_id, core->id[cnt]);
			break;
		}
	}

	set_core_status(lcore_id, SPP_CORE_STOP);
	RTE_LOG(INFO, VF, "Core[%d] End.\n", lcore_id);
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
		RTE_LOG(ERR, VF, "daemonize is failed. (ret = %d)\n",
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

		/* Parse spp_vf specific parameters */
		int ret_parse = parse_app_args(argc, argv);
		if (unlikely(ret_parse != SPP_RET_OK))
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
					  g_main_lcore_id) < SPP_RET_OK) {
			RTE_LOG(ERR, VF, "manage address set is failed.\n");
			break;
		}

		int ret_mng = init_mng_data();
		if (unlikely(ret_mng != SPP_RET_OK))
			break;

		int ret_classifier_mac_init = spp_classifier_mac_init();
		if (unlikely(ret_classifier_mac_init != SPP_RET_OK))
			break;

		spp_forward_init();
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
		if (unlikely(ret_wait != SPP_RET_OK))
			break;

		/* Start forwarding */
		set_all_core_status(SPP_CORE_FORWARD);
		RTE_LOG(INFO, VF, "My ID %d start handling message\n", 0);
		RTE_LOG(INFO, VF, "[Press Ctrl-C to quit ...]\n");

		/* Backup the management information after initialization */
		backup_mng_info(&g_backup_info);

		/* Enter loop for accepting commands */
		int ret_do = SPP_RET_OK;
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
			* Wait to avoid CPU overloaded.
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
		if (unlikely(ret_core_end != SPP_RET_OK))
			RTE_LOG(ERR, VF, "Core did not stop.\n");

		/*
		 * Remove vhost sock file if it is not running
		 *  in vhost-client mode
		 */
		del_vhost_sockfile(g_iface_info.vhost);
	}

#ifdef SPP_RINGLATENCYSTATS_ENABLE
	spp_ringlatencystats_uninit();
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

	RTE_LOG(INFO, VF, "spp_vf exit.\n");
	return ret;
}
