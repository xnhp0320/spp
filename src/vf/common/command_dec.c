/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_ether.h>
#include <rte_log.h>
#include <rte_branch_prediction.h>

#include "command_dec.h"

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_COMMAND_DEC RTE_LOGTYPE_USER2

/*
 * classifier type string list
 * do it same as the order of enum spp_classifier_type (spp_proc.h)
 */
const char *CLASSIFILER_TYPE_STRINGS[] = {
	"none",
	"mac",
	"vlan",

	/* termination */ "",
};

/*
 * command action type string list
 * do it same as the order of enum spp_command_action (command_dec.h)
 */
const char *COMMAND_ACTION_STRINGS[] = {
	"none",
	"start",
	"stop",
	"add",
	"del",

	/* termination */ "",
};

/*
 * port rxtx string list
 * do it same as the order of enum spp_port_rxtx (spp_vf.h)
 */
const char *PORT_RXTX_STRINGS[] = {
	"none",
	"rx",
	"tx",

	/* termination */ "",
};

/*
 * port ability string list
 * do it same as the order of enum spp_port_ability_type (spp_vf.h)
 */
const char *PORT_ABILITY_STRINGS[] = {
	"none",
	"add_vlantag",
	"del_vlantag",

	/* termination */ "",
};

/* Check mac address used on the port for registering or removing */
static int
spp_check_classid_used_port(
		int vid, uint64_t mac_addr,
		enum port_type iface_type, int iface_no)
{
	struct spp_port_info *port_info = get_iface_info(iface_type, iface_no);

	/**
	 * return true if given mac_addr/vid matches
	 *  with that of port_info/vid
	 */
	return ((mac_addr == port_info->class_id.mac_addr) &&
		(vid == port_info->class_id.vlantag.vid));
}

/* Check if port has been added. */
static int
spp_check_added_port(enum port_type iface_type, int iface_no)
{
	struct spp_port_info *port = get_iface_info(iface_type, iface_no);
	return port->iface_type != UNDEF;
}

/**
 * Separate port id of combination of iface type and number and
 * assign to given argument, iface_type and iface_no.
 *
 * For instance, 'ring:0' is separated to 'ring' and '0'.
 */
static int
spp_convert_port_to_iface(const char *port,
		    enum port_type *iface_type,
		    int *iface_no)
{
	enum port_type type = UNDEF;
	const char *no_str = NULL;
	char *endptr = NULL;

	/* Find out which type of interface from port */
	if (strncmp(port, SPP_IFTYPE_NIC_STR ":",
			strlen(SPP_IFTYPE_NIC_STR)+1) == 0) {
		/* NIC */
		type = PHY;
		no_str = &port[strlen(SPP_IFTYPE_NIC_STR)+1];
	} else if (strncmp(port, SPP_IFTYPE_VHOST_STR ":",
			strlen(SPP_IFTYPE_VHOST_STR)+1) == 0) {
		/* VHOST */
		type = VHOST;
		no_str = &port[strlen(SPP_IFTYPE_VHOST_STR)+1];
	} else if (strncmp(port, SPP_IFTYPE_RING_STR ":",
			strlen(SPP_IFTYPE_RING_STR)+1) == 0) {
		/* RING */
		type = RING;
		no_str = &port[strlen(SPP_IFTYPE_RING_STR)+1];
	} else {
		/* OTHER */
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown interface type. (port = %s)\n", port);
		return SPP_RET_NG;
	}

	/* Change type of number of interface */
	int ret_no = strtol(no_str, &endptr, 0);
	if (unlikely(no_str == endptr) || unlikely(*endptr != '\0')) {
		/* No IF number */
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"No interface number. (port = %s)\n", port);
		return SPP_RET_NG;
	}

	*iface_type = type;
	*iface_no = ret_no;

	RTE_LOG(DEBUG, SPP_COMMAND_DEC, "Port = %s => Type = %d No = %d\n",
			port, *iface_type, *iface_no);
	return SPP_RET_OK;
}

/* Convert component name to component type */
static enum spp_component_type
spp_convert_component_type(const char *type_str)
{
	RTE_LOG(DEBUG, SPP_COMMAND_DEC, "type_str is %s\n", type_str);
#ifdef SPP_VF_MODULE
	if (strncmp(type_str, SPP_TYPE_CLASSIFIER_MAC_STR,
			strlen(SPP_TYPE_CLASSIFIER_MAC_STR)+1) == 0) {
		/* Classifier */
		return SPP_COMPONENT_CLASSIFIER_MAC;
	} else if (strncmp(type_str, SPP_TYPE_MERGE_STR,
			strlen(SPP_TYPE_MERGE_STR)+1) == 0) {
		/* Merger */
		return SPP_COMPONENT_MERGE;
	} else if (strncmp(type_str, SPP_TYPE_FORWARD_STR,
			strlen(SPP_TYPE_FORWARD_STR)+1) == 0) {
		/* Forwarder */
		return SPP_COMPONENT_FORWARD;
	}
#endif /* SPP_VF_MODULE */
#ifdef SPP_MIRROR_MODULE
	if (strncmp(type_str, SPP_TYPE_MIRROR_STR,
			strlen(SPP_TYPE_MIRROR_STR)+1) == 0)
		/* Mirror */
		return SPP_COMPONENT_MIRROR;
#endif /* SPP_MIRROR_MODULE */
	return SPP_COMPONENT_UNUSE;
}

/* set decode error */
static inline int
set_decode_error(struct spp_command_decode_error *error,
		const int error_code, const char *error_name)
{
	error->code = error_code;

	if (likely(error_name != NULL))
		strcpy(error->value_name, error_name);

	return error->code;
}

/* set decode error */
static inline int
set_string_value_decode_error(struct spp_command_decode_error *error,
		const char *value, const char *error_name)
{
	strcpy(error->value, value);
	return set_decode_error(error, SPP_CMD_DERR_BAD_VALUE, error_name);
}

/* Split command line parameter with spaces */
static int
decode_parameter_value(char *string, int max, int *argc, char *argv[])
{
	int cnt = 0;
	const char *delim = " ";
	char *argv_tok = NULL;
	char *saveptr = NULL;

	argv_tok = strtok_r(string, delim, &saveptr);
	while (argv_tok != NULL) {
		if (cnt >= max)
			return SPP_RET_NG;
		argv[cnt] = argv_tok;
		cnt++;
		argv_tok = strtok_r(NULL, delim, &saveptr);
	}
	*argc = cnt;

	return SPP_RET_OK;
}

/* Get index of array */
static int
get_arrary_index(const char *match, const char *list[])
{
	int i;
	for (i = 0; list[i][0] != '\0'; i++) {
		if (strcmp(list[i], match) == 0)
			return i;
	}
	return SPP_RET_NG;
}

/* Get int type value */
static int
get_int_value(
		int *output,
		const char *arg_val,
		int min,
		int max)
{
	int ret = 0;
	char *endptr = NULL;
	ret = strtol(arg_val, &endptr, 0);
	if (unlikely(endptr == arg_val) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	if (unlikely(ret < min) || unlikely(ret > max))
		return SPP_RET_NG;

	*output = ret;
	return SPP_RET_OK;
}

/* Get unsigned int type value */
static int
get_uint_value(
		unsigned int *output,
		const char *arg_val,
		unsigned int min,
		unsigned int max)
{
	unsigned int ret = SPP_RET_OK;
	char *endptr = NULL;
	ret = strtoul(arg_val, &endptr, 0);
	if (unlikely(endptr == arg_val) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	if (unlikely(ret < min) || unlikely(ret > max))
		return SPP_RET_NG;

	*output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of string */
static int
decode_str_value(char *output, const char *arg_val)
{
	if (strlen(arg_val) >= SPP_CMD_VALUE_BUFSZ)
		return SPP_RET_NG;

	strcpy(output, arg_val);
	return SPP_RET_OK;
}

/* decoding procedure of port */
static int
decode_port_value(void *output, const char *arg_val)
{
	int ret = SPP_RET_OK;
	struct spp_port_index *port = output;
	ret = spp_convert_port_to_iface(arg_val, &port->iface_type,
							&port->iface_no);
	if (unlikely(ret != 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Bad port. val=%s\n", arg_val);
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/* decoding procedure of core */
static int
decode_core_value(void *output, const char *arg_val)
{
	int ret = SPP_RET_OK;
	ret = get_uint_value(output, arg_val, 0, RTE_MAX_LCORE-1);
	if (unlikely(ret < 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Bad core id. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/* decoding procedure of action for component command */
static int
decode_component_action_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	ret = get_arrary_index(arg_val, COMMAND_ACTION_STRINGS);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown component action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (unlikely(ret != SPP_CMD_ACTION_START) &&
			unlikely(ret != SPP_CMD_ACTION_STOP)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown component action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	*(int *)output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of action for component command */
static int
decode_component_name_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_component *component = output;

	/* "stop" has no core ID parameter. */
	if (component->action == SPP_CMD_ACTION_START) {
		ret = spp_get_component_id(arg_val);
		if (unlikely(ret >= 0)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
					"Component name in used. val=%s\n",
					arg_val);
			return SPP_RET_NG;
		}
	}

	return decode_str_value(component->name, arg_val);
}

/* decoding procedure of core id for component command */
static int
decode_component_core_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	struct spp_command_component *component = output;

	/* "stop" has no core ID parameter. */
	if (component->action != SPP_CMD_ACTION_START)
		return SPP_RET_OK;

	return decode_core_value(&component->core, arg_val);
}

/* decoding procedure of type for component command */
static int
decode_component_type_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	enum spp_component_type comp_type;
	struct spp_command_component *component = output;

	/* "stop" has no type parameter. */
	if (component->action != SPP_CMD_ACTION_START)
		return SPP_RET_OK;

	comp_type = spp_convert_component_type(arg_val);
	if (unlikely(comp_type <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown component type. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	component->type = comp_type;
	return SPP_RET_OK;
}

/* decoding procedure of action for port command */
static int
decode_port_action_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	ret = get_arrary_index(arg_val, COMMAND_ACTION_STRINGS);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (unlikely(ret != SPP_CMD_ACTION_ADD) &&
			unlikely(ret != SPP_CMD_ACTION_DEL)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	*(int *)output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of port for port command */
static int
decode_port_port_value(void *output, const char *arg_val, int allow_override)
{
	int ret = SPP_RET_NG;
	struct spp_port_index tmp_port;
	struct spp_command_port *port = output;

	ret = decode_port_value(&tmp_port, arg_val);
	if (ret < SPP_RET_OK)
		return SPP_RET_NG;

	/* add vlantag command check */
	if (allow_override == 0) {
		if ((port->action == SPP_CMD_ACTION_ADD) &&
				(spp_check_used_port(tmp_port.iface_type,
						tmp_port.iface_no,
						SPP_PORT_RXTX_RX) >= 0) &&
				(spp_check_used_port(tmp_port.iface_type,
						tmp_port.iface_no,
						SPP_PORT_RXTX_TX) >= 0)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Port in used. (port command) val=%s\n",
				arg_val);
			return SPP_RET_NG;
		}
	}

	port->port.iface_type = tmp_port.iface_type;
	port->port.iface_no   = tmp_port.iface_no;
	return SPP_RET_OK;
}

/* decoding procedure of rxtx type for port command */
static int
decode_port_rxtx_value(void *output, const char *arg_val, int allow_override)
{
	int ret = SPP_RET_OK;
	struct spp_command_port *port = output;

	ret = get_arrary_index(arg_val, PORT_RXTX_STRINGS);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Unknown port rxtx. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	/* add vlantag command check */
	if (allow_override == 0) {
		if ((port->action == SPP_CMD_ACTION_ADD) &&
				(spp_check_used_port(port->port.iface_type,
					port->port.iface_no, ret) >= 0)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Port in used. (port command) val=%s\n",
				arg_val);
			return SPP_RET_NG;
		}
	}

	port->rxtx = ret;
	return SPP_RET_OK;
}

/* decoding procedure of component name for port command */
static int
decode_port_name_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;

	ret = spp_get_component_id(arg_val);
	if (unlikely(ret < SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown component name. val=%s\n", arg_val);
		return SPP_RET_NG;
	}

	return decode_str_value(output, arg_val);
}

/* decoding procedure of vlan operation for port command */
static int
decode_port_vlan_operation(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_port *port = output;
	struct spp_port_ability *ability = &port->ability;

	switch (ability->ope) {
	case SPP_PORT_ABILITY_OPE_NONE:
		ret = get_arrary_index(arg_val, PORT_ABILITY_STRINGS);
		if (unlikely(ret <= 0)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
					"Unknown port ability. val=%s\n",
					arg_val);
			return SPP_RET_NG;
		}
		ability->ope  = ret;
		ability->rxtx = port->rxtx;
		break;
	case SPP_PORT_ABILITY_OPE_ADD_VLANTAG:
		/* Nothing to do. */
		break;
	default:
		/* Not used. */
		break;
	}

	return SPP_RET_OK;
}

/* decoding procedure of vid  for port command */
static int
decode_port_vid(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_port *port = output;
	struct spp_port_ability *ability = &port->ability;

	switch (ability->ope) {
	case SPP_PORT_ABILITY_OPE_ADD_VLANTAG:
		ret = get_int_value(&ability->data.vlantag.vid,
			arg_val, 0, ETH_VLAN_ID_MAX);
		if (unlikely(ret < SPP_RET_OK)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
					"Bad VLAN ID. val=%s\n", arg_val);
			return SPP_RET_NG;
		}
		ability->data.vlantag.pcp = -1;
		break;
	default:
		/* Not used. */
		break;
	}

	return SPP_RET_OK;
}

/* decoding procedure of pcp for port command */
static int
decode_port_pcp(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_port *port = output;
	struct spp_port_ability *ability = &port->ability;

	switch (ability->ope) {
	case SPP_PORT_ABILITY_OPE_ADD_VLANTAG:
		ret = get_int_value(&ability->data.vlantag.pcp,
				arg_val, 0, SPP_VLAN_PCP_MAX);
		if (unlikely(ret < SPP_RET_OK)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
					"Bad VLAN PCP. val=%s\n", arg_val);
			return SPP_RET_NG;
		}
		break;
	default:
		/* Not used. */
		break;
	}

	return SPP_RET_OK;
}

/* decoding procedure of mac address string */
static int
decode_mac_addr_str_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int64_t ret = SPP_RET_OK;
	const char *str_val = arg_val;

	/* if default specification, convert to internal dummy address */
	if (unlikely(strcmp(str_val, SPP_DEFAULT_CLASSIFIED_SPEC_STR) == 0))
		str_val = SPP_DEFAULT_CLASSIFIED_DMY_ADDR_STR;

	ret = spp_change_mac_str_to_int64(str_val);
	if (unlikely(ret < SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Bad mac address string. val=%s\n", str_val);
		return SPP_RET_NG;
	}

	strcpy((char *)output, str_val);
	return SPP_RET_OK;
}

/* decoding procedure of action for classifier_table command */
static int
decode_classifier_action_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	ret = get_arrary_index(arg_val, COMMAND_ACTION_STRINGS);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (unlikely(ret != SPP_CMD_ACTION_ADD) &&
			unlikely(ret != SPP_CMD_ACTION_DEL)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	*(int *)output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of type for classifier_table command */
static int
decode_classifier_type_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	ret = get_arrary_index(arg_val, CLASSIFILER_TYPE_STRINGS);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Unknown classifier type. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	*(int *)output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of vlan id for classifier_table command */
static int
decode_classifier_vid_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	ret = get_int_value(output, arg_val, 0, ETH_VLAN_ID_MAX);
	if (unlikely(ret < SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Bad VLAN ID. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}
	return SPP_RET_OK;
}

/* decoding procedure of port for classifier_table command */
static int
decode_classifier_port_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_classifier_table *classifier_table = output;
	struct spp_port_index tmp_port;
	int64_t mac_addr = 0;

	ret = decode_port_value(&tmp_port, arg_val);
	if (ret < SPP_RET_OK)
		return SPP_RET_NG;

	if (spp_check_added_port(tmp_port.iface_type,
					tmp_port.iface_no) == 0) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Port not added. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (classifier_table->type == SPP_CLASSIFIER_TYPE_MAC)
		classifier_table->vid = ETH_VLAN_ID_MAX;

	if (unlikely(classifier_table->action == SPP_CMD_ACTION_ADD)) {
		if (!spp_check_classid_used_port(ETH_VLAN_ID_MAX, 0,
				tmp_port.iface_type, tmp_port.iface_no)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC, "Port in used. "
					"(classifier_table command) val=%s\n",
					arg_val);
			return SPP_RET_NG;
		}
	} else if (unlikely(classifier_table->action == SPP_CMD_ACTION_DEL)) {
		mac_addr = spp_change_mac_str_to_int64(classifier_table->mac);
		if (mac_addr < 0)
			return SPP_RET_NG;

		if (!spp_check_classid_used_port(classifier_table->vid,
				(uint64_t)mac_addr,
				tmp_port.iface_type, tmp_port.iface_no)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC, "Port in used. "
					"(classifier_table command) val=%s\n",
					arg_val);
			return SPP_RET_NG;
		}
	}

	classifier_table->port.iface_type = tmp_port.iface_type;
	classifier_table->port.iface_no   = tmp_port.iface_no;
	return SPP_RET_OK;
}

#define DECODE_PARAMETER_LIST_EMPTY { NULL, 0, NULL }

/* parameter list for decoding */
struct decode_parameter_list {
	const char *name;       /* Parameter name */
	size_t offset;          /* Offset value of struct spp_command */
	int (*func)(void *output, const char *arg_val, int allow_override);
				/* Pointer to parameter handling function */
};

/* parameter list for each command */
static struct decode_parameter_list
parameter_list[][SPP_CMD_MAX_PARAMETERS] = {
	{                                /* classifier_table(mac) */
		{
			.name = "action",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.action),
			.func = decode_classifier_action_value
		},
		{
			.name = "type",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.type),
			.func = decode_classifier_type_value
		},
		{
			.name = "mac address",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.mac),
			.func = decode_mac_addr_str_value
		},
		{
			.name = "port",
			.offset = offsetof(struct spp_command,
					spec.classifier_table),
			.func = decode_classifier_port_value
		},
		DECODE_PARAMETER_LIST_EMPTY,
	},
	{                                /* classifier_table(VLAN) */
		{
			.name = "action",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.action),
			.func = decode_classifier_action_value
		},
		{
			.name = "type",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.type),
			.func = decode_classifier_type_value
		},
		{
			.name = "vlan id",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.vid),
			.func = decode_classifier_vid_value
		},
		{
			.name = "mac address",
			.offset = offsetof(struct spp_command,
					spec.classifier_table.mac),
			.func = decode_mac_addr_str_value
		},
		{
			.name = "port",
			.offset = offsetof(struct spp_command,
					spec.classifier_table),
			.func = decode_classifier_port_value
		},
		DECODE_PARAMETER_LIST_EMPTY,
	},
	{ DECODE_PARAMETER_LIST_EMPTY }, /* _get_client_id   */
	{ DECODE_PARAMETER_LIST_EMPTY }, /* status           */
	{ DECODE_PARAMETER_LIST_EMPTY }, /* exit             */
	{                                /* component        */
		{
			.name = "action",
			.offset = offsetof(struct spp_command,
					spec.component.action),
			.func = decode_component_action_value
		},
		{
			.name = "component name",
			.offset = offsetof(struct spp_command, spec.component),
			.func = decode_component_name_value
		},
		{
			.name = "core",
			.offset = offsetof(struct spp_command, spec.component),
			.func = decode_component_core_value
		},
		{
			.name = "component type",
			.offset = offsetof(struct spp_command, spec.component),
			.func = decode_component_type_value
		},
		DECODE_PARAMETER_LIST_EMPTY,
	},
	{                                /* port             */
		{
			.name = "action",
			.offset = offsetof(struct spp_command,
					spec.port.action),
			.func = decode_port_action_value
		},
		{
			.name = "port",
			.offset = offsetof(struct spp_command, spec.port),
			.func = decode_port_port_value
		},
		{
			.name = "port rxtx",
			.offset = offsetof(struct spp_command, spec.port),
			.func = decode_port_rxtx_value
		},
		{
			.name = "component name",
			.offset = offsetof(struct spp_command, spec.port.name),
			.func = decode_port_name_value
		},
		{
			.name = "port vlan operation",
			.offset = offsetof(struct spp_command, spec.port),
			.func = decode_port_vlan_operation
		},
		{
			.name = "port vid",
			.offset = offsetof(struct spp_command, spec.port),
			.func = decode_port_vid
		},
		{
			.name = "port pcp",
			.offset = offsetof(struct spp_command, spec.port),
			.func = decode_port_pcp
		},
		DECODE_PARAMETER_LIST_EMPTY,
	},
	{ DECODE_PARAMETER_LIST_EMPTY }, /* termination      */
};

/* check by list for each command line parameter component */
static int
decode_command_parameter_component(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_command_decode_error *error,
				int maxargc __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	int ci = request->commands[0].type;
	int pi = 0;
	struct decode_parameter_list *list = NULL;
	for (pi = 1; pi < argc; pi++) {
		list = &parameter_list[ci][pi-1];
		ret = (*list->func)((void *)
				((char *)&request->commands[0]+list->offset),
				argv[pi], 0);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC,
					"Bad value. command=%s, name=%s, "
					"index=%d, value=%s\n",
					argv[0], list->name, pi, argv[pi]);
			return set_string_value_decode_error(error, argv[pi],
					list->name);
		}
	}
	return SPP_RET_OK;
}

/* check by list for each command line parameter clssfier_table */
static int
decode_command_parameter_cls_table(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_command_decode_error *error,
				int maxargc)
{
	return decode_command_parameter_component(request,
						argc,
						argv,
						error,
						maxargc);
}
/* check by list for each command line parameter clssfier_table(vlan) */
static int
decode_command_parameter_cls_table_vlan(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_command_decode_error *error,
				int maxargc __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	int ci = request->commands[0].type;
	int pi = 0;
	struct decode_parameter_list *list = NULL;
	for (pi = 1; pi < argc; pi++) {
		list = &parameter_list[ci][pi-1];
		ret = (*list->func)((void *)
				((char *)&request->commands[0]+list->offset),
				argv[pi], 0);
		if (unlikely(ret < SPP_RET_OK)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC, "Bad value. "
				"command=%s, name=%s, index=%d, value=%s\n",
					argv[0], list->name, pi, argv[pi]);
			return set_string_value_decode_error(error, argv[pi],
				list->name);
		}
	}
	return SPP_RET_OK;
}

/* check by list for each command line parameter port */
static int
decode_command_parameter_port(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_command_decode_error *error,
				int maxargc)
{
	int ret = SPP_RET_OK;
	int ci = request->commands[0].type;
	int pi = 0;
	struct decode_parameter_list *list = NULL;
	int flag = 0;

	/* check add vlatag */
	if (argc == maxargc)
		flag = 1;

	for (pi = 1; pi < argc; pi++) {
		list = &parameter_list[ci][pi-1];
		ret = (*list->func)((void *)
				((char *)&request->commands[0]+list->offset),
				argv[pi], flag);
		if (unlikely(ret < SPP_RET_OK)) {
			RTE_LOG(ERR, SPP_COMMAND_DEC, "Bad value. "
				"command=%s, name=%s, index=%d, value=%s\n",
					argv[0], list->name, pi, argv[pi]);
			return set_string_value_decode_error(error, argv[pi],
				list->name);
		}
	}
	return SPP_RET_OK;
}

/* command list for decoding */
struct decode_command_list {
	const char *name;       /* Command name */
	int   param_min;        /* Min number of parameters */
	int   param_max;        /* Max number of parameters */
	int (*func)(struct spp_command_request *request, int argc,
			char *argv[], struct spp_command_decode_error *error,
			int maxargc);
				/* Pointer to command handling function */
};

/* command list */
static struct decode_command_list command_list[] = {
	/* classifier_table(mac) */
	{ "classifier_table", 5, 5, decode_command_parameter_cls_table },
	/* classifier_table(vlan) */
	{ "classifier_table", 6, 6, decode_command_parameter_cls_table_vlan },
	{ "_get_client_id",   1, 1, NULL },
	{ "status",           1, 1, NULL },
	{ "exit",             1, 1, NULL },
	{ "component",        3, 5, decode_command_parameter_component },
	{ "port",             5, 8, decode_command_parameter_port },
	/* termination     */
	{ "",                 0, 0, NULL }
};

/* Decode command line parameters */
static int
decode_command_in_list(struct spp_command_request *request,
			const char *request_str,
			struct spp_command_decode_error *error)
{
	int ret = SPP_RET_OK;
	int command_name_check = 0;
	struct decode_command_list *list = NULL;
	int i = 0;
	int argc = 0;
	char *argv[SPP_CMD_MAX_PARAMETERS];
	char tmp_str[SPP_CMD_MAX_PARAMETERS*SPP_CMD_VALUE_BUFSZ];
	memset(argv, 0x00, sizeof(argv));
	memset(tmp_str, 0x00, sizeof(tmp_str));

	strcpy(tmp_str, request_str);
	ret = decode_parameter_value(tmp_str, SPP_CMD_MAX_PARAMETERS,
			&argc, argv);
	if (ret < SPP_RET_OK) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Parameter number over limit."
				"request_str=%s\n", request_str);
		return set_decode_error(error, SPP_CMD_DERR_BAD_FORMAT, NULL);
	}
	RTE_LOG(DEBUG, SPP_COMMAND_DEC, "Decode array. num=%d\n", argc);

	for (i = 0; command_list[i].name[0] != '\0'; i++) {
		list = &command_list[i];
		if (strcmp(argv[0], list->name) != 0)
			continue;

		if (unlikely(argc < list->param_min) ||
				unlikely(list->param_max < argc)) {
			command_name_check = 1;
			continue;
		}

		request->commands[0].type = i;
		if (list->func != NULL)
			return (*list->func)(request, argc, argv, error,
							list->param_max);

		return SPP_RET_OK;
	}

	if (command_name_check != 0) {
		RTE_LOG(ERR, SPP_COMMAND_DEC, "Parameter number out of range."
				"request_str=%s\n", request_str);
		return set_decode_error(error, SPP_CMD_DERR_BAD_FORMAT, NULL);
	}

	RTE_LOG(ERR, SPP_COMMAND_DEC,
			"Unknown command. command=%s, request_str=%s\n",
			argv[0], request_str);
	return set_string_value_decode_error(error, argv[0], "command");
}

/* decode request from no-null-terminated string */
int
spp_command_decode_request(
		struct spp_command_request *request,
		const char *request_str, size_t request_str_len,
		struct spp_command_decode_error *error)
{
	int ret = SPP_RET_NG;
	int i;

	/* decode request */
	request->num_command = 1;
	ret = decode_command_in_list(request, request_str, error);
	if (unlikely(ret != SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_DEC,
				"Cannot decode command request. "
				"ret=%d, request_str=%.*s\n",
				ret, (int)request_str_len, request_str);
		return ret;
	}
	request->num_valid_command = 1;

	/* check getter command */
	for (i = 0; i < request->num_valid_command; ++i) {
		switch (request->commands[i].type) {
		case SPP_CMDTYPE_CLIENT_ID:
			request->is_requested_client_id = 1;
			break;
		case SPP_CMDTYPE_STATUS:
			request->is_requested_status = 1;
			break;
		case SPP_CMDTYPE_EXIT:
			request->is_requested_exit = 1;
			break;
		default:
			/* nothing to do */
			break;
		}
	}

	return ret;
}
