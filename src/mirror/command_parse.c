/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_ether.h>
#include <rte_log.h>
#include <rte_branch_prediction.h>

#include "command_parse.h"

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_COMMAND_PARSE RTE_LOGTYPE_USER2

/*
 * command action type string list
 * do it same as the order of enum spp_command_action (command_dec.h)
 */
const char *COMMAND_ACTION[] = {
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
const char *PORT_RXTX_STR[] = {
	"none",
	"rx",
	"tx",

	/* termination */ "",
};

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
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Unknown interface type. (port = %s)\n", port);
		return SPP_RET_NG;
	}

	/* Change type of number of interface */
	int ret_no = strtol(no_str, &endptr, 0);
	if (unlikely(no_str == endptr) || unlikely(*endptr != '\0')) {
		/* No IF number */
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"No interface number. (port = %s)\n", port);
		return SPP_RET_NG;
	}

	*iface_type = type;
	*iface_no = ret_no;

	RTE_LOG(DEBUG, SPP_COMMAND_PARSE, "Port = %s => Type = %d No = %d\n",
			port, *iface_type, *iface_no);
	return SPP_RET_OK;
}

/* Convert component name to component type */
static enum spp_component_type
spp_convert_component_type(const char *type_str)
{
	RTE_LOG(DEBUG, SPP_COMMAND_PARSE, "type_str is %s\n", type_str);
	if (strncmp(type_str, TYPE_MIRROR_STR,
			strlen(TYPE_MIRROR_STR)+1) == 0)
		return SPP_COMPONENT_MIRROR;
	return SPP_COMPONENT_UNUSE;
}

/* set parse error */
static inline int
set_parse_error(struct spp_parse_command_error *error,
		const int error_code, const char *error_name)
{
	error->code = error_code;

	if (likely(error_name != NULL))
		strcpy(error->value_name, error_name);

	return error->code;
}

/* set parse error */
static inline int
set_string_value_parse_error(struct spp_parse_command_error *error,
		const char *value, const char *error_name)
{
	strcpy(error->value, value);
	return set_parse_error(error, WRONG_VALUE, error_name);
}

/* Split command line parameter with spaces */
static int
parse_parameter_value(char *string, int max, int *argc, char *argv[])
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

/* parse procedure of string */
static int
parse_str_value(char *output, const char *arg_val)
{
	if (strlen(arg_val) >= CMD_VALUE_BUFSZ)
		return SPP_RET_NG;

	strcpy(output, arg_val);
	return SPP_RET_OK;
}

/* parse procedure of port */
static int
parse_port_value(void *output, const char *arg_val)
{
	int ret = SPP_RET_OK;
	struct spp_port_index *port = output;
	ret = spp_convert_port_to_iface(arg_val, &port->iface_type,
							&port->iface_no);
	if (unlikely(ret != 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE, "Bad port. val=%s\n", arg_val);
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
		RTE_LOG(ERR, SPP_COMMAND_PARSE, "Bad core id. val=%s\n",
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
	ret = get_arrary_index(arg_val, COMMAND_ACTION);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Unknown component action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (unlikely(ret != CMD_ACTION_START) &&
			unlikely(ret != CMD_ACTION_STOP)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
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
	if (component->action == CMD_ACTION_START) {
		ret = spp_get_component_id(arg_val);
		if (unlikely(ret >= 0)) {
			RTE_LOG(ERR, SPP_COMMAND_PARSE,
					"Component name in used. val=%s\n",
					arg_val);
			return SPP_RET_NG;
		}
	}

	return parse_str_value(component->name, arg_val);
}

/* decoding procedure of core id for component command */
static int
decode_component_core_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	struct spp_command_component *component = output;

	/* "stop" has no core ID parameter. */
	if (component->action != CMD_ACTION_START)
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
	if (component->action != CMD_ACTION_START)
		return SPP_RET_OK;

	comp_type = spp_convert_component_type(arg_val);
	if (unlikely(comp_type <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
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
	ret = get_arrary_index(arg_val, COMMAND_ACTION);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if (unlikely(ret != CMD_ACTION_ADD) &&
			unlikely(ret != CMD_ACTION_DEL)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Unknown port action. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	*(int *)output = ret;
	return SPP_RET_OK;
}

/* decoding procedure of port for port command */
static int
decode_port_port_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	struct spp_port_index tmp_port;
	struct spp_command_port *port = output;

	ret = parse_port_value(&tmp_port, arg_val);
	if (ret < SPP_RET_OK)
		return SPP_RET_NG;

	if ((port->action == CMD_ACTION_ADD) &&
			(spp_check_used_port(tmp_port.iface_type,
					tmp_port.iface_no,
					SPP_PORT_RXTX_RX) >= 0) &&
			(spp_check_used_port(tmp_port.iface_type,
					tmp_port.iface_no,
					SPP_PORT_RXTX_TX) >= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
			"Port in used. (port command) val=%s\n",
			arg_val);
		return SPP_RET_NG;
	}

	port->port.iface_type = tmp_port.iface_type;
	port->port.iface_no   = tmp_port.iface_no;
	return SPP_RET_OK;
}

/* decoding procedure of rxtx type for port command */
static int
decode_port_rxtx_value(void *output, const char *arg_val,
				int allow_override __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	struct spp_command_port *port = output;

	ret = get_arrary_index(arg_val, PORT_RXTX_STR);
	if (unlikely(ret <= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE, "Unknown port rxtx. val=%s\n",
				arg_val);
		return SPP_RET_NG;
	}

	if ((port->action == CMD_ACTION_ADD) &&
			(spp_check_used_port(port->port.iface_type,
				port->port.iface_no, ret) >= 0)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
			"Port in used. (port command) val=%s\n",
			arg_val);
		return SPP_RET_NG;
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
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Unknown component name. val=%s\n", arg_val);
		return SPP_RET_NG;
	}

	return parse_str_value(output, arg_val);
}

#define PARSE_PARAMETER_LIST_EMPTY { NULL, 0, NULL }

/* parameter list for decoding */
struct parse_parameter_list {
	const char *name;       /* Parameter name */
	size_t offset;          /* Offset value of struct spp_command */
	int (*func)(void *output, const char *arg_val, int allow_override);
				/* Pointer to parameter handling function */
};

/* parameter list for each command */
static struct parse_parameter_list
parameter_list[][CMD_MAX_PARAMETERS] = {
	{ PARSE_PARAMETER_LIST_EMPTY }, /* _get_client_id   */
	{ PARSE_PARAMETER_LIST_EMPTY }, /* status           */
	{ PARSE_PARAMETER_LIST_EMPTY }, /* exit             */
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
		PARSE_PARAMETER_LIST_EMPTY,
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
		PARSE_PARAMETER_LIST_EMPTY,
	},
	{ PARSE_PARAMETER_LIST_EMPTY }, /* termination      */
};

/* check by list for each command line parameter component */
static int
parse_command_parameter_component(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_parse_command_error *error,
				int maxargc __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	int ci = request->commands[0].type;
	int pi = 0;
	struct parse_parameter_list *list = NULL;
	for (pi = 1; pi < argc; pi++) {
		list = &parameter_list[ci][pi-1];
		ret = (*list->func)((void *)
				((char *)&request->commands[0]+list->offset),
				argv[pi], 0);
		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, SPP_COMMAND_PARSE,
					"Bad value. command=%s, name=%s, "
					"index=%d, value=%s\n",
					argv[0], list->name, pi, argv[pi]);
			return set_string_value_parse_error(error, argv[pi],
					list->name);
		}
	}
	return SPP_RET_OK;
}

/* check by list for each command line parameter port */
static int
parse_command_parameter_port(struct spp_command_request *request,
				int argc, char *argv[],
				struct spp_parse_command_error *error,
				int maxargc __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	int ci = request->commands[0].type;
	int pi = 0;
	struct parse_parameter_list *list = NULL;
	int flag = 0;

	for (pi = 1; pi < argc; pi++) {
		list = &parameter_list[ci][pi-1];
		ret = (*list->func)((void *)
				((char *)&request->commands[0]+list->offset),
				argv[pi], flag);
		if (unlikely(ret < SPP_RET_OK)) {
			RTE_LOG(ERR, SPP_COMMAND_PARSE, "Bad value. "
				"command=%s, name=%s, index=%d, value=%s\n",
					argv[0], list->name, pi, argv[pi]);
			return set_string_value_parse_error(error, argv[pi],
				list->name);
		}
	}
	return SPP_RET_OK;
}

/* command list for parse */
struct parse_command_list {
	const char *name;       /* Command name */
	int   param_min;        /* Min number of parameters */
	int   param_max;        /* Max number of parameters */
	int (*func)(struct spp_command_request *request, int argc,
			char *argv[], struct spp_parse_command_error *error,
			int maxargc);
				/* Pointer to command handling function */
};

/* command list */
static struct parse_command_list command_list[] = {
	{ "_get_client_id",   1, 1, NULL },
	{ "status",           1, 1, NULL },
	{ "exit",             1, 1, NULL },
	{ "component",        3, 5, parse_command_parameter_component },
	{ "port",             5, 8, parse_command_parameter_port },
	/* termination     */
	{ "",                 0, 0, NULL }
};

/* Parse command line parameters */
static int
parse_command_in_list(struct spp_command_request *request,
			const char *request_str,
			struct spp_parse_command_error *error)
{
	int ret = SPP_RET_OK;
	int command_name_check = 0;
	struct parse_command_list *list = NULL;
	int i = 0;
	int argc = 0;
	char *argv[CMD_MAX_PARAMETERS];
	char tmp_str[CMD_MAX_PARAMETERS*CMD_VALUE_BUFSZ];
	memset(argv, 0x00, sizeof(argv));
	memset(tmp_str, 0x00, sizeof(tmp_str));

	strcpy(tmp_str, request_str);
	ret = parse_parameter_value(tmp_str, CMD_MAX_PARAMETERS, &argc, argv);
	if (ret < SPP_RET_OK) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE, "Parameter number over limit."
				"request_str=%s\n", request_str);
		return set_parse_error(error, WRONG_FORMAT, NULL);
	}
	RTE_LOG(DEBUG, SPP_COMMAND_PARSE, "Parse array. num=%d\n", argc);

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
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Parameter number out of range. "
				"request_str=%s\n", request_str);
		return set_parse_error(error, WRONG_FORMAT, NULL);
	}

	RTE_LOG(ERR, SPP_COMMAND_PARSE,
			"Unknown command. command=%s, request_str=%s\n",
			argv[0], request_str);
	return set_string_value_parse_error(error, argv[0], "command");
}

/* parse request from no-null-terminated string */
int
spp_parse_command_request(
		struct spp_command_request *request,
		const char *request_str, size_t request_str_len,
		struct spp_parse_command_error *error)
{
	int ret = SPP_RET_NG;
	int i;

	/* parse request */
	request->num_command = 1;
	ret = parse_command_in_list(request, request_str, error);
	if (unlikely(ret != SPP_RET_OK)) {
		RTE_LOG(ERR, SPP_COMMAND_PARSE,
				"Cannot parse command request. "
				"ret=%d, request_str=%.*s\n",
				ret, (int)request_str_len, request_str);
		return ret;
	}
	request->num_valid_command = 1;

	/* check getter command */
	for (i = 0; i < request->num_valid_command; ++i) {
		switch (request->commands[i].type) {
		case CMD_CLIENT_ID:
			request->is_requested_client_id = 1;
			break;
		case CMD_STATUS:
			request->is_requested_status = 1;
			break;
		case CMD_EXIT:
			request->is_requested_exit = 1;
			break;
		default:
			/* nothing to do */
			break;
		}
	}

	return ret;
}
