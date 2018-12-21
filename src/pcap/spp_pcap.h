/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#ifndef __SPP_PCAP_H__
#define __SPP_PCAP_H__

#include "spp_proc.h"

/**
 * @file
 * SPP_PCAP main
 *
 * Main function of spp_pcap.
 * This provides the function for initializing and starting the threads.
 *
 */

/**
 * Pcap get core status
 *
 * @param lcore_id
 *  The logical core ID for forwarder and merger.
 * @param params
 *  The pointer to struct spp_iterate_core_params.@n
 *  Detailed data of pcap status.
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int spp_pcap_get_core_status(
		unsigned int lcore_id,
		struct spp_iterate_core_params *params);

#endif /* __SPP_PCAP_H__ */
