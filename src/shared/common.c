/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <rte_cycles.h>
#include "common.h"

#define RTE_LOGTYPE_SHARED RTE_LOGTYPE_USER1

/**
 * Set log level of type RTE_LOGTYPE_USER* to given level, for instance,
 * RTE_LOG_INFO or RTE_LOG_DEBUG.
 *
 * This function is typically used to output debug log as following.
 *
 *   #define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1
 *   ...
 *   set_user_log_level(1, RTE_LOG_DEBUG);
 *   ...
 *   RTE_LOG(DEBUG, APP, "Your debug log...");
 */
int
set_user_log_level(int num_user_log, uint32_t log_level)
{
	char userlog[8];

	if (num_user_log < 1 || num_user_log > 8)
		return -1;

	memset(userlog, '\0', sizeof(userlog));
	sprintf(userlog, "user%d", num_user_log);

	rte_log_set_level(rte_log_register(userlog), log_level);
	return 0;
}

/* Set log level of type RTE_LOGTYPE_USER* to RTE_LOG_DEBUG. */
int
set_user_log_debug(int num_user_log)
{
	set_user_log_level(num_user_log, RTE_LOG_DEBUG);
	return 0;
}

/**
 * Take the number of clients passed with `-n` option and convert to
 * to a number to store in the num_clients variable.
 *
 * TODO(yasufum): Revise the usage of this function for spp_primary because
 * it does not use for the number of ring ports, but clients. The name of
 * function is inadequte.
 */
int
parse_num_clients(uint16_t *num_clients, const char *clients)
{
	char *end = NULL;
	unsigned long temp;

	if (clients == NULL || *clients == '\0')
		return -1;

	temp = strtoul(clients, &end, 10);
	if (end == NULL || *end != '\0' || temp == 0)
		return -1;

	*num_clients = (uint16_t)temp;
	return 0;
}

int
parse_server(char **server_ip, int *server_port, char *server_addr)
{
	const char delim[2] = ":";
	char *token;

	if (server_addr == NULL || *server_addr == '\0')
		return -1;

	*server_ip = strtok(server_addr, delim);
	RTE_LOG(DEBUG, SHARED, "server ip %s\n", *server_ip);

	token = strtok(NULL, delim);
	RTE_LOG(DEBUG, SHARED, "token %s\n", token);
	if (token == NULL || *token == '\0')
		return -1;

	RTE_LOG(DEBUG, SHARED, "token %s\n", token);
	*server_port = atoi(token);
	return 0;
}

/* Get directory name of given proc_name */
int get_sec_dir(char *proc_name, char *dir_name)
{
	if (!strcmp(proc_name, "spp_nfv")) {
		sprintf(dir_name, "%s", "nfv");
		RTE_LOG(DEBUG, SHARED, "Found dir 'nfv' for '%s'.\n",
				proc_name);
	} else if (!strcmp(proc_name, "spp_vf")) {
		sprintf(dir_name, "%s", "vf");
		RTE_LOG(DEBUG, SHARED, "Found dir 'vf' for '%s'.\n",
				proc_name);
	} else if (!strcmp(proc_name, "spp_mirror")) {
		sprintf(dir_name, "%s", "mirror");
		RTE_LOG(DEBUG, SHARED, "Found dir 'mirror' for '%s'.\n",
				proc_name);
	} else if (!strcmp(proc_name, "spp_pcap")) {
		sprintf(dir_name, "%s", "pcap");
		RTE_LOG(DEBUG, SHARED, "Found dir 'pcap' for '%s'.\n",
				proc_name);
	} else {
		RTE_LOG(DEBUG, SHARED, "No dir found for '%s'.\n",
				proc_name);
	}
	return 0;
}
