/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <string.h>

#include <rte_log.h>

#include "string_buffer.h"
#include "spp_pcap.h"
#include "command_conn.h"
#include "command_dec.h"
#include "command_proc.h"

#define RTE_LOGTYPE_SPP_COMMAND_PROC RTE_LOGTYPE_USER2

/* request message initial size */
#define CMD_RES_ERR_MSG_SIZE  128
#define CMD_TAG_APPEND_SIZE   16
#define CMD_REQ_BUF_INIT_SIZE 2048
#define CMD_RES_BUF_INIT_SIZE 2048

#define COMMAND_RESP_LIST_EMPTY { "", NULL }

#define JSON_APPEND_COMMA(flg)    ((flg)?", ":"")
#define JSON_APPEND_VALUE(format) "%s\"%s\": "format
#define JSON_APPEND_ARRAY         "%s\"%s\": [ %s ]"
#define JSON_APPEND_BLOCK         "%s\"%s\": { %s }"
#define JSON_APPEND_BLOCK_NONAME  "%s%s{ %s }"

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
	char msg[SPP_CMD_NAME_BUFSZ];

	/* Detailed response message */
	char error_message[CMD_RES_ERR_MSG_SIZE];
};

/* command response list control structure */
struct command_response_list {
	/* JSON Tag name */
	char tag_name[SPP_CMD_NAME_BUFSZ];

	/* Pointer to handling function */
	int (*func)(const char *name, char **output, void *tmp);
};

/* caputure status string list */
const char *CAPTURE_STATUS_STRINGS[] = {
	"idle",
	"running",
	"", /* termination */
};

/* get client id */
static int
spp_get_client_id(void)
{
	struct startup_param *startup_param;

	spp_get_mng_data_addr(&startup_param, NULL, NULL, NULL, NULL);
	return startup_param->client_id;
}

/**
 * Iterate core information for number of available cores to
 * append response for status command.
 */
static int
spp_iterate_core_info(struct spp_iterate_core_params *params)
{
	int ret;
	int lcore_id;

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (spp_get_core_status(lcore_id) == SPP_CORE_UNUSE)
			continue;

		ret = spp_pcap_get_core_status(lcore_id, params);
		if (unlikely(ret != 0)) {
			RTE_LOG(ERR, SPP_COMMAND_PROC,
					"Cannot iterate core information. "
						"(core = %d)\n", lcore_id);
			return SPP_RET_NG;
		}
	}

	return SPP_RET_OK;
}

/* append a comma for JSON format */
static int
append_json_comma(char **output)
{
	*output = spp_strbuf_append(*output, ", ", strlen(", "));
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
 * `output` is `"results": [ { "result": "success" } ]`
 * if the args of `name` is "results" and `str` is "{ "result": "success" }".
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

	case CMD_START:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute start command.\n");
		break;

	case CMD_STOP:
		RTE_LOG(INFO, SPP_COMMAND_PROC,
				"Execute stop command.\n");
		break;
	}

	return ret;
}

/* parse error message for response */
static const char *
parse_error_message(
		const struct spp_command_parse_error *parse_error,
		char *message)
{
	switch (parse_error->code) {
	case BAD_FORMAT:
		sprintf(message, "bad message format");
		break;

	case UNKNOWN_COMMAND:
		sprintf(message, "unknown command(%s)", parse_error->value);
		break;

	case NO_PARAM:
		sprintf(message, "not enough parameter(%s)",
				parse_error->value_name);
		break;

	case BAD_TYPE:
		sprintf(message, "bad value type(%s)",
				parse_error->value_name);
		break;

	case BAD_VALUE:
		sprintf(message, "bad value(%s)", parse_error->value_name);
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
		const struct spp_command_parse_error *parse_error)
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

/* append a capture status for JSON format */
static int
append_capture_status_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	int *capture_status = NULL;

	spp_get_mng_data_addr(NULL, NULL, NULL, NULL, &capture_status);

	return append_json_str_value(name, output,
			CAPTURE_STATUS_STRINGS[*capture_status]);
}

/* append a client id for JSON format */
static int
append_client_id_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	return append_json_int_value(name, output, spp_get_client_id());
}

/* append a block of port entry for JSON format */
static int
append_port_entry(char **output, const struct spp_port_index *port,
		const enum spp_port_rxtx rxtx __attribute__ ((unused)))
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

/* append a secondary process type for JSON format */
static int
append_process_type_value(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	return append_json_str_value(name, output, "pcap");
}

static int
append_pcap_core_element_value(
		struct spp_iterate_core_params *params,
		const unsigned int lcore_id,
		const char *name, const char *type,
		const int num_rx,
		const struct spp_port_index *rx_ports,
		const int num_tx __attribute__ ((unused)),
		const struct spp_port_index *tx_ports __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	int unuse_flg = 0;
	char *buff, *tmp_buff;
	buff = params->output;

	/* there is not necessary data when "unuse" by type */
	unuse_flg = strcmp(type, "unuse");
	if (!unuse_flg)
		return SPP_RET_OK;

	tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, SPP_COMMAND_PROC,
				"allocate error. (lcore_id = %d, type = %s)\n",
				lcore_id, type);
		return ret;
	}

	ret = append_json_uint_value("core", &tmp_buff, lcore_id);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	ret = append_json_str_value("role", &tmp_buff, type);
	if (unlikely(ret < SPP_RET_OK))
		return ret;

	if (num_rx != 0)
		ret = append_port_array("rx_port", &tmp_buff,
				num_rx, rx_ports, SPP_PORT_RXTX_RX);
	else
		ret = append_json_str_value("filename", &tmp_buff, name);
	if (unlikely(ret < 0))
		return ret;

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
	itr_params.element_proc = append_pcap_core_element_value;

	ret = spp_iterate_core_info(&itr_params);
	if (unlikely(ret != SPP_RET_OK)) {
		spp_strbuf_free(itr_params.output);
		return SPP_RET_NG;
	}

	ret = append_json_array_brackets(name, output, itr_params.output);
	spp_strbuf_free(itr_params.output);
	return ret;
}

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
	{ "status",           append_capture_status_value },
	{ "core",             append_core_value },
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
	int *capture_request = NULL;

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

	/* pcap start command */
	if (request->is_requested_start) {
		spp_get_mng_data_addr(NULL, NULL, NULL,
				      &capture_request, NULL);
		*capture_request = SPP_CAPTURE_RUNNING;
	}

	/* pcap stop command */
	if (request->is_requested_stop) {
		spp_get_mng_data_addr(NULL, NULL, NULL,
					&capture_request, NULL);
		*capture_request = SPP_CAPTURE_IDLE;
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
	struct spp_command_parse_error parse_error;
	struct command_result command_results[SPP_CMD_MAX_COMMANDS];

	memset(&request, 0, sizeof(struct spp_command_request));
	memset(&parse_error, 0, sizeof(struct spp_command_parse_error));
	memset(command_results, 0, sizeof(command_results));

	RTE_LOG(DEBUG, SPP_COMMAND_PROC, "Start command request processing. "
			"request_str=\n%.*s\n",
			(int)request_str_len, request_str);

	/* parse request message */
	ret = spp_command_parse_request(
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
