/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef _SPP_COMMAND_PARSE_H_
#define _SPP_COMMAND_PARSE_H_

/**
 * @file
 * SPP command parse
 *
 * Parse and validate the command message string.
 */

#include "spp_proc.h"

/** max number of command per request */
#define CMD_MAX_COMMANDS 32

/** maximum number of parameters per command */
#define CMD_MAX_PARAMETERS 8

/** command name string buffer size (include null char) */
#define CMD_NAME_BUFSZ  32

/** command value string buffer size (include null char) */
#define CMD_VALUE_BUFSZ 128

/** parse error code */
enum spp_parse_command_error_code {
	/* not use 0, in general 0 is OK */
	WRONG_FORMAT = 1, /**< Wrong format */
	UNKNOWN_COMMAND,  /**< Unknown command */
	NO_PARAM,         /**< No parameters */
	WRONG_TYPE,       /**< Wrong data type */
	WRONG_VALUE,      /**< Wrong value */
};

/**
 * Define actions of each of components
 *  The Run option of the folllwing commands.
 *   compomnent       : start,stop
 *   port             : add,del
 *   classifier_table : add,del
 */
enum spp_command_action {
	CMD_ACTION_NONE,  /**< none */
	CMD_ACTION_START, /**< start */
	CMD_ACTION_STOP,  /**< stop */
	CMD_ACTION_ADD,   /**< add */
	CMD_ACTION_DEL,   /**< delete */
};

/**
 * spp command type.
 *
 * @attention This enumerated type must have the same order of command_list
 *            defined in command_dec.c
 */
enum spp_command_type {
	/** get_client_id command */
	CMD_CLIENT_ID,

	/** status command */
	CMD_STATUS,

	/** exit command */
	CMD_EXIT,

	/** component command */
	CMD_COMPONENT,

	/** port command */
	CMD_PORT,
};

/* "flush" command specific parameters */
struct spp_command_flush {
	/* nothing specific */
};

/** "component" command parameters */
struct spp_command_component {
	/** Action identifier (start or stop) */
	enum spp_command_action action;

	/** Component name */
	char name[CMD_NAME_BUFSZ];

	/** Logical core number */
	unsigned int core;

	/** Component type */
	enum spp_component_type type;
};

/** "port" command parameters */
struct spp_command_port {
	/** Action identifier (add or del) */
	enum spp_command_action action;

	/** Port type and number */
	struct spp_port_index port;

	/** rx/tx identifier */
	enum spp_port_rxtx rxtx;

	/** Attached component name */
	char name[CMD_NAME_BUFSZ];
};

/** command parameters */
struct spp_command {
	enum spp_command_type type; /**< Command type */

	union {
		/** Structured data for flush command  */
		struct spp_command_flush flush;

		/** Structured data for component command  */
		struct spp_command_component component;

		/** Structured data for port command  */
		struct spp_command_port port;
	} spec;
};

/** request parameters */
struct spp_command_request {
	int num_command;                /**< Number of accepted commands */
	int num_valid_command;          /**< Number of executed commands */
	struct spp_command commands[CMD_MAX_COMMANDS];
					/**<Information of executed commands */

	int is_requested_client_id;     /**< Id for get_client_id command */
	int is_requested_status;        /**< Id for status command */
	int is_requested_exit;          /**< Id for exit command */
};

/** parse error information */
struct spp_parse_command_error {
	int code;                            /**< Error code */
	char value_name[CMD_NAME_BUFSZ]; /**< Error value name */
	char value[CMD_VALUE_BUFSZ];     /**< Error value */
};

/**
 * decode request from no-null-terminated string
 *
 * @param request
 *  The pointer to struct spp_command_request.@n
 *  The result value of decoding the command message.
 * @param request_str
 *  The pointer to requested command message.
 * @param request_str_len
 *  The length of requested command message.
 * @param error
 *  The pointer to struct spp_parse_command_error.@n
 *  Detailed error information will be stored.
 *
 * @retval SPP_RET_OK succeeded.
 * @retval !0 failed.
 */
int spp_parse_command_request(struct spp_command_request *request,
		const char *request_str, size_t request_str_len,
		struct spp_parse_command_error *error);

#endif /* _SPP_COMMAND_PARSE_H_ */
