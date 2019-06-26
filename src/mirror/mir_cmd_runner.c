/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include "spp_mirror.h"
#include "shared/secondary/return_codes.h"
#include "shared/secondary/string_buffer.h"
#include "shared/secondary/json_helper.h"
#include "shared/secondary/spp_worker_th/cmd_parser.h"
#include "shared/secondary/spp_worker_th/cmd_runner.h"
#include "shared/secondary/spp_worker_th/cmd_res_formatter.h"
#include "shared/secondary/spp_worker_th/mirror_deps.h"

#define RTE_LOGTYPE_MIR_CMD_RUNNER RTE_LOGTYPE_USER1

/* Assign worker thread or remove on specified lcore. */
/* TODO(yasufum) revise func name for removing the term `component`. */
static int
update_comp(enum sppwk_action wk_action, const char *name,
		unsigned int lcore_id, enum sppwk_worker_type wk_type)
{
	int ret;
	int ret_del;
	int comp_lcore_id = 0;
	unsigned int tmp_lcore_id = 0;
	struct sppwk_comp_info *comp_info = NULL;
	/* TODO(yasufum) revise `core` to be more specific. */
	struct core_info *core = NULL;
	struct core_mng_info *info = NULL;
	struct sppwk_comp_info *comp_info_base = NULL;
	/* TODO(yasufum) revise `core_info` which is same as struct name. */
	struct core_mng_info *core_info = NULL;
	int *change_core = NULL;
	int *change_component = NULL;

	sppwk_get_mng_data(NULL, &comp_info_base, &core_info, &change_core,
			&change_component, NULL);

	switch (wk_action) {
	case SPPWK_ACT_START:
		info = (core_info + lcore_id);
		if (info->status == SPP_CORE_UNUSE) {
			RTE_LOG(ERR, MIR_CMD_RUNNER, "Core %d is not available because "
				"it is in SPP_CORE_UNUSE state.\n", lcore_id);
			return SPP_RET_NG;
		}

		comp_lcore_id = sppwk_get_lcore_id(name);
		if (comp_lcore_id >= 0) {
			RTE_LOG(ERR, MIR_CMD_RUNNER, "Component name '%s' is already "
				"used.\n", name);
			return SPP_RET_NG;
		}

		comp_lcore_id = get_free_lcore_id();
		if (comp_lcore_id < 0) {
			RTE_LOG(ERR, MIR_CMD_RUNNER, "Cannot assign component over the "
				"maximum number.\n");
			return SPP_RET_NG;
		}

		core = &info->core[info->upd_index];

		comp_info = (comp_info_base + comp_lcore_id);
		memset(comp_info, 0x00, sizeof(struct sppwk_comp_info));
		strcpy(comp_info->name, name);
		comp_info->wk_type = wk_type;
		comp_info->lcore_id = lcore_id;
		comp_info->comp_id = comp_lcore_id;

		core->id[core->num] = comp_lcore_id;
		core->num++;
		ret = SPP_RET_OK;
		tmp_lcore_id = lcore_id;
		*(change_component + comp_lcore_id) = 1;
		break;

	case SPPWK_ACT_STOP:
		comp_lcore_id = sppwk_get_lcore_id(name);
		if (comp_lcore_id < 0)
			return SPP_RET_OK;

		comp_info = (comp_info_base + comp_lcore_id);
		tmp_lcore_id = comp_info->lcore_id;
		memset(comp_info, 0x00, sizeof(struct sppwk_comp_info));

		info = (core_info + tmp_lcore_id);
		core = &info->core[info->upd_index];

		/* The latest lcore is released if worker thread is stopped. */
		ret_del = del_comp_info(comp_lcore_id, core->num, core->id);
		if (ret_del >= 0)
			core->num--;

		ret = SPP_RET_OK;
		*(change_component + comp_lcore_id) = 0;
		break;

	default:  /* Unexpected case. */
		ret = SPP_RET_NG;
		break;
	}

	*(change_core + tmp_lcore_id) = 1;
	return ret;
}

/* Check if over the maximum num of rx and tx ports of component. */
static int
check_mir_port_count(enum sppwk_port_dir dir, int nof_rx, int nof_tx)
{
	RTE_LOG(INFO, MIR_CMD_RUNNER, "port count, port_type=%d,"
				" rx=%d, tx=%d\n", dir, nof_rx, nof_tx);
	if (dir == SPPWK_PORT_DIR_RX)
		nof_rx++;
	else
		nof_tx++;
	/* Add rx or tx port appointed in port_type. */
	RTE_LOG(INFO, MIR_CMD_RUNNER, "Num of ports after count up,"
				" port_type=%d, rx=%d, tx=%d\n",
				dir, nof_rx, nof_tx);
	if (nof_rx > 1 || nof_tx > 2)
		return SPP_RET_NG;

	return SPP_RET_OK;
}

/* Port add or del to execute it */
static int
update_port(enum sppwk_action wk_action,
		const struct sppwk_port_idx *port,
		enum sppwk_port_dir dir,
		const char *name,
		const struct spp_port_ability *ability)
{
	int ret = SPP_RET_NG;
	int port_idx;
	int ret_del = -1;
	int comp_lcore_id = 0;
	int cnt = 0;
	struct sppwk_comp_info *comp_info = NULL;
	struct sppwk_port_info *port_info = NULL;
	int *nof_ports = NULL;
	struct sppwk_port_info **ports = NULL;
	struct sppwk_comp_info *comp_info_base = NULL;
	int *change_component = NULL;

	comp_lcore_id = sppwk_get_lcore_id(name);
	if (comp_lcore_id < 0) {
		RTE_LOG(ERR, MIR_CMD_RUNNER, "Unknown component by port command. "
				"(component = %s)\n", name);
		return SPP_RET_NG;
	}
	sppwk_get_mng_data(NULL, &comp_info_base, NULL, NULL,
			&change_component, NULL);
	comp_info = (comp_info_base + comp_lcore_id);
	port_info = get_sppwk_port(port->iface_type, port->iface_no);
	if (dir == SPPWK_PORT_DIR_RX) {
		nof_ports = &comp_info->nof_rx;
		ports = comp_info->rx_ports;
	} else {
		nof_ports = &comp_info->nof_tx;
		ports = comp_info->tx_ports;
	}

	switch (wk_action) {
	case SPPWK_ACT_ADD:
		/* Check if over the maximum num of ports of component. */
		if (check_mir_port_count(dir, comp_info->nof_rx,
				comp_info->nof_tx) != SPP_RET_OK)
			return SPP_RET_NG;

		/* Check if the port_info is included in array `ports`. */
		port_idx = get_idx_port_info(port_info, *nof_ports, ports);
		if (port_idx >= SPP_RET_OK) {
			/* registered */
			/* TODO(yasufum) confirm it is needed for spp_mirror. */
			if (ability->ops == SPPWK_PORT_ABL_OPS_ADD_VLANTAG) {
				while ((cnt < PORT_ABL_MAX) &&
					    (port_info->ability[cnt].ops !=
					    SPPWK_PORT_ABL_OPS_ADD_VLANTAG))
					cnt++;
				if (cnt >= PORT_ABL_MAX) {
					RTE_LOG(ERR, MIR_CMD_RUNNER, "update VLAN tag "
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

		if (*nof_ports >= RTE_MAX_ETHPORTS) {
			RTE_LOG(ERR, MIR_CMD_RUNNER, "Cannot assign port over the "
				"maximum number.\n");
			return SPP_RET_NG;
		}

		if (ability->ops != SPPWK_PORT_ABL_OPS_NONE) {
			while ((cnt < PORT_ABL_MAX) &&
					(port_info->ability[cnt].ops !=
					SPPWK_PORT_ABL_OPS_NONE)) {
				cnt++;
			}
			if (cnt >= PORT_ABL_MAX) {
				RTE_LOG(ERR, MIR_CMD_RUNNER,
						"No space of port ability.\n");
				return SPP_RET_NG;
			}
			memcpy(&port_info->ability[cnt], ability,
					sizeof(struct spp_port_ability));
		}

		port_info->iface_type = port->iface_type;
		ports[*nof_ports] = port_info;
		(*nof_ports)++;

		ret = SPP_RET_OK;
		break;

	case SPPWK_ACT_DEL:
		for (cnt = 0; cnt < PORT_ABL_MAX; cnt++) {
			if (port_info->ability[cnt].ops ==
					SPPWK_PORT_ABL_OPS_NONE)
				continue;

			if (port_info->ability[cnt].dir == dir)
				memset(&port_info->ability[cnt], 0x00,
					sizeof(struct spp_port_ability));
		}

		ret_del = delete_port_info(port_info, *nof_ports, ports);
		if (ret_del == 0)
			(*nof_ports)--; /* If deleted, decrement number. */

		ret = SPP_RET_OK;
		break;

	default:  /* This case cannot be happend without invlid wk_action. */
		return SPP_RET_NG;
	}

	*(change_component + comp_lcore_id) = 1;
	return ret;
}

/* Execute one command. */
int
exec_one_cmd(const struct sppwk_cmd_attrs *cmd)
{
	int ret;

	RTE_LOG(INFO, MIR_CMD_RUNNER, "Exec `%s` cmd.\n",
			sppwk_cmd_type_str(cmd->type));

	switch (cmd->type) {
	case SPPWK_CMDTYPE_WORKER:
		ret = update_comp(
				cmd->spec.comp.wk_action,
				cmd->spec.comp.name,
				cmd->spec.comp.core,
				cmd->spec.comp.wk_type);
		if (ret == 0) {
			RTE_LOG(INFO, MIR_CMD_RUNNER, "Exec flush.\n");
			ret = flush_cmd();
		}
		break;

	case SPPWK_CMDTYPE_PORT:
		RTE_LOG(INFO, MIR_CMD_RUNNER, "with action `%s`.\n",
				sppwk_action_str(cmd->spec.port.wk_action));
		ret = update_port(cmd->spec.port.wk_action,
				&cmd->spec.port.port, cmd->spec.port.dir,
				cmd->spec.port.name, &cmd->spec.port.ability);
		if (ret == 0) {
			RTE_LOG(INFO, MIR_CMD_RUNNER, "Exec flush.\n");
			ret = flush_cmd();
		}
		break;

	default:
		/* Do nothing. */
		ret = SPP_RET_OK;
		break;
	}

	return ret;
}

/* Iterate core information to create response to status command */
static int
spp_iterate_core_info(struct spp_iterate_core_params *params)
{
	int ret;
	int lcore_id, cnt;
	struct core_info *core = NULL;
	struct sppwk_comp_info *comp_info_base = NULL;
	struct sppwk_comp_info *comp_info = NULL;

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (spp_get_core_status(lcore_id) == SPP_CORE_UNUSE)
			continue;

		core = get_core_info(lcore_id);
		if (core->num == 0) {
			ret = (*params->element_proc)(
				params, lcore_id,
				"", SPPWK_TYPE_NONE_STR,
				0, NULL, 0, NULL);
			if (unlikely(ret != 0)) {
				RTE_LOG(ERR, MIR_CMD_RUNNER,
						"Cannot iterate core "
						"information. "
						"(core = %d, type = %d)\n",
						lcore_id, SPPWK_TYPE_NONE);
				return SPP_RET_NG;
			}
			continue;
		}

		for (cnt = 0; cnt < core->num; cnt++) {
			sppwk_get_mng_data(NULL, &comp_info_base, NULL, NULL,
					NULL, NULL);
			comp_info = (comp_info_base + core->id[cnt]);
			ret = get_mirror_status(lcore_id, core->id[cnt],
					params);

			if (unlikely(ret != 0)) {
				RTE_LOG(ERR, MIR_CMD_RUNNER,
						"Cannot iterate core "
						"information. "
						"(core = %d, type = %d)\n",
						lcore_id, comp_info->wk_type);
				return SPP_RET_NG;
			}
		}
	}

	return SPP_RET_OK;
}

/* Add entry of core info of worker to a response in JSON such as "core:0". */
int
add_core(const char *name, char **output,
		void *tmp __attribute__ ((unused)))
{
	int ret = SPP_RET_NG;
	struct spp_iterate_core_params itr_params;
	char *tmp_buff = spp_strbuf_allocate(CMD_RES_BUF_INIT_SIZE);
	if (unlikely(tmp_buff == NULL)) {
		RTE_LOG(ERR, MIR_CMD_RUNNER,
				/* TODO(yasufum) refactor no meaning err msg */
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

	ret = append_json_array_brackets(output, name, itr_params.output);
	spp_strbuf_free(itr_params.output);
	return ret;
}

/* Activate temporarily stored component info while flushing. */
int
update_comp_info(struct sppwk_comp_info *p_comp_info, int *p_change_comp)
{
	int ret = 0;
	int cnt = 0;
	struct sppwk_comp_info *comp_info = NULL;

	for (cnt = 0; cnt < RTE_MAX_LCORE; cnt++) {
		if (*(p_change_comp + cnt) == 0)
			continue;

		comp_info = (p_comp_info + cnt);
		spp_port_ability_update(comp_info);

		ret = update_mirror(comp_info);
		RTE_LOG(DEBUG, MIR_CMD_RUNNER, "Update mirror.\n");

		if (unlikely(ret < 0)) {
			RTE_LOG(ERR, MIR_CMD_RUNNER, "Flush error. "
					"( component = %s, type = %d)\n",
					comp_info->name,
					comp_info->wk_type);
			return SPP_RET_NG;
		}
	}
	return SPP_RET_OK;
}

/* Get component type from string of its name. */
enum sppwk_worker_type
get_comp_type_from_str(const char *type_str)
{
	RTE_LOG(DEBUG, MIR_CMD_RUNNER, "type_str is %s\n", type_str);

	if (strncmp(type_str, SPPWK_TYPE_MIR_STR,
			strlen(SPPWK_TYPE_MIR_STR)+1) == 0)
		return SPPWK_TYPE_MIR;

	return SPPWK_TYPE_NONE;
}

/**
 * List of combination of tag and operator function. It is used to assemble
 * a result of command in JSON like as following.
 *
 *     {
 *         "client-id": 1,
 *         "ports": ["phy:0", "phy:1", "vhost:0", "ring:0"],
 *         "components": [
 *             {
 *                 "core": 2,
 *                 ...
 */
/* TODO(yasufum) consider to create and move to vf_cmd_formatter.c */
int get_status_ops(struct cmd_res_formatter_ops *ops_list)
{
	/* Num of entries should be the same as NOF_STAT_OPS in vf_deps.h. */
	struct cmd_res_formatter_ops tmp_ops_list[] = {
		{ "client-id", add_client_id },
		{ "phy", add_interface },
		{ "vhost", add_interface },
		{ "ring", add_interface },
		{ "master-lcore", add_master_lcore},
		{ "core", add_core},
		{ "", NULL }
	};
	memcpy(ops_list, tmp_ops_list, sizeof(tmp_ops_list));
	if (unlikely(ops_list == NULL)) {
		RTE_LOG(ERR, MIR_CMD_RUNNER, "Failed to setup ops_list.\n");
		return -1;
	}

	return 0;
}