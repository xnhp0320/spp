/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_log.h>
#include <rte_branch_prediction.h>

#include "spp_port.h"
#include "string_buffer.h"
#ifdef SPP_VF_MODULE
#include "../classifier_mac.h"
#include "../spp_forward.h"
#endif /* SPP_VF_MODULE */
#ifdef SPP_MIRROR_MODULE
#include "../../mirror/spp_mirror.h"
#endif /* SPP_MIRROR_MODULE */
#include "command_conn.h"
#include "command_parse.h"
#include "command_proc.h"

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_COMMAND_PROC RTE_LOGTYPE_USER2

/* request message initial size */
#define CMD_RES_ERR_MSG_SIZE  160
#define CMD_TAG_APPEND_SIZE   16
#define CMD_REQ_BUF_INIT_SIZE 2048
#define CMD_RES_BUF_INIT_SIZE 2048

#define JSON_APPEND_COMMA(flg)    ((flg)?",":"")
#define JSON_APPEND_VALUE(format) "%s\"%s\":"format
#define JSON_APPEND_ARRAY         "%s\"%s\":[%s]"
#define JSON_APPEND_BLOCK         "%s\"%s\":{%s}"
#define JSON_APPEND_BLOCK_NONAME  "%s%s{%s}"

/* command execution result type */
enum command_result_type {
	CMD_SUCCESS = 0,
	CMD_FAILURE,
	CMD_INVALID,
};

/* command execution result information */
struct command_result {
	/* Response code */
	int code;

	/* Response message */
	char msg[CMD_NAME_BUFSZ];

	/* Detailed response message */
	char error_message[CMD_RES_ERR_MSG_SIZE];
};

/* command response list control structure */
struct command_response_list {
	/* JSON Tag name */
	char tag_name[CMD_NAME_BUFSZ];

	/* Pointer to handling function */
	int (*func)(const char *name, char **output, void *tmp);
};

/*
 * classifier type string list
 * do it same as the order of enum spp_classifier_type (spp_proc.h)
 */
const char *CLASSIFILER_TYPE_STATUS[] = {
	"none",
	"mac",
	"vlan",

	/* termination */ "",
};

/*
 * seconary type string list
 * do it same the order enum secondary_type (spp_proc.h)
 */
const char *SECONDARY_PROCESS_TYPE_SRINGS[] = {
	"none",
	"vf",
	"mirror",

	/* termination */ "",
};

/*
 * port ability string list
 * do it same as the order of enum spp_port_ability_type (spp_vf.h)
 */
const char *PORT_ABILITY_STATUS_STRINGS[] = {
	"none",
	"add",
	"del",

	/* termination */ "",
};

/* get client id */
static int
spp_get_client_id(void)
{
	struct startup_param *startup_param;

	spp_get_mng_data_addr(&startup_param,
			NULL, NULL, NULL, NULL, NULL, NULL);
	return startup_param->client_id;
}

/* get process type */
static int
spp_get_process_type(void)
{
	struct startup_param *startup_param;

	spp_get_mng_data_addr(&startup_param,
			NULL, NULL, NULL, NULL, NULL, NULL);
	return startup_param->secondary_type;
}

/* Check if port has been flushed. */
static int
spp_check_flush_port(enum port_type iface_type, int iface_no)
{
	struct spp_port_info *port = get_iface_info(iface_type, iface_no);
	return port->dpdk_port >= 0;
}

/* update classifier table according to the specified action(add or del). */
static int
spp_update_classifier_table(
		enum spp_command_action action,
		enum spp_classifier_type type __attribute__ ((unused)),
		int vid,
		const char *mac_addr_str,
		const struct spp_port_index *port)
{
	struct spp_port_info *port_info = NULL;
	int64_t ret_mac = 0;
	uint64_t mac_addr = 0;

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "update_classifier_table "
			"( type = mac, mac addr = %s, port = %d:%d )\n",
			mac_addr_str, port->iface_type, port->iface_no);

	ret_mac = spp_change_mac_str_to_int64(mac_addr_str);
	if (unlikely(ret_mac == -1)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
			"MAC address format error. ( mac = %s )\n",
			mac_addr_str);
		return SPP_RET_NG;
	}
	mac_addr = (uint64_t)ret_mac;

	port_info = get_iface_info(port->iface_type, port->iface_no);
	if (unlikely(port_info == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC, "No port. ( port = %d:%d )\n",
				port->iface_type, port->iface_no);
		return SPP_RET_NG;
	}
	if (unlikely(port_info->iface_type == UNDEF)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Port not added. ( port = %d:%d )\n",
				port->iface_type, port->iface_no);
		return SPP_RET_NG;
	}

	if (action == CMD_ACTION_DEL) {
		/* Delete */
		if ((port_info->class_id.vlantag.vid != 0) &&
				unlikely(port_info->class_id.vlantag.vid !=
				vid)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC, "VLAN ID is different. "
					"( vid = %d )\n", vid);
			return SPP_RET_NG;
		}
		if ((port_info->class_id.mac_addr != 0) &&
			unlikely(port_info->class_id.mac_addr !=
					mac_addr)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"MAC address is different. "
					"( mac = %s )\n", mac_addr_str);
			return SPP_RET_NG;
		}

		port_info->class_id.vlantag.vid = ETH_VLAN_ID_MAX;
		port_info->class_id.mac_addr    = 0;
		memset(port_info->class_id.mac_addr_str, 0x00,
							SPP_MIN_STR_LEN);

	} else if (action == CMD_ACTION_ADD) {
		/* Setting */
		if (unlikely(port_info->class_id.vlantag.vid !=
				ETH_VLAN_ID_MAX)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC, "Port in used. "
					"( port = %d:%d, vlan = %d != %d )\n",
					port->iface_type, port->iface_no,
					port_info->class_id.vlantag.vid, vid);
			return SPP_RET_NG;
		}
		if (unlikely(port_info->class_id.mac_addr != 0)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC, "Port in used. "
					"( port = %d:%d, mac = %s != %s )\n",
					port->iface_type, port->iface_no,
					port_info->class_id.mac_addr_str,
					mac_addr_str);
			return SPP_RET_NG;
		}

		port_info->class_id.vlantag.vid = vid;
		port_info->class_id.mac_addr    = mac_addr;
		strcpy(port_info->class_id.mac_addr_str, mac_addr_str);
	}

	set_component_change_port(port_info, SPP_PORT_RXTX_TX);
	return SPP_RET_OK;
}

/**
 * Assign or remove component to/from specified lcore depending
 * on component action
 */
static int
spp_update_component(
		enum spp_command_action action,
		const char *name,
		unsigned int lcore_id,
		enum spp_component_type type)
{
	int ret = SPP_RET_NG;
	int ret_del = -1;
	int component_id = 0;
	unsigned int tmp_lcore_id = 0;
	struct spp_component_info *comp_info = NULL;
	struct core_info *core = NULL;
	struct core_mng_info *info = NULL;
	struct spp_component_info *comp_info_base = NULL;
	struct core_mng_info *core_info = NULL;
	int *change_core = NULL;
	int *change_component = NULL;

	spp_get_mng_data_addr(NULL, NULL, &comp_info_base, &core_info,
				&change_core, &change_component, NULL);

	switch (action) {
	case CMD_ACTION_START:
		info = (core_info + lcore_id);
		if (info->status == SPP_CORE_UNUSE) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Core %d is not available because it is in "
				"SPP_CORE_UNUSE state.\n", lcore_id);
			return SPP_RET_NG;
		}

		component_id = spp_get_component_id(name);
		if (component_id >= 0) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Component name '%s' is already used.\n", name);
			return SPP_RET_NG;
		}

		component_id = get_free_component();
		if (component_id < 0) {
			RTE_LOG(ERR, SPP_COMMAND_PROC, "Cannot assign "
				"component over the maximum number.\n");
			return SPP_RET_NG;
		}

		core = &info->core[info->upd_index];

		comp_info = (comp_info_base + component_id);
		memset(comp_info, 0x00, sizeof(struct spp_component_info));
		strcpy(comp_info->name, name);
		comp_info->type		= type;
		comp_info->lcore_id	= lcore_id;
		comp_info->component_id	= component_id;

		core->id[core->num] = component_id;
		core->num++;
		ret = SPP_RET_OK;
		tmp_lcore_id = lcore_id;
		*(change_component + component_id) = 1;
		break;

	case CMD_ACTION_STOP:
		component_id = spp_get_component_id(name);
		if (component_id < 0)
			return SPP_RET_OK;

		comp_info = (comp_info_base + component_id);
		tmp_lcore_id = comp_info->lcore_id;
		memset(comp_info, 0x00, sizeof(struct spp_component_info));

		info = (core_info + tmp_lcore_id);
		core = &info->core[info->upd_index];

#ifdef SPP_VF_MODULE
		/* initialize classifier information */
		if (comp_info->type == SPP_COMPONENT_CLASSIFIER_MAC)
			init_classifier_info(component_id);
#endif /* SPP_VF_MODULE */

		ret_del = del_component_info(component_id,
				core->num, core->id);
		if (ret_del >= 0)
			/* If deleted, decrement number. */
			core->num--;

		ret = SPP_RET_OK;
		*(change_component + component_id) = 0;
		break;

	default:
		break;
	}

	*(change_core + tmp_lcore_id) = 1;
	return ret;
}

/* Check if over the maximum num of rx and tx ports of component. */
static int
check_port_count(int component_type, enum spp_port_rxtx rxtx, int num_rx,
								int num_tx)
{
	RTE_LOG(INFO, SPP_COMMAND_PROC, "port count, port_type=%d,"
				" rx=%d, tx=%d\n", rxtx, num_rx, num_tx);
	if (rxtx == SPP_PORT_RXTX_RX)
		num_rx++;
	else
		num_tx++;
	/* Add rx or tx port appointed in port_type. */
	RTE_LOG(INFO, SPP_COMMAND_PROC, "Num of ports after count up,"
				" port_type=%d, rx=%d, tx=%d\n",
				rxtx, num_rx, num_tx);
	switch (component_type) {
	case SPP_COMPONENT_FORWARD:
		if (num_rx > 1 || num_tx > 1)
			return SPP_RET_NG;
		break;

	case SPP_COMPONENT_MERGE:
		if (num_tx > 1)
			return SPP_RET_NG;
		break;

	case SPP_COMPONENT_CLASSIFIER_MAC:
		if (num_rx > 1)
			return SPP_RET_NG;
		break;

	case SPP_COMPONENT_MIRROR:
		if (num_rx > 1 || num_tx > 2)
			return SPP_RET_NG;
		break;

	default:
		/* Illegal component type. */
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/* Port add or del to execute it */
/**
 * TODO(Ogasawara) The name `action` should be revised to be more
 * appropriate one.
 */
static int
spp_update_port(enum spp_command_action action,
		const struct spp_port_index *port,
		enum spp_port_rxtx rxtx,
		const char *name,
		const struct spp_port_ability *ability)
{
	int ret = SPP_RET_NG;
	int ret_check = -1;
	int ret_del = -1;
	int component_id = 0;
	int cnt = 0;
	struct spp_component_info *comp_info = NULL;
	struct spp_port_info *port_info = NULL;
	int *num = NULL;
	struct spp_port_info **ports = NULL;
	struct spp_component_info *comp_info_base = NULL;
	int *change_component = NULL;

	component_id = spp_get_component_id(name);
	if (component_id < 0) {
		RTE_LOG(ERR, SPP_COMMAND_PROC, "Unknown component by port "
				"command. (component = %s)\n", name);
		return SPP_RET_NG;
	}
	spp_get_mng_data_addr(NULL, NULL,
			&comp_info_base, NULL, NULL, &change_component, NULL);
	comp_info = (comp_info_base + component_id);
	port_info = get_iface_info(port->iface_type, port->iface_no);
	if (rxtx == SPP_PORT_RXTX_RX) {
		num = &comp_info->num_rx_port;
		ports = comp_info->rx_ports;
	} else {
		num = &comp_info->num_tx_port;
		ports = comp_info->tx_ports;
	}

	switch (action) {
	case CMD_ACTION_ADD:
		/* Check if over the maximum num of ports of component. */
		if (check_port_count(comp_info->type, rxtx,
				comp_info->num_rx_port,
				comp_info->num_tx_port) != SPP_RET_OK)
			return SPP_RET_NG;

		ret_check = check_port_element(port_info, *num, ports);
		/* Check whether a port has been already registered. */
		if (ret_check >= SPP_RET_OK) {
			/* registered */
			if (ability->ope == SPP_PORT_ABILITY_OPE_ADD_VLANTAG) {
				while ((cnt < SPP_PORT_ABILITY_MAX) &&
					    (port_info->ability[cnt].ope !=
					    SPP_PORT_ABILITY_OPE_ADD_VLANTAG))
					cnt++;
				if (cnt >= SPP_PORT_ABILITY_MAX) {
					RTE_LOG(ERR, SPP_COMMAND_PROC,
						"update VLAN tag "
						"Non-registratio\n");
					return SPP_RET_NG;
				}
				memcpy(&port_info->ability[cnt], ability,
					sizeof(struct spp_port_ability));

				ret = SPP_RET_OK;
				break;
			}
			return SPP_RET_OK;
		}

		if (*num >= RTE_MAX_ETHPORTS) {
			RTE_LOG(ERR, SPP_COMMAND_PROC, "Cannot assign port "
					"over the maximum number.\n");
			return SPP_RET_NG;
		}

		if (ability->ope != SPP_PORT_ABILITY_OPE_NONE) {
			while ((cnt < SPP_PORT_ABILITY_MAX) &&
					(port_info->ability[cnt].ope !=
					SPP_PORT_ABILITY_OPE_NONE)) {
				cnt++;
			}
			if (cnt >= SPP_PORT_ABILITY_MAX) {
				RTE_LOG(ERR, SPP_COMMAND_PROC,
						"No space of port ability.\n");
				return SPP_RET_NG;
			}
			memcpy(&port_info->ability[cnt], ability,
					sizeof(struct spp_port_ability));
		}

		port_info->iface_type = port->iface_type;
		ports[*num] = port_info;
		(*num)++;

		ret = SPP_RET_OK;
		break;

	case CMD_ACTION_DEL:
		for (cnt = 0; cnt < SPP_PORT_ABILITY_MAX; cnt++) {
			if (port_info->ability[cnt].ope ==
					SPP_PORT_ABILITY_OPE_NONE)
				continue;

			if (port_info->ability[cnt].rxtx == rxtx)
				memset(&port_info->ability[cnt], 0x00,
					sizeof(struct spp_port_ability));
		}

		ret_del = get_del_port_element(port_info, *num, ports);
		if (ret_del == 0)
			(*num)--; /* If deleted, decrement number. */

		ret = SPP_RET_OK;
		break;

	default:
		return SPP_RET_NG;
	}

	*(change_component + component_id) = 1;
	return ret;
}

/* Flush command to execute it */
static int
spp_flush(void)
{
	int ret = SPP_RET_NG;
	struct cancel_backup_info *backup_info = NULL;

	spp_get_mng_data_addr(NULL, NULL, NULL,
				NULL, NULL, NULL, &backup_info);

	/* Initial setting of each interface. */
	ret = flush_port();
	if (ret < SPP_RET_OK)
		return ret;

	/* Flush of core index. */
	flush_core();

	/* Flush of component */
	ret = flush_component();

	backup_mng_info(backup_info);
	return ret;
}

/**
 * Iterate core information for number of available cores to
 * append response for status command.
 */
static int
spp_iterate_core_info(struct spp_iterate_core_params *params)
{
	int ret;
	int lcore_id, cnt;
	struct core_info *core = NULL;
	struct spp_component_info *comp_info_base = NULL;
	struct spp_component_info *comp_info = NULL;

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (spp_get_core_status(lcore_id) == SPP_CORE_UNUSE)
			continue;

		core = get_core_info(lcore_id);
		if (core->num == 0) {
			ret = (*params->element_proc)(
				params, lcore_id,
				"", TYPE_UNUSE_STR,
				0, NULL, 0, NULL);
			if (unlikely(ret != 0)) {
				RTE_LOG(ERR, SPP_COMMAND_PROC, "Cannot iterate"
						" core information. "
						"(core = %d, type = %d)\n",
						lcore_id, SPP_COMPONENT_UNUSE);
				return SPP_RET_NG;
			}
			continue;
		}

		for (cnt = 0; cnt < core->num; cnt++) {
			spp_get_mng_data_addr(NULL, NULL, &comp_info_base,
							NULL, NULL, NULL, NULL);
			comp_info = (comp_info_base + core->id[cnt]);
#ifdef SPP_VF_MODULE
			if (comp_info->type == SPP_COMPONENT_CLASSIFIER_MAC) {
				ret = spp_classifier_get_component_status(
						lcore_id,
						core->id[cnt],
						params);
			} else {
				ret = spp_forward_get_component_status(
						lcore_id,
						core->id[cnt],
						params);
			}
#endif /* SPP_VF_MODULE */
#ifdef SPP_MIRROR_MODULE
			ret = spp_mirror_get_component_status(
						lcore_id,
						core->id[cnt],
						params);
#endif /* SPP_MIRROR_MODULE */
			if (unlikely(ret != 0)) {
				RTE_LOG(ERR, SPP_COMMAND_PROC, "Cannot iterate"
						" core information. "
						"(core = %d, type = %d)\n",
						lcore_id, comp_info->type);
				return SPP_RET_NG;
			}
		}
	}

	return SPP_RET_OK;
}

/* Iterate classifier_table to create response to status command */
#ifdef SPP_VF_MODULE
static int
spp_iterate_classifier_table(
		struct spp_iterate_classifier_table_params *params)
{
	int ret;

	ret = spp_classifier_mac_iterate_table(params);
	if (unlikely(ret != 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Cannot iterate classifier_mac_table.\n");
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}
#endif /* SPP_VF_MODULE */

/* Get port number assigned by DPDK lib */
static int
spp_get_dpdk_port(enum port_type iface_type, int iface_no)
{
	struct iface_info *iface_info = NULL;

	spp_get_mng_data_addr(NULL, &iface_info,
				NULL, NULL, NULL, NULL, NULL);
	switch (iface_type) {
	case PHY:
		return iface_info->nic[iface_no].dpdk_port;
	case RING:
		return iface_info->ring[iface_no].dpdk_port;
	case VHOST:
		return iface_info->vhost[iface_no].dpdk_port;
	default:
		return SPP_RET_NG;
	}
}

/* append a comma for JSON format */
static int
append_json_comma(char **output)
{
	*output = spp_strbuf_append(*output, ",", strlen(","));
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's comma failed to add.\n");
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/**
 * Append JSON formatted tag and its value to given `output` val. For example,
 * `output` is `"core": 2` if the args of `name` is "core" and `value` is 2.
 */
static int
append_json_uint_value(const char *name, char **output, unsigned int value)
{
	int len = strlen(*output);
	/* extend the buffer */
	*output = spp_strbuf_append(*output, "",
			strlen(name) + CMD_TAG_APPEND_SIZE*2);
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's numeric format failed to add. "
				"(name = %s, uint = %u)\n", name, value);
		return SPP_RET_NG;
	}

	sprintf(&(*output)[len], JSON_APPEND_VALUE("%u"),
			JSON_APPEND_COMMA(len), name, value);
	return SPP_RET_OK;
}

/**
 * Append JSON formatted tag and its value to given `output` val. For example,
 * `output` is `"client-id": 1`
 * if the args of `name` is "client-id" and `value` is 1.
 */
static int
append_json_int_value(const char *name, char **output, int value)
{
	int len = strlen(*output);
	/* extend the buffer */
	*output = spp_strbuf_append(*output, "",
			strlen(name) + CMD_TAG_APPEND_SIZE*2);
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's numeric format failed to add. "
				"(name = %s, int = %d)\n", name, value);
		return SPP_RET_NG;
	}

	sprintf(&(*output)[len], JSON_APPEND_VALUE("%d"),
			JSON_APPEND_COMMA(len), name, value);
	return SPP_RET_OK;
}

/**
 * Append JSON formatted tag and its value to given `output` val. For example,
 * `output` is `"port": "phy:0"`
 *  if the args of `name` is "port" and  `str` is ”phy:0”.
 */
static int
append_json_str_value(const char *name, char **output, const char *str)
{
	int len = strlen(*output);
	/* extend the buffer */
	*output = spp_strbuf_append(*output, "",
			strlen(name) + strlen(str) + CMD_TAG_APPEND_SIZE);
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's string format failed to add. "
				"(name = %s, str = %s)\n", name, str);
		return SPP_RET_NG;
	}

	sprintf(&(*output)[len], JSON_APPEND_VALUE("\"%s\""),
			JSON_APPEND_COMMA(len), name, str);
	return SPP_RET_OK;
}

/**
 * Append JSON formatted tag and its value to given `output` val. For example,
 * `output` is `"results":[{"result":"success"}]`
 * if the args of `name` is "results" and `str` is "{"result":"success"}".
 */
static int
append_json_array_brackets(const char *name, char **output, const char *str)
{
	int len = strlen(*output);
	/* extend the buffer */
	*output = spp_strbuf_append(*output, "",
			strlen(name) + strlen(str) + CMD_TAG_APPEND_SIZE);
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's square bracket failed to add. "
				"(name = %s, str = %s)\n", name, str);
		return SPP_RET_NG;
	}

	sprintf(&(*output)[len], JSON_APPEND_ARRAY,
			JSON_APPEND_COMMA(len), name, str);
	return SPP_RET_OK;
}

/* append brackets of the blocks for JSON format */
static int
append_json_block_brackets(const char *name, char **output, const char *str)
{
	int len = strlen(*output);
	/* extend the buffer */
	*output = spp_strbuf_append(*output, "",
			strlen(name) + strlen(str) + CMD_TAG_APPEND_SIZE);
	if (unlikely(*output == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"JSON's curly bracket failed to add. "
				"(name = %s, str = %s)\n", name, str);
		return SPP_RET_NG;
	}

	if (name[0] == '\0')
		sprintf(&(*output)[len], JSON_APPEND_BLOCK_NONAME,
				JSON_APPEND_COMMA(len), name, str);
	else
		sprintf(&(*output)[len], JSON_APPEND_BLOCK,
				JSON_APPEND_COMMA(len), name, str);
	return SPP_RET_OK;
}

/* execute one command */
static int
execute_command(const struct spp_command *command)
{
	int ret = SPP_RET_OK;

	switch (command->type) {
	case CMD_CLASSIFIER_TABLE_MAC:
	case CMD_CLASSIFIER_TABLE_VLAN:
		if (command->type == CMD_CLASSIFIER_TABLE_MAC)
			RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute classifier_table command(MAC).\n");
		else
			RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute classifier_table command(VLAN).\n");
		ret = spp_update_classifier_table(
				command->spec.classifier_table.action,
				command->spec.classifier_table.type,
				command->spec.classifier_table.vid,
				command->spec.classifier_table.mac,
				&command->spec.classifier_table.port);
		if (ret == 0) {
			RTE_LOG(INFO, SPP_COMMAND_PROC,
					"Execute flush.\n");
			ret = spp_flush();
		}
		break;

	case CMD_COMPONENT:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute component command.\n");
		ret = spp_update_component(
				command->spec.component.action,
				command->spec.component.name,
				command->spec.component.core,
				command->spec.component.type);
		if (ret == 0) {
			RTE_LOG(INFO, SPP_COMMAND_PROC,
					"Execute flush.\n");
			ret = spp_flush();
		}
		break;

	case CMD_PORT:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute port command. (act = %d)\n",
				command->spec.port.action);
		ret = spp_update_port(
				command->spec.port.action,
				&command->spec.port.port,
				command->spec.port.rxtx,
				command->spec.port.name,
				&command->spec.port.ability);
		if (ret == 0) {
			RTE_LOG(INFO, SPP_COMMAND_PROC,
					"Execute flush.\n");
			ret = spp_flush();
		}
		break;

	case CMD_CLIENT_ID:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute get_client_id command.\n");
		break;

	case CMD_STATUS:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute status command.\n");
		break;

	case CMD_EXIT:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute exit command.\n");
		break;
	}

	return ret;
}

/* parse error message for response */
static const char *
parse_error_message(
		const struct spp_parse_command_error *parse_error,
		char *message)
{
	switch (parse_error->code) {
	case WRONG_FORMAT:
		sprintf(message, "wrong message format");
		break;

	case UNKNOWN_COMMAND:
		sprintf(message, "unknown command(%s)", parse_error->value);
		break;

	case NO_PARAM:
		sprintf(message, "not enough parameter(%s)",
				parse_error->value_name);
		break;

	case WRONG_TYPE:
		sprintf(message, "wrong value type(%s)",
				parse_error->value_name);
		break;

	case WRONG_VALUE:
		sprintf(message, "wrong value(%s)", parse_error->value_name);
		break;

	default:
		sprintf(message, "error occur");
		break;
	}

	return message;
}

/* set the command result */
static inline void
set_command_results(struct command_result *result,
		int code, const char *error_messege)
{
	result->code = code;
	switch (code) {
	case CMD_SUCCESS:
		strcpy(result->msg, "success");
		memset(result->error_message, 0x00, CMD_RES_ERR_MSG_SIZE);
		break;
	case CMD_FAILURE:
		strcpy(result->msg, "error");
		strcpy(result->error_message, error_messege);
		break;
	case CMD_INVALID: /* FALLTHROUGH */
	default:
		strcpy(result->msg, "invalid");
		memset(result->error_message, 0x00, CMD_RES_ERR_MSG_SIZE);
		break;
	}
}

/* set parse error to command result */
static void
set_parse_error_to_results(struct command_result *results,
		const struct spp_command_request *request,
		const struct spp_parse_command_error *parse_error)
{
	int i;
	const char *tmp_buff;
	char error_messege[CMD_RES_ERR_MSG_SIZE];

	for (i = 0; i < request->num_command; i++) {
		if (parse_error->code == 0)
			set_command_results(&results[i], CMD_SUCCESS, "");
		else
			set_command_results(&results[i], CMD_INVALID, "");
	}

	if (parse_error->code != 0) {
		tmp_buff = parse_error_message(parse_error,
				error_messege);
		set_command_results(&results[request->num_valid_command],
				CMD_FAILURE, tmp_buff);
	}
}

/* append a command result for JSON format */
static int
append_result_value(const char *name, char **output, void *tmp)
{
	const struct command_result *result = tmp;
	return append_json_str_value(name, output, result->msg);
}

/* append error details for JSON format */
static int
append_error_details_value(const char *name, char **output, void *tmp)
{
	int ret = SPP_RET_NG;
	const struct command_result *result = tmp;
	char *tmp_buff;
	/* string is empty, except for errors */
	if (result->error_message[0] == '\0')
		return SPP_RET_OK;

	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	ret = append_json_str_value("message", &tmp_buff,
			result->error_message);
	if (unlikely(ret < 0)) {
		spp_strbuf_free(tmp_buff);
		return SPP_RET_NG;
	}

	ret = append_json_block_brackets(name, output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* append a client id for JSON format */
static int
append_client_id_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	return append_json_int_value(name, output, spp_get_client_id());
}

/* append a list of interface numbers */
static int
append_interface_array(char **output, const enum port_type type)
{
	int i, port_cnt = 0;
	char tmp_str[CMD_TAG_APPEND_SIZE];

	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (!spp_check_flush_port(type, i))
			continue;

		sprintf(tmp_str, "%s%d", JSON_APPEND_COMMA(port_cnt), i);

		*output = spp_strbuf_append(*output, tmp_str, strlen(tmp_str));
		if (unlikely(*output == NULL)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Interface number failed to add. "
					"(type = %d)\n", type);
			return SPP_RET_NG;
		}

		port_cnt++;
	}

	return SPP_RET_OK;
}

/* append a secondary process type for JSON format */
static int
append_process_type_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	return append_json_str_value(name, output,
			SECONDARY_PROCESS_TYPE_SRINGS[spp_get_process_type()]);
}

/* append a list of interface numbers for JSON format */
static int
append_interface_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	if (strcmp(name, SPP_IFTYPE_NIC_STR) == 0)
		ret = append_interface_array(&tmp_buff, PHY);

	else if (strcmp(name, SPP_IFTYPE_VHOST_STR) == 0)
		ret = append_interface_array(&tmp_buff, VHOST);

	else if (strcmp(name, SPP_IFTYPE_RING_STR) == 0)
		ret = append_interface_array(&tmp_buff, RING);

	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(tmp_buff);
		return SPP_RET_NG;
	}

	ret = append_json_array_brackets(name, output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* append a value of vlan for JSON format */
static int
append_vlan_value(char **output, const int ope, const int vid, const int pcp)
{
	int ret = SPP_RET_OK;
	ret = append_json_str_value("operation", output,
			PORT_ABILITY_STATUS_STRINGS[ope]);
	if (unlikely(ret < SPP_RET_OK))
		return SPP_RET_NG;

	ret = append_json_int_value("id", output, vid);
	if (unlikely(ret < 0))
		return SPP_RET_NG;

	ret = append_json_int_value("pcp", output, pcp);
	if (unlikely(ret < 0))
		return SPP_RET_NG;

	return SPP_RET_OK;
}

/* append a block of vlan for JSON format */
static int
append_vlan_block(const char *name, char **output,
		const int port_id, const enum spp_port_rxtx rxtx)
{
	int ret = SPP_RET_NG;
	int i = 0;
	struct spp_port_ability *info = NULL;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	spp_port_ability_get_info(port_id, rxtx, &info);
	for (i = 0; i < SPP_PORT_ABILITY_MAX; i++) {
		switch (info[i].ope) {
		case SPP_PORT_ABILITY_OPE_ADD_VLANTAG:
		case SPP_PORT_ABILITY_OPE_DEL_VLANTAG:
			ret = append_vlan_value(&tmp_buff, info[i].ope,
					info[i].data.vlantag.vid,
					info[i].data.vlantag.pcp);
			if (unlikely(ret < SPP_RET_OK))
				return SPP_RET_NG;

			/*
			 * Change counter to "maximum+1" for exit the loop.
			 * An if statement after loop termination is false
			 * by "maximum+1 ".
			 */
			i = SPP_PORT_ABILITY_MAX + 1;
			break;
		default:
			/* not used */
			break;
		}
	}
	if (i == SPP_PORT_ABILITY_MAX) {
		ret = append_vlan_value(&tmp_buff, SPP_PORT_ABILITY_OPE_NONE,
				0, 0);
		if (unlikely(ret < SPP_RET_OK))
			return SPP_RET_NG;
	}

	ret = append_json_block_brackets(name, output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* append a block of port entry for JSON format */
static int
append_port_entry(char **output, const struct spp_port_index *port,
		const enum spp_port_rxtx rxtx)
{
	int ret = SPP_RET_NG;
	char port_str[CMD_TAG_APPEND_SIZE];
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = port_block)\n");
		return SPP_RET_NG;
	}

	spp_format_port_string(port_str, port->iface_type, port->iface_no);
	ret = append_json_str_value("port", &tmp_buff, port_str);
	if (unlikely(ret < SPP_RET_OK))
		return SPP_RET_NG;

	ret = append_vlan_block("vlan", &tmp_buff,
			spp_get_dpdk_port(port->iface_type, port->iface_no),
			rxtx);
	if (unlikely(ret < SPP_RET_OK))
		return SPP_RET_NG;

	ret = append_json_block_brackets("", output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* append a list of port numbers for JSON format */
static int
append_port_array(const char *name, char **output, const int num,
		const struct spp_port_index *ports,
		const enum spp_port_rxtx rxtx)
{
	int ret = SPP_RET_NG;
	int i = 0;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	for (i = 0; i < num; i++) {
		ret = append_port_entry(&tmp_buff, &ports[i], rxtx);
		if (unlikely(ret < SPP_RET_OK))
			return SPP_RET_NG;
	}

	ret = append_json_array_brackets(name, output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* append one element of core information for JSON format */
static int
append_core_element_value(
		struct spp_iterate_core_params *params,
		const unsigned int lcore_id,
		const char *name, const char *type,
		const int num_rx, const struct spp_port_index *rx_ports,
		const int num_tx, const struct spp_port_index *tx_ports)
{
	int ret = SPP_RET_NG;
	int unuse_flg = 0;
	char *buff, *tmp_buff;
	buff = params->output;
	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return ret;
	}

	/* there is unnecessary data when "unuse" by type */
	unuse_flg = strcmp(type, TYPE_UNUSE_STR);

	ret = append_json_uint_value("core", &tmp_buff, lcore_id);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	if (unuse_flg) {
		ret = append_json_str_value("name", &tmp_buff, name);
		if (unlikely(ret < 0))
			return ret;
	}

	ret = append_json_str_value("type", &tmp_buff, type);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	if (unuse_flg) {
		ret = append_port_array("rx_port", &tmp_buff,
				num_rx, rx_ports, SPP_PORT_RXTX_RX);
		if (unlikely(ret < 0))
			return ret;

		ret = append_port_array("tx_port", &tmp_buff,
				num_tx, tx_ports, SPP_PORT_RXTX_TX);
		if (unlikely(ret < SPP_RET_OK))
			return ret;
	}

	ret = append_json_block_brackets("", &buff, tmp_buff);
	spp_strbuf_free(tmp_buff);
	params->output = buff;
	return ret;
}

/* append a list of core information for JSON format */
static int
append_core_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	struct spp_iterate_core_params itr_params;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	itr_params.output = tmp_buff;
	itr_params.element_proc = append_core_element_value;

	ret = spp_iterate_core_info(&itr_params);
	if (unlikely(ret != SPP_RET_OK)) {
		spp_strbuf_free(itr_params.output);
		return SPP_RET_NG;
	}

	ret = append_json_array_brackets(name, output, itr_params.output);
	spp_strbuf_free(itr_params.output);
	return ret;
}

/* append one element of classifier table for JSON format */
#ifdef SPP_VF_MODULE
static int
append_classifier_element_value(
		struct spp_iterate_classifier_table_params *params,
		enum spp_classifier_type type,
		int vid, const char *mac,
		const struct spp_port_index *port)
{
	int ret = SPP_RET_NG;
	char *buff, *tmp_buff;
	char port_str[CMD_TAG_APPEND_SIZE];
	char value_str[SPP_MIN_STR_LEN];
	buff = params->output;
	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = classifier_table)\n");
		return ret;
	}

	spp_format_port_string(port_str, port->iface_type, port->iface_no);

	ret = append_json_str_value("type", &tmp_buff,
				    CLASSIFILER_TYPE_STATUS[type]);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	memset(value_str, 0x00, SPP_MIN_STR_LEN);
	switch (type) {
	case SPP_CLASSIFIER_TYPE_MAC:
		sprintf(value_str, "%s", mac);
		break;
	case SPP_CLASSIFIER_TYPE_VLAN:
		sprintf(value_str, "%d/%s", vid, mac);
		break;
	default:
		/* not used */
		break;
	}

	ret = append_json_str_value("value", &tmp_buff, value_str);
	if (unlikely(ret < 0))
		return ret;

	ret = append_json_str_value("port", &tmp_buff, port_str);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	ret = append_json_block_brackets("", &buff, tmp_buff);
	spp_strbuf_free(tmp_buff);
	params->output = buff;
	return ret;
}
#endif /* SPP_VF_MODULE */

/* append a list of classifier table for JSON format */
#ifdef SPP_VF_MODULE
static int
append_classifier_table_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	struct spp_iterate_classifier_table_params itr_params;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	itr_params.output = tmp_buff;
	itr_params.element_proc = append_classifier_element_value;

	ret = spp_iterate_classifier_table(&itr_params);
	if (unlikely(ret != SPP_RET_OK)) {
		spp_strbuf_free(itr_params.output);
		return SPP_RET_NG;
	}

	ret = append_json_array_brackets(name, output, itr_params.output);
	spp_strbuf_free(itr_params.output);
	return ret;
}
#endif /* SPP_VF_MODULE */
/* append string of command response list for JSON format */
static int
append_response_list_value(char **output,
		struct command_response_list *list,
		void *tmp)
{
	int ret = SPP_RET_NG;
	int i;
	char *tmp_buff;
	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = response_list)\n");
		return SPP_RET_NG;
	}

	for (i = 0; list[i].tag_name[0] != '\0'; i++) {
		tmp_buff[0] = '\0';
		ret = list[i].func(list[i].tag_name, &tmp_buff, tmp);
		if (unlikely(ret < SPP_RET_OK)) {
			spp_strbuf_free(tmp_buff);
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Failed to get reply string. "
					"(tag = %s)\n", list[i].tag_name);
			return SPP_RET_NG;
		}

		if (tmp_buff[0] == '\0')
			continue;

		if ((*output)[0] != '\0') {
			ret = append_json_comma(output);
			if (unlikely(ret < SPP_RET_OK)) {
				spp_strbuf_free(tmp_buff);
				RTE_LOG(ERR, SPP_COMMAND_PROC,
						"Failed to add commas. "
						"(tag = %s)\n",
						list[i].tag_name);
				return SPP_RET_NG;
			}
		}

		*output = spp_strbuf_append(*output, tmp_buff,
				strlen(tmp_buff));
		if (unlikely(*output == NULL)) {
			spp_strbuf_free(tmp_buff);
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Failed to add reply string. "
					"(tag = %s)\n",
					list[i].tag_name);
			return SPP_RET_NG;
		}
	}

	spp_strbuf_free(tmp_buff);
	return SPP_RET_OK;
}

/* termination constant of command response list */
#define COMMAND_RESP_TAG_LIST_EMPTY { "", NULL }

/* command response result string list */
struct command_response_list response_result_list[] = {
	{ "result",        append_result_value },
	{ "error_details", append_error_details_value },
	COMMAND_RESP_TAG_LIST_EMPTY
};

/* command response status information string list */
struct command_response_list response_info_list[] = {
	{ "client-id",        append_client_id_value },
	{ "phy",              append_interface_value },
	{ "vhost",            append_interface_value },
	{ "ring",             append_interface_value },
	{ "core",             append_core_value },
#ifdef SPP_VF_MODULE
	{ "classifier_table", append_classifier_table_value },
#endif /* SPP_VF_MODULE */
	COMMAND_RESP_TAG_LIST_EMPTY
};

/* append a list of command results for JSON format. */
static int
append_command_results_value(const char *name, char **output,
		int num, struct command_result *results)
{
	int ret = SPP_RET_NG;
	int i;
	char *tmp_buff1, *tmp_buff2;
	tmp_buff1 = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff1 == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s, buff=1)\n",
				name);
		return SPP_RET_NG;
	}

	tmp_buff2 = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff2 == NULL)) {
		spp_strbuf_free(tmp_buff1);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s, buff=2)\n",
				name);
		return SPP_RET_NG;
	}

	for (i = 0; i < num; i++) {
		tmp_buff1[0] = '\0';
		ret = append_response_list_value(&tmp_buff1,
				response_result_list, &results[i]);
		if (unlikely(ret < 0)) {
			spp_strbuf_free(tmp_buff1);
			spp_strbuf_free(tmp_buff2);
			return SPP_RET_NG;
		}

		ret = append_json_block_brackets("", &tmp_buff2, tmp_buff1);
		if (unlikely(ret < 0)) {
			spp_strbuf_free(tmp_buff1);
			spp_strbuf_free(tmp_buff2);
			return SPP_RET_NG;
		}

	}

	ret = append_json_array_brackets(name, output, tmp_buff2);
	spp_strbuf_free(tmp_buff1);
	spp_strbuf_free(tmp_buff2);
	return ret;
}

/* append a list of status information for JSON format. */
static int
append_info_value(const char *name, char **output)
{
	int ret = SPP_RET_NG;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = %s)\n",
				name);
		return SPP_RET_NG;
	}

	ret = append_response_list_value(&tmp_buff,
			response_info_list, NULL);
	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(tmp_buff);
		return SPP_RET_NG;
	}

	ret = append_json_block_brackets(name, output, tmp_buff);
	spp_strbuf_free(tmp_buff);
	return ret;
}

/* send response for parse error */
static void
send_parse_error_response(int *sock,
		const struct spp_command_request *request,
		struct command_result *command_results)
{
	int ret = SPP_RET_NG;
	char *msg, *tmp_buff;
	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC, "allocate error. "
				"(name = parse_error_response)\n");
		return;
	}

	/* create & append result array */
	ret = append_command_results_value("results", &tmp_buff,
			request->num_command, command_results);
	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(tmp_buff);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Failed to make command result response.\n");
		return;
	}

	msg = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(msg == NULL)) {
		spp_strbuf_free(tmp_buff);
		RTE_LOG(ERR, SPP_COMMAND_PROC, "allocate error. "
				"(name = parse_error_response)\n");
		return;
	}
	ret = append_json_block_brackets("", &msg, tmp_buff);
	spp_strbuf_free(tmp_buff);
	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(msg);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = result_response)\n");
		return;
	}

	RTE_LOG(DEBUG, SPP_COMMAND_PROC,
			"Make command response (parse error). "
			"response_str=\n%s\n", msg);

	/* send response to requester */
	ret = spp_send_message(sock, msg, strlen(msg));
	if (unlikely(ret != SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Failed to send parse error response.\n");
		/* not return */
	}

	spp_strbuf_free(msg);
}

/* send response for command execution result */
static void
send_command_result_response(int *sock,
		const struct spp_command_request *request,
		struct command_result *command_results)
{
	int ret = SPP_RET_NG;
	char *msg, *tmp_buff;
	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = result_response)\n");
		return;
	}

	/* create & append result array */
	ret = append_command_results_value("results", &tmp_buff,
			request->num_command, command_results);
	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(tmp_buff);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"Failed to make command result response.\n");
		return;
	}

	/* append client id information value */
	if (request->is_requested_client_id) {
		ret = append_client_id_value("client_id", &tmp_buff, NULL);
		if (unlikely(ret < SPP_RET_OK)) {
			spp_strbuf_free(tmp_buff);
			RTE_LOG(ERR, SPP_COMMAND_PROC, "Failed to make "
					"client id response.\n");
			return;
		}
		ret = append_process_type_value("process_type",
							&tmp_buff, NULL);
	}

	/* append info value */
	if (request->is_requested_status) {
		ret = append_info_value("info", &tmp_buff);
		if (unlikely(ret < SPP_RET_OK)) {
			spp_strbuf_free(tmp_buff);
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Failed to make status response.\n");
			return;
		}
	}

	msg = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(msg == NULL)) {
		spp_strbuf_free(tmp_buff);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = result_response)\n");
		return;
	}
	ret = append_json_block_brackets("", &msg, tmp_buff);
	spp_strbuf_free(tmp_buff);
	if (unlikely(ret < SPP_RET_OK)) {
		spp_strbuf_free(msg);
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (name = result_response)\n");
		return;
	}

	RTE_LOG(DEBUG, SPP_COMMAND_PROC,
			"Make command response (command result). "
			"response_str=\n%s\n", msg);

	/* send response to requester */
	ret = spp_send_message(sock, msg, strlen(msg));
	if (unlikely(ret != SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
			"Failed to send command result response.\n");
		/* not return */
	}

	spp_strbuf_free(msg);
}

/* process command request from no-null-terminated string */
static int
process_request(int *sock, const char *request_str, size_t request_str_len)
{
	int ret = SPP_RET_NG;
	int i;

	struct spp_command_request request;
	struct spp_parse_command_error parse_error;
	struct command_result command_results[CMD_MAX_COMMANDS];

	memset(&request, 0, sizeof(struct spp_command_request));
	memset(&parse_error, 0, sizeof(struct spp_parse_command_error));
	memset(command_results, 0, sizeof(command_results));

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "Start command request processing. "
			"request_str=\n%.*s\n",
			(int)request_str_len, request_str);

	/* parse request message */
	ret = spp_parse_command_request(
			&request, request_str, request_str_len, &parse_error);
	if (unlikely(ret != SPP_RET_OK)) {
		/* send error response */
		set_parse_error_to_results(command_results, &request,
				&parse_error);
		send_parse_error_response(sock, &request, command_results);
		RTE_LOG(DEBUG, SPP_COMMAND_PROC,
				"End command request processing.\n");
		return SPP_RET_OK;
	}

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "Command request is valid. "
			"num_command=%d, num_valid_command=%d\n",
			request.num_command, request.num_valid_command);

	/* execute commands */
	for (i = 0; i < request.num_command ; ++i) {
		ret = execute_command(request.commands + i);
		if (unlikely(ret != SPP_RET_OK)) {
			set_command_results(&command_results[i], CMD_FAILURE,
					"error occur");

			/* not execute remaining commands */
			for (++i; i < request.num_command ; ++i)
				set_command_results(&command_results[i],
					CMD_INVALID, "");

			break;
		}

		set_command_results(&command_results[i], CMD_SUCCESS, "");
	}

	if (request.is_requested_exit) {
		/* Terminated by process exit command.                       */
		/* Other route is normal end because it responds to command. */
		set_command_results(&command_results[0], CMD_SUCCESS, "");
		send_command_result_response(sock, &request, command_results);
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Terminate process for exit.\n");
		return SPP_RET_NG;
	}

	/* send response */
	send_command_result_response(sock, &request, command_results);

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "End command request processing.\n");

	return SPP_RET_OK;
}

/* initialize command processor. */
int
spp_command_proc_init(const char *controller_ip, int controller_port)
{
	return spp_command_conn_init(controller_ip, controller_port);
}

/* process command from controller. */
int
spp_command_proc_do(void)
{
	int ret = SPP_RET_NG;
	int msg_ret = -1;

	static int sock = -1;
	static char *msgbuf;

	if (unlikely(msgbuf == NULL)) {
		msgbuf = spp_strbuf_allocate(CMD_REQ_BUF_INIT_SIZE);
		if (unlikely(msgbuf == NULL)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Cannot allocate memory "
					"for receive data(init).\n");
			return SPP_RET_NG;
		}
	}

	ret = spp_connect_to_controller(&sock);

	if (unlikely(ret != SPP_RET_OK))
		return SPP_RET_OK;

	msg_ret = spp_receive_message(&sock, &msgbuf);
	if (unlikely(msg_ret <= 0)) {
		if (likely(msg_ret == 0))
			return SPP_RET_OK;
		else if (unlikely(msg_ret == SPP_CONNERR_TEMPORARY))
			return SPP_RET_OK;
		else
			return SPP_RET_NG;
	}

	ret = process_request(&sock, msgbuf, msg_ret);
	spp_strbuf_remove_front(msgbuf, msg_ret);

	return ret;
}
