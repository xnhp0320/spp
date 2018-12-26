/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _SPP_PCAP_COMMAND_DEC_H_
#define _SPP_PCAP_COMMAND_DEC_H_

/**
 * @file
 * SPP pcap command parse
 *
 * Decode and validate the command message string.
 */

#include "spp_proc.h"

/** max number of command per request */
#define SPP_CMD_MAX_COMMANDS 32

/** maximum number of parameters per command */
#define SPP_CMD_MAX_PARAMETERS 8

/** command name string buffer size (include null char) */
#define SPP_CMD_NAME_BUFSZ  32

/** command value string buffer size (include null char) */
#define SPP_CMD_VALUE_BUFSZ 111

/** parse error code */
enum spp_command_parse_error_code {
	/* not use 0, in general 0 is OK */
	BAD_FORMAT = 1,  /**< Wrong format */
	UNKNOWN_COMMAND, /**< Unknown command */
	NO_PARAM,        /**< No parameters */
	BAD_TYPE,        /**< Wrong data type */
	BAD_VALUE,       /**< Wrong value */
};

/**
 * spp command type.
 *
 * @attention This enumerated type must have the same order of command_list
 *            defined in command_dec_pcap.c
 */
enum spp_command_type {
	/** get_client_id command */
	CMD_CLIENT_ID,

	/** status command */
	CMD_STATUS,

	/** exit command */
	CMD_EXIT,

	/** start command */
	CMD_START,

	/** stop command */
	CMD_STOP,

};

/** command parameters */
struct spp_command {
	enum spp_command_type type; /**< Command type */
};

/** request parameters */
struct spp_command_request {
	int num_command;                /**< Number of accepted commands */
	int num_valid_command;          /**< Number of executed commands */
	struct spp_command commands[SPP_CMD_MAX_COMMANDS];
					/**<Information of executed commands */

	int is_requested_client_id;     /**< Id for get_client_id command */
	int is_requested_status;        /**< Id for status command */
	int is_requested_exit;          /**< Id for exit command */
	int is_requested_start;         /**< Id for start command */
	int is_requested_stop;          /**< Id for stop command */
};

/** parse error information */
struct spp_command_parse_error {
	int code;                            /**< Error code */
	char value_name[SPP_CMD_NAME_BUFSZ]; /**< Error value name */
	char value[SPP_CMD_VALUE_BUFSZ];     /**< Error value */
};

/**
 * parse request from no-null-terminated string
 *
 * @param request
 *  The pointer to struct spp_command_request.@n
 *  The result value of decoding the command message.
 * @param request_str
 *  The pointer to requested command message.
 * @param request_str_len
 *  The length of requested command message.
 * @param error
 *  The pointer to struct spp_command_parse_error.@n
 *  Detailed error information will be stored.
 *
 * @retval SPP_RET_OK succeeded.
 * @retval !0 failed.
 */
int spp_command_parse_request(struct spp_command_request *request,
		const char *request_str, size_t request_str_len,
		struct spp_command_parse_error *error);

#endif /* _COMMAND_DEC_H_ */
