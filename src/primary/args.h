/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015-2016 Intel Corporation
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _PRIMARY_ARGS_H_
#define _PRIMARY_ARGS_H_

#include <stdint.h>
#include "common.h"

extern uint16_t num_clients;
extern char *server_ip;
extern int server_port;

int parse_portmask(struct port_info *ports, uint16_t max_ports,
		const char *portmask);
int parse_app_args(uint16_t max_ports, int argc, char *argv[]);

#endif /* _PRIMARY_ARGS_H_ */
