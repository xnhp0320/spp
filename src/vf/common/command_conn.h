/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2018 Nippon Telegraph and Telephone Corporation
 */

#ifndef _COMMAND_CONN_H_
#define _COMMAND_CONN_H_

/**
 * @file
 * SPP Connection
 *
 * Command connection management.
 */

/** result code - temporary error. please retry */
#define SPP_CONNERR_TEMPORARY -1
/** result code - fatal error occurred. should terminate process. */
#define SPP_CONNERR_FATAL     -2

/**
 * initialize command connection.
 *
 * @param controller_ip
 *  The controller's ip address.
 * @param controller_port
 *  The controller's port number.
 *
 * @retval SPP_RET_OK  succeeded.
 * @retval SPP_RET_NG failed.
 */
int spp_command_conn_init(const char *controller_ip, int controller_port);

/**
 * connect to controller.
 *
 * @note bocking.
 *
 * @param sock
 *  Socket number for connecting to controller.
 *
 * @retval SPP_RET_OK		 succeeded.
 * @retval SPP_CONNERR_TEMPORARY temporary error. please retry.
 */
int spp_connect_to_controller(int *sock);

/**
 * receive message.
 *
 * @note non-blocking.
 *
 * @param sock
 *  The socket number for the connection.
 * @param msgbuf
 *  The pointer to command message buffer.
 *
 * @retval 0 <			 succeeded. number of bytes received.
 * @retval SPP_RET_OK		 no receive message.
 * @retval SPP_CONNERR_TEMPORARY temporary error. please reconnect.
 * @retval SPP_CONNERR_FATAL	fatal error occurred. should terminate process.
 */
int spp_receive_message(int *sock, char **msgbuf);

/**
 * send message.
 *
 * @note non-blocking.
 *
 * @param sock
 *  The socket number to be sent.
 * @param message
 *  The pointer to the message to be sent.
 * @param message_len
 *  The length of message.
 *
 * @retval SPP_RET_OK		 succeeded.
 * @retval SPP_CONNERR_TEMPORARY temporary error. please reconnect.
 */
int spp_send_message(int *sock, const char *message, size_t message_len);

#endif /* _COMMAND_CONN_H_ */
