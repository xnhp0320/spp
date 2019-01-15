/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#ifndef SHARED_SECONDARY_H
#define SHARED_SECONDARY_H

#define VHOST_IFACE_NAME "/tmp/sock%u"
#define VHOST_BACKEND_NAME "eth_vhost%u"

#define PCAP_PMD_DEV_NAME "eth_pcap%u"
#define NULL_PMD_DEV_NAME "eth_null%u"

static inline const char *
get_vhost_backend_name(unsigned int id)
{
	/*
	 * buffer for return value. Size calculated by %u being replaced
	 * by maximum 3 digits (plus an extra byte for safety)
	 */
	static char buffer[sizeof(VHOST_BACKEND_NAME) + 2];

	snprintf(buffer, sizeof(buffer) - 1, VHOST_BACKEND_NAME, id);
	return buffer;
}

static inline char *
get_vhost_iface_name(unsigned int id)
{
	/*
	 * buffer for return value. Size calculated by %u being replaced
	 * by maximum 3 digits (plus an extra byte for safety)
	 */
	static char buffer[sizeof(VHOST_IFACE_NAME) + 2];

	snprintf(buffer, sizeof(buffer) - 1, VHOST_IFACE_NAME, id);
	return buffer;
}

static inline const char *
get_pcap_pmd_name(int id)
{
	static char buffer[sizeof(PCAP_PMD_DEV_NAME) + 2];
	snprintf(buffer, sizeof(buffer) - 1, PCAP_PMD_DEV_NAME, id);
	return buffer;
}

static inline const char *
get_null_pmd_name(int id)
{
	static char buffer[sizeof(NULL_PMD_DEV_NAME) + 2];
	snprintf(buffer, sizeof(buffer) - 1, NULL_PMD_DEV_NAME, id);
	return buffer;
}

/* Get status of spp_nfv or spp_vm as JSON format. */
void get_sec_stats_json(char *str, uint16_t client_id,
		const char *running_stat,
		struct port *ports_fwd_array,
		struct port_map *port_map);

/* Append port info to sec status, called from get_sec_stats_json(). */
int append_port_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map);

/* Append patch info to sec status, called from get_sec_stats_json(). */
int append_patch_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map);

int parse_resource_uid(char *str, char **port_type, int *port_id);
int spp_atoi(const char *str, int *val);

/**
 * Attach a new Ethernet device specified by arguments.
 *
 * @param devargs
 *  A pointer to a strings array describing the new device
 *  to be attached. The strings should be a pci address like
 *  '0000:01:00.0' or virtual device name like 'net_pcap0'.
 * @param port_id
 *  A pointer to a port identifier actually attached.
 * @return
 *  0 on success and port_id is filled, negative on error
 */
int
dev_attach_by_devargs(const char *devargs, uint16_t *port_id);

/**
 * Detach a Ethernet device specified by port identifier.
 * This function must be called when the device is in the
 * closed state.
 *
 * @param port_id
 *   The port identifier of the device to detach.
 * @return
 *  0 on success and devname is filled, negative on error
 */
int dev_detach_by_port_id(uint16_t port_id);

#endif
