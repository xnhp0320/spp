/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <stdint.h>
#include "common.h"
#include "secondary.h"

#define RTE_LOGTYPE_SHARED RTE_LOGTYPE_USER1

/*
 * Get status of spp_nfv or spp_vm as JSON format. It consists of running
 * status and patch info of ports.
 *
 * Here is an example of well-formatted JSON status to better understand.
 * Actual status has no spaces and new lines inserted as
 * '{"status":"running","ports":[{"src":"phy:0","dst":"ring:0"},...]}'
 *
 *   {
 *     "status": "running",
 *     "ports": ["phy:0", "phy:1", "ring:0", "vhost:0"],
 *     "patches": [
 *       {"src":"phy:0","dst": "ring:0"},
 *       {"src":"ring:0","dst": "vhost:0"}
 *     ]
 *   }
 */
void
get_sec_stats_json(char *str, uint16_t client_id,
		const char *running_stat,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	sprintf(str, "{\"client-id\":%d,", client_id);

	sprintf(str + strlen(str), "\"status\":");
	sprintf(str + strlen(str), "\"%s\",", running_stat);

	append_port_info_json(str, ports_fwd_array, port_map);
	sprintf(str + strlen(str), ",");

	append_patch_info_json(str, ports_fwd_array, port_map);
	sprintf(str + strlen(str), "}");

	// make sure to be terminated with null character
	sprintf(str + strlen(str), "%c", '\0');
}


/*
 * Append patch info to sec status. It is called from get_sec_stats_json()
 * to add a JSON formatted patch info to given 'str'. Here is an example.
 *
 *     "ports": ["phy:0", "phy:1", "ring:0", "vhost:0"]
 */
int
append_port_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	unsigned int i;
	unsigned int has_port = 0;  // for checking having port at last

	sprintf(str + strlen(str), "\"ports\":[");
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {

		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		has_port = 1;
		switch (port_map[i].port_type) {
		case PHY:
			sprintf(str + strlen(str), "\"phy:%u\",",
					port_map[i].id);
			break;
		case RING:
			sprintf(str + strlen(str), "\"ring:%u\",",
				port_map[i].id);
			break;
		case VHOST:
			sprintf(str + strlen(str), "\"vhost:%u\",",
				port_map[i].id);
			break;
		case PCAP:
			sprintf(str + strlen(str), "\"pcap:%u\",",
					port_map[i].id);
			break;
		case NULLPMD:
			sprintf(str + strlen(str), "\"nullpmd:%u\",",
					port_map[i].id);
			break;
		case UNDEF:
			/* TODO(yasufum) Need to remove print for undefined ? */
			sprintf(str + strlen(str), "\"udf\",");
			break;
		}
	}

	// Check if it has at least one port to remove ",".
	if (has_port == 0) {
		sprintf(str + strlen(str), "]");
	} else {  // Remove last ','
		sprintf(str + strlen(str) - 1, "]");
	}

	return 0;
}

/*
 * Append patch info to sec status. It is called from get_sec_stats_json()
 * to add a JSON formatted patch info to given 'str'. Here is an example.
 *
 *     "patches": [
 *       {"src":"phy:0","dst": "ring:0"},
 *       {"src":"ring:0","dst": "vhost:0"}
 *      ]
 */
int
append_patch_info_json(char *str,
		struct port *ports_fwd_array,
		struct port_map *port_map)
{
	unsigned int i;
	unsigned int has_patch = 0;  // for checking having patch at last

	char patch_str[128];
	sprintf(str + strlen(str), "\"patches\":[");
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {

		if (ports_fwd_array[i].in_port_id == PORT_RESET)
			continue;

		RTE_LOG(INFO, SHARED, "Port ID %d\n", i);
		RTE_LOG(INFO, SHARED, "Status %d\n",
			ports_fwd_array[i].in_port_id);

		memset(patch_str, '\0', sizeof(patch_str));

		sprintf(patch_str, "{\"src\":");

		switch (port_map[i].port_type) {
		case PHY:
			RTE_LOG(INFO, SHARED, "Type: PHY\n");
			sprintf(patch_str + strlen(patch_str),
					"\"phy:%u\",",
					port_map[i].id);
			break;
		case RING:
			RTE_LOG(INFO, SHARED, "Type: RING\n");
			sprintf(patch_str + strlen(patch_str),
					"\"ring:%u\",",
					port_map[i].id);
			break;
		case VHOST:
			RTE_LOG(INFO, SHARED, "Type: VHOST\n");
			sprintf(patch_str + strlen(patch_str),
					"\"vhost:%u\",",
					port_map[i].id);
			break;
		case PCAP:
			RTE_LOG(INFO, SHARED, "Type: PCAP\n");
			sprintf(patch_str + strlen(patch_str),
					"\"pcap:%u\",",
					port_map[i].id);
			break;
		case NULLPMD:
			RTE_LOG(INFO, SHARED, "Type: NULLPMD\n");
			sprintf(patch_str + strlen(patch_str),
					"\"nullpmd:%u\",",
					port_map[i].id);
			break;
		case UNDEF:
			RTE_LOG(INFO, SHARED, "Type: UDF\n");
			/* TODO(yasufum) Need to remove print for undefined ? */
			sprintf(patch_str + strlen(patch_str),
					"\"udf\",");
			break;
		}

		sprintf(patch_str + strlen(patch_str), "\"dst\":");

		RTE_LOG(INFO, SHARED, "Out Port ID %d\n",
				ports_fwd_array[i].out_port_id);

		if (ports_fwd_array[i].out_port_id == PORT_RESET) {
			//sprintf(patch_str + strlen(patch_str), "%s", "\"\"");
			continue;
		} else {
			has_patch = 1;
			unsigned int j = ports_fwd_array[i].out_port_id;
			switch (port_map[j].port_type) {
			case PHY:
				RTE_LOG(INFO, SHARED, "Type: PHY\n");
				sprintf(patch_str + strlen(patch_str),
						"\"phy:%u\"",
						port_map[j].id);
				break;
			case RING:
				RTE_LOG(INFO, SHARED, "Type: RING\n");
				sprintf(patch_str + strlen(patch_str),
						"\"ring:%u\"",
						port_map[j].id);
				break;
			case VHOST:
				RTE_LOG(INFO, SHARED, "Type: VHOST\n");
				sprintf(patch_str + strlen(patch_str),
						"\"vhost:%u\"",
						port_map[j].id);
				break;
			case PCAP:
				RTE_LOG(INFO, SHARED, "Type: PCAP\n");
				sprintf(patch_str + strlen(patch_str),
						"\"pcap:%u\"",
						port_map[j].id);
				break;
			case NULLPMD:
				RTE_LOG(INFO, SHARED, "Type: NULLPMD\n");
				sprintf(patch_str + strlen(patch_str),
						"\"nullpmd:%u\"",
						port_map[j].id);
				break;
			case UNDEF:
				RTE_LOG(INFO, SHARED, "Type: UDF\n");
				/*
				 * TODO(yasufum) Need to remove print for
				 * undefined ?
				 */
				sprintf(patch_str + strlen(patch_str),
						"\"udf\"");
				break;
			}
		}

		sprintf(patch_str + strlen(patch_str), "},");

		if (has_patch != 0)
			sprintf(str + strlen(str), "%s", patch_str);
	}


	// Check if it has at least one patch to remove ",".
	if (has_patch == 0) {
		sprintf(str + strlen(str), "]");
	} else {  // Remove last ','
		sprintf(str + strlen(str) - 1, "]");
	}

	return 0;
}

/**
 * Retieve port type and ID from resource UID. For example, resource UID
 * 'ring:0' is  parsed to retrieve port tyep 'ring' and ID '0'.
 */
int
parse_resource_uid(char *str, char **port_type, int *port_id)
{
	char *token;
	char delim[] = ":";
	char *endp;

	RTE_LOG(DEBUG, SHARED, "Parsing resource UID: '%s\n'", str);
	if (strstr(str, delim) == NULL) {
		RTE_LOG(ERR, SHARED, "Invalid resource UID: '%s'\n", str);
		return -1;
	}
	RTE_LOG(DEBUG, SHARED, "Delimiter %s is included\n", delim);

	*port_type = strtok(str, delim);

	token = strtok(NULL, delim);
	*port_id = strtol(token, &endp, 10);

	if (*endp) {
		RTE_LOG(ERR, SHARED, "Bad integer value: %s\n", str);
		return -1;
	}

	return 0;
}

int
spp_atoi(const char *str, int *val)
{
	char *end;

	*val = strtol(str, &end, 10);

	if (*end) {
		RTE_LOG(ERR, SHARED, "Bad integer value: %s\n", str);
		return -1;
	}

	return 0;
}

/* attach the new device, then store port_id of the device */
int
dev_attach_by_devargs(const char *devargs, uint16_t *port_id)
{
	int ret = -1;
	struct rte_devargs da;

	memset(&da, 0, sizeof(da));

	/* parse devargs */
	if (rte_devargs_parse(&da, devargs))
		return -1;

	ret = rte_eal_hotplug_add(da.bus->name, da.name, da.args);
	if (ret < 0) {
		free(da.args);
		return ret;
	}

	ret = rte_eth_dev_get_port_by_name(da.name, port_id);

	free(da.args);

	return ret;
}

/* detach the device, then store the name of the device */
int
dev_detach_by_port_id(uint16_t port_id)
{
	struct rte_device *dev;
	struct rte_bus *bus;
	uint32_t dev_flags;
	int ret = -1;

	if (rte_eth_devices[port_id].data == NULL) {
		RTE_LOG(INFO, SHARED,
			"rte_eth_devices[%"PRIu16"].data is  NULL\n", port_id);
		return 0;
	}
	dev_flags = rte_eth_devices[port_id].data->dev_flags;
	if (dev_flags & RTE_ETH_DEV_BONDED_SLAVE) {
		RTE_LOG(ERR, SHARED,
			"Port %"PRIu16" is bonded, cannot detach\n", port_id);
		return -ENOTSUP;
	}

	dev = rte_eth_devices[port_id].device;
	if (dev == NULL)
		return -EINVAL;

	bus = rte_bus_find_by_device(dev);
	if (bus == NULL)
		return -ENOENT;

	ret = rte_eal_hotplug_remove(bus->name, dev->name);
	if (ret < 0)
		return ret;

	rte_eth_dev_release_port(&rte_eth_devices[port_id]);

	return 0;
}
