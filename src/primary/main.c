/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <signal.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <poll.h>
#include <fcntl.h>

#include <rte_atomic.h>
#include <rte_eth_ring.h>

#include "shared/common.h"
#include "args.h"
#include "init.h"
#include "primary.h"

/*
 * Buffer sizes of status message of primary. Total number of size
 * must be equal to MSG_SIZE 2048 defined in `shared/common.h`.
 */
#define PRI_BUF_SIZE_LCORE 128
#define PRI_BUF_SIZE_PHY 512
#define PRI_BUF_SIZE_RING (MSG_SIZE - PRI_BUF_SIZE_LCORE - PRI_BUF_SIZE_PHY)

#define SPP_PATH_LEN 1024  /* seems enough for path of spp procs */
#define NOF_TOKENS 48  /* seems enough to contain tokens */
/* should be contain extra two tokens for `python` and path of launcher */
#define NOF_CMD_LIST (NOF_TOKENS + 2)

#define LAUNCH_CMD "python"
#define LCMD_LEN 8

#define LAUNCHER_NAME "sec_launcher.py"
#define LAUNCHER_LEN 16

#define POLL_TIMEOUT_MS 100

static sig_atomic_t on = 1;

static enum cmd_type cmd = STOP;

static struct pollfd pfd;

/* global var - extern in header */
uint8_t lcore_id_used[RTE_MAX_LCORE];

static void
turn_off(int sig)
{
	on = 0;
	RTE_LOG(INFO, PRIMARY, "terminated %d\n", sig);
}

static const char *
get_printable_mac_addr(uint16_t port)
{
	static const char err_address[] = "00:00:00:00:00:00";
	static char addresses[RTE_MAX_ETHPORTS][sizeof(err_address)];

	if (unlikely(port >= RTE_MAX_ETHPORTS))
		return err_address;

	if (unlikely(addresses[port][0] == '\0')) {
		struct rte_ether_addr mac;

		rte_eth_macaddr_get(port, &mac);
		snprintf(addresses[port], sizeof(addresses[port]),
			"%02x:%02x:%02x:%02x:%02x:%02x\n",
			mac.addr_bytes[0], mac.addr_bytes[1],
			mac.addr_bytes[2], mac.addr_bytes[3],
			mac.addr_bytes[4], mac.addr_bytes[5]);
	}

	return addresses[port];
}

/*
 * This function displays the recorded statistics for each port
 * and for each client. It uses ANSI terminal codes to clear
 * screen when called. It is called from a single non-master
 * thread in the server process, when the process is run with more
 * than one lcore enabled.
 */
static void
do_stats_display(void)
{
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H', '\0' };
	const char clr[] = { 27, '[', '2', 'J', '\0' };
	unsigned int i;

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("PORTS\n");
	printf("-----\n");
	for (i = 0; i < ports->num_ports; i++)
		printf("Port %u: '%s'\t", ports->id[i],
			get_printable_mac_addr(ports->id[i]));
	printf("\n\n");
	for (i = 0; i < ports->num_ports; i++) {
		printf("Port %u - rx: %9"PRIu64"\t tx: %9"PRIu64"\t"
			" tx_drop: %9"PRIu64"\n",
			ports->id[i], ports->port_stats[i].rx,
			ports->port_stats[i].tx,
			ports->client_stats[i].tx_drop);
	}

	printf("\nCLIENTS\n");
	printf("-------\n");
	for (i = 0; i < num_rings; i++) {
		printf("Client %2u - rx: %9"PRIu64", rx_drop: %9"PRIu64"\n"
			"            tx: %9"PRIu64", tx_drop: %9"PRIu64"\n",
			i, ports->client_stats[i].rx,
			ports->client_stats[i].rx_drop,
			ports->client_stats[i].tx,
			ports->client_stats[i].tx_drop);
	}

	printf("\n");
}

/*
 * The function called from each non-master lcore used by the process.
 * The test_and_set function is used to randomly pick a single lcore on which
 * the code to display the statistics will run. Otherwise, the code just
 * repeatedly sleeps.
 */
static int
sleep_lcore(void *dummy __rte_unused)
{
	/* Used to pick a display thread - static, so zero-initialised */
	static rte_atomic32_t display_stats;

	/* Only one core should display stats */
	if (rte_atomic32_test_and_set(&display_stats)) {
		const unsigned int sleeptime = 1;

		RTE_LOG(INFO, PRIMARY, "Core %u displaying statistics\n",
				rte_lcore_id());

		/* Longer initial pause so above log is seen */
		sleep(sleeptime * 3);

		/* Loop forever: sleep always returns 0 or <= param */
		while (sleep(sleeptime) <= sleeptime && on)
			do_stats_display();
	}

	return 0;
}

/*
 * Function to set all the client statistic values to zero.
 * Called at program startup.
 */
static void
clear_stats(void)
{
	memset(ports->port_stats, 0, sizeof(struct stats) * RTE_MAX_ETHPORTS);
	memset(ports->client_stats, 0, sizeof(struct stats) * MAX_CLIENT);
}

static int
do_send(int *connected, int *sock, char *str)
{
	int ret;

	ret = send(*sock, str, MSG_SIZE, 0);
	if (ret == -1) {
		RTE_LOG(ERR, PRIMARY, "send failed");
		*connected = 0;
		return -1;
	}

	RTE_LOG(INFO, PRIMARY, "To Server: %s\n", str);

	return 0;
}

/*
 * Launch secondary process of given name and ID.
 *
 * This function finds the path of secondary by using the path of spp_primary
 * itself and given proc name.
 *
 * Output of launched proc is sent to logfile located in `log` directory in
 * the project root, and the name of logfile is a combination of proc name
 * and ID, such as `spp_nfv-1.log`.
 */
static int
launch_sec_proc(char *sec_name, int sec_id, char **sec_args)
{
	char path_spp_pri[SPP_PATH_LEN];
	char path_spp_sec[SPP_PATH_LEN];
	char path_spp_log[SPP_PATH_LEN];

	char cmd[LCMD_LEN];  /* Command for launcher */
	char launcher[LAUNCHER_LEN];  /* Name of launcher script */
	char path_launcher[SPP_PATH_LEN];

	/* Contains elems of path_spp_pri */
	char *token_list[NOF_TOKENS] = {NULL};

	/* Contains cmd and args for execvp() */
	char *cmd_list[NOF_CMD_LIST] = {NULL};

	int num_token = 0;
	int i = 0;
	char sec_dirname[16];
	int fd;

	/* Get path of spp_primary to be used to find secondary */
	if (readlink("/proc/self/exe",
				path_spp_pri, sizeof(path_spp_pri)) == -1)
		RTE_LOG(INFO, PRIMARY,
				"Failed to find exec file of spp_primary.\n");
	else {
		/* Tokenize path of spp_primary */
		token_list[i] = strtok(path_spp_pri, "/");
		while (token_list[i] != NULL) {
			// RTE_LOG(DEBUG, PRIMARY, "token: %s\n",
			//		token_list[i]);
			i++;
			num_token++;
			token_list[i] = strtok(NULL, "/");
		}

		/* Get path of `src` dir used as a basename */
		for (i = 0; i < num_token - 3; i++) {
			if (i == 0)
				sprintf(path_spp_sec, "/%s/", token_list[i]);
			else
				sprintf(path_spp_sec + strlen(path_spp_sec),
						"%s/", token_list[i]);
		}

		/* Dir of logfile is located in the parent dir of src */
		sprintf(path_spp_log, "%s../log/%s-%d.log",
				path_spp_sec, sec_name, sec_id);

		/* Setup cmd list */
		memset(cmd, '\0', sizeof(cmd));
		sprintf(cmd, "%s", LAUNCH_CMD);
		cmd_list[0] = cmd;

		/* Path of launcher is `spp/tools/helpers/sec_launcher.py` */
		memset(launcher, '\0', sizeof(launcher));
		sprintf(launcher, "%s", LAUNCHER_NAME);
		memset(path_launcher, '\0', sizeof(path_launcher));
		sprintf(path_launcher, "%s../tools/helpers/%s",
				path_spp_sec, launcher);
		cmd_list[1] = path_launcher;

		/* path of sec proc */
		get_sec_dir(sec_name, sec_dirname);
		sprintf(path_spp_sec + strlen(path_spp_sec), "%s/%s/%s",
				sec_dirname, token_list[num_token-2],
				sec_name);
		cmd_list[2] = path_spp_sec;

		i = 0;
		while (sec_args[i] != NULL) {
			cmd_list[i+3] = sec_args[i];
			i++;
		}

		/* Output debug log for checking tokens of cmd_list */
		i = 0;
		while (cmd_list[i] != NULL) {
			RTE_LOG(DEBUG, PRIMARY, "launch, token[%2d] = %s\n",
					i, cmd_list[i]);
			i++;
		}

		RTE_LOG(DEBUG, PRIMARY, "sec_path: '%s'.\n", path_spp_sec);
		RTE_LOG(DEBUG, PRIMARY, "sec_log: '%s'.\n", path_spp_log);

		pid_t pid;
		pid = fork();
		if (pid < 0)
			RTE_LOG(ERR, PRIMARY,
					"Failed to open secondary proc.\n");
		else if (pid == 0) {
			/* Open log file with mode equals to 'a+' */
			fd = open(path_spp_log,
					O_RDWR | O_CREAT | O_APPEND,
					0666);

			/* change to output of stdout and stderr to logfile */
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);

			printf("%s\n", sec_args[0]);
			if (execvp(cmd_list[0], cmd_list) != 0)
				RTE_LOG(ERR, PRIMARY,
					"Failed to open child proc!\n");
		} else
			RTE_LOG(INFO, PRIMARY, "Launched '%s' with ID %d.\n",
					path_spp_sec, sec_id);
	}

	return 0;
}

/**
 * Retrieve all of statu of ports as JSON format managed by primary.
 *
 * Here is an exmaple.
 *
 * {
 *     "lcores": [0],
 *     "ring_ports": [
 *     {
 *         "id": 0,
 *             "rx": 0,
 *             "rx_drop": 0,
 *             "tx": 0,
 *             "tx_drop": 0
 *     },
 *     ...
 *     ],
 *     "phy_ports": [
 *     {
 *         "eth": "56:48:4f:53:54:00",
 *         "id": 0,
 *         "rx": 0,
 *         "tx": 0,
 *         "tx_drop": 0
 *     },
 *     ...
 *     ]
 * }
 */
static int
get_status_json(char *str)
{
	int i;
	int lcore_buf_size = PRI_BUF_SIZE_LCORE;
	int phyp_buf_size = PRI_BUF_SIZE_PHY;
	int ringp_buf_size = PRI_BUF_SIZE_RING;
	char lcore_ids[PRI_BUF_SIZE_LCORE];
	char phy_ports[phyp_buf_size];
	char ring_ports[ringp_buf_size];
	memset(lcore_ids, '\0', lcore_buf_size);
	memset(phy_ports, '\0', phyp_buf_size);
	memset(ring_ports, '\0', ringp_buf_size);

	int buf_size = 256;  /* size of temp buffer */
	char lcore_id[108];  /* seems enough */
	char phy_port[buf_size];
	char ring_port[buf_size];

	memset(lcore_id, '\0', sizeof(lcore_id));
	for (i = 0; i < RTE_MAX_LCORE; i++) {
		if (lcore_id_used[i] == 1)
			sprintf(lcore_id + strlen(lcore_id), "%d,", i);
	}
	sprintf(lcore_id + strlen(lcore_id) - 1, "%s", "");

	sprintf(lcore_ids, "\"lcores\":[%s]", lcore_id);

	for (i = 0; i < ports->num_ports; i++) {

		RTE_LOG(DEBUG, PRIMARY, "Size of phy_ports str: %d\n",
				(int)strlen(phy_ports));

		memset(phy_port, '\0', buf_size);

		sprintf(phy_port, "{\"id\": %u, \"eth\": \"%s\", "
				"\"rx\": %"PRIu64", \"tx\": %"PRIu64", "
				"\"tx_drop\": %"PRIu64"}",
				ports->id[i],
				get_printable_mac_addr(ports->id[i]),
				ports->port_stats[i].rx,
				ports->port_stats[i].tx,
				ports->client_stats[i].tx_drop);

		int cur_buf_size = (int)strlen(phy_ports) +
			(int)strlen(phy_port);
		if (cur_buf_size > phyp_buf_size - 1) {
			RTE_LOG(ERR, PRIMARY,
				"Cannot send all of phy_port stats (%d/%d)\n",
				i, ports->num_ports);
			sprintf(phy_ports + strlen(phy_ports) - 1, "%s", "");
			break;
		}

		sprintf(phy_ports + strlen(phy_ports), "%s", phy_port);

		if (i < ports->num_ports - 1)
			sprintf(phy_ports, "%s,", phy_ports);
	}

	for (i = 0; i < num_rings; i++) {

		RTE_LOG(DEBUG, PRIMARY, "Size of ring_ports str: %d\n",
				(int)strlen(ring_ports));

		memset(ring_port, '\0', buf_size);

		sprintf(ring_port, "{\"id\": %u, \"rx\": %"PRIu64", "
			"\"rx_drop\": %"PRIu64", "
			"\"tx\": %"PRIu64", \"tx_drop\": %"PRIu64"}",
			i,
			ports->client_stats[i].rx,
			ports->client_stats[i].rx_drop,
			ports->client_stats[i].tx,
			ports->client_stats[i].tx_drop);

		int cur_buf_size = (int)strlen(ring_ports) +
			(int)strlen(ring_port);
		if (cur_buf_size > ringp_buf_size - 1) {
			RTE_LOG(ERR, PRIMARY,
				"Cannot send all of ring_port stats (%d/%d)\n",
				i, num_rings);
			sprintf(ring_ports + strlen(ring_ports) - 1, "%s", "");
			break;
		}

		sprintf(ring_ports + strlen(ring_ports), "%s", ring_port);

		if (i < num_rings - 1)
			sprintf(ring_ports, "%s,", ring_ports);
	}

	RTE_LOG(DEBUG, PRIMARY,
			"{%s, \"phy_ports\": [%s], \"ring_ports\": [%s]}\n",
			lcore_ids, phy_ports, ring_ports);

	sprintf(str, "{%s, \"phy_ports\": [%s], \"ring_ports\": [%s]}",
			lcore_ids, phy_ports, ring_ports);

	return 0;
}

static int
parse_command(char *str)
{
	char *token_list[MAX_PARAMETER] = {NULL};
	char sec_name[16];
	char *sec_args[NOF_TOKENS] = {NULL};
	int ret = 0;
	int i = 0;

	memset(sec_name, '\0', 16);

	/* tokenize the user commands from controller */
	token_list[i] = strtok(str, " ");
	while (token_list[i] != NULL) {
		RTE_LOG(DEBUG, PRIMARY,
				"parse command, token[%2d] = %s\n",
				i, token_list[i]);
		if (i == 2)
			sprintf(sec_name, "%s", token_list[i]);
		else if (i > 2)
			sec_args[i-3] = token_list[i];
		i++;
		token_list[i] = strtok(NULL, " ");
	}

	if (!strcmp(token_list[0], "status")) {
		RTE_LOG(DEBUG, PRIMARY, "'status' command received.\n");

		/* Clear str and token_list nouse already */
		memset(str, '\0', MSG_SIZE);
		ret = get_status_json(str);

	} else if (!strcmp(token_list[0], "launch")) {
		RTE_LOG(DEBUG, PRIMARY, "'%s' command received.\n",
				token_list[0]);

		ret = launch_sec_proc(sec_name,
				strtod(token_list[1], NULL), sec_args);

	} else if (!strcmp(token_list[0], "exit")) {
		RTE_LOG(DEBUG, PRIMARY, "'exit' command received.\n");
		cmd = STOP;
		ret = -1;

	} else if (!strcmp(token_list[0], "clear")) {
		sprintf(str, "{\"status\": \"cleared\"}");
		clear_stats();
	}

	return ret;
}

static int
do_receive(int *connected, int *sock, char *str)
{
	int ret;

	memset(str, '\0', MSG_SIZE);

	ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
	if (ret <= 0) {
		if (ret < 0) {
			close(*sock);
			*sock = SOCK_RESET;
			*connected = 0;
		}
		return -1;
	}

	ret = recv(*sock, str, MSG_SIZE, 0);
	if (ret <= 0) {
		RTE_LOG(DEBUG, PRIMARY, "Receive count: %d\n", ret);

		if (ret < 0)
			RTE_LOG(ERR, PRIMARY, "Receive Fail");
		else
			RTE_LOG(INFO, PRIMARY, "Receive 0\n");

		RTE_LOG(INFO, PRIMARY, "Assume Server closed connection\n");
		close(*sock);
		*sock = SOCK_RESET;
		*connected = 0;
		return -1;
	}

	return 0;
}

static int
do_connection(int *connected, int *sock)
{
	static struct sockaddr_in servaddr;
	int ret = 0;

	if (*connected == 0) {
		if (*sock < 0) {
			RTE_LOG(INFO, PRIMARY, "Creating socket...\n");
			*sock = socket(AF_INET, SOCK_STREAM, 0);
			if (*sock < 0)
				rte_exit(EXIT_FAILURE, "socket error\n");

			/* Creation of the socket */
			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_addr.s_addr = inet_addr(server_ip);
			servaddr.sin_port = htons(server_port);

			pfd.fd = *sock;
			pfd.events = POLLIN;
		}

		RTE_LOG(INFO,
			PRIMARY, "Trying to connect ... socket %d\n", *sock);
		ret = connect(*sock, (struct sockaddr *) &servaddr,
			sizeof(servaddr));
		if (ret < 0) {
			RTE_LOG(ERR, PRIMARY, "Connection Error");
			return ret;
		}

		RTE_LOG(INFO, PRIMARY, "Connected\n");
		*connected = 1;
	}

	return ret;
}

int
main(int argc, char *argv[])
{
	int sock = SOCK_RESET;
	int connected = 0;
	char str[MSG_SIZE];
	int flg_exit;  // used as res of parse_command() to exit if -1
	int ret;

	set_user_log_debug(1);

	/* Register signals */
	signal(SIGINT, turn_off);

	/* initialise the system */
	if (init(argc, argv) < 0)
		return -1;

	RTE_LOG(INFO, PRIMARY, "Finished Process Init.\n");

	/* clear statistics */
	clear_stats();

	/* put all other cores to sleep bar master */
	rte_eal_mp_remote_launch(sleep_lcore, NULL, SKIP_MASTER);

	while (on) {
		ret = do_connection(&connected, &sock);
		if (ret < 0) {
			usleep(CONN_RETRY_USEC);
			continue;
		}

		ret = do_receive(&connected, &sock, str);
		if (ret < 0)
			continue;

		RTE_LOG(DEBUG, PRIMARY, "Received string: %s\n", str);

		flg_exit = parse_command(str);

		/* Send the message back to client */
		ret = do_send(&connected, &sock, str);

		if (flg_exit < 0)  /* terminate process if exit is called */
			break;
		else if (ret < 0)
			continue;
	}

	/* exit */
	close(sock);
	sock = SOCK_RESET;
	RTE_LOG(INFO, PRIMARY, "spp_primary exit.\n");
	return 0;
}
