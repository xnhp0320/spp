/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#ifndef _SPP_PROC_H_
#define _SPP_PROC_H_

/**
 * @file
 * SPP process
 *
 * SPP component common function.
 */

#include <netinet/in.h>
#include "common.h"

/* Max number of core status check */
#define SPP_CORE_STATUS_CHECK_MAX 5

/** The length of shortest character string */
#define SPP_MIN_STR_LEN   32

/** The length of NAME string */
#define SPP_NAME_STR_LEN  128

/** Maximum number of port abilities available */
#define SPP_PORT_ABILITY_MAX 4

/** Identifier string for each interface */
#define SPP_IFTYPE_NIC_STR   "phy"
#define SPP_IFTYPE_RING_STR  "ring"

/* State on core */
enum spp_core_status {
	SPP_CORE_UNUSE,        /**< Not used */
	SPP_CORE_STOP,         /**< Stopped */
	SPP_CORE_IDLE,         /**< Idling */
	SPP_CORE_FORWARD,      /**< Forwarding  */
	SPP_CORE_STOP_REQUEST, /**< Request stopping */
	SPP_CORE_IDLE_REQUEST  /**< Request idling */
};

/* State on capture */
enum spp_capture_status {
	SPP_CAPTURE_IDLE,      /* Idling */
	SPP_CAPTURE_RUNNING     /* Running */
};

enum spp_return_value {
	SPP_RET_OK = 0,  /**< succeeded */
	SPP_RET_NG = -1, /**< failed */
};

/**
 * Port type (rx or tx) to indicate which direction packet goes
 * (e.g. receiving or transmitting)
 */
enum spp_port_rxtx {
	SPP_PORT_RXTX_NONE, /**< none */
	SPP_PORT_RXTX_RX,   /**< rx port */
	SPP_PORT_RXTX_TX,   /**< tx port */
	SPP_PORT_RXTX_ALL,  /**< rx/tx port */
};

/* Process type for each component */
enum spp_component_type {
	SPP_COMPONENT_UNUSE,          /**< Not used */
	SPP_COMPONENT_CLASSIFIER_MAC, /**< Classifier_mac */
	SPP_COMPONENT_MERGE,          /**< Merger */
	SPP_COMPONENT_FORWARD,        /**< Forwarder */
	SPP_COMPONENT_MIRROR,         /**< Mirror */
};

/**
 * Port ability operation which indicates vlan tag operation on the port
 * (e.g. add vlan tag or delete vlan tag)
 */
enum spp_port_ability_ope {
	SPP_PORT_ABILITY_OPE_NONE,        /**< none */
	SPP_PORT_ABILITY_OPE_ADD_VLANTAG, /**< add VLAN tag */
	SPP_PORT_ABILITY_OPE_DEL_VLANTAG, /**< delete VLAN tag */
};

/* getopt_long return value for long option */
enum SPP_LONGOPT_RETVAL {
	SPP_LONGOPT_RETVAL__ = 127,

	/*
	 * Return value definition for getopt_long()
	 * Only for long option
	 */
	SPP_LONGOPT_RETVAL_CLIENT_ID,      /* --client-id       */
	SPP_LONGOPT_RETVAL_OUTPUT,         /* --output          */
	SPP_LONGOPT_RETVAL_LIMIT_FILE_SIZE /* --limit_file_size */
};

/* Interface information structure */
struct spp_port_index {
	enum port_type  iface_type; /**< Interface type (phy/ring) */
	int             iface_no;   /**< Interface number */
};

/** VLAN tag information */
struct spp_vlantag_info {
	int vid; /**< VLAN ID */
	int pcp; /**< Priority Code Point */
	int tci; /**< Tag Control Information */
};

/**
 * Data for each port ability which indicates vlantag related information
 * for the port
 */
union spp_ability_data {
	/** VLAN tag information */
	struct spp_vlantag_info vlantag;
};

/** Port ability information */
struct spp_port_ability {
	enum spp_port_ability_ope ope; /**< Operation */
	enum spp_port_rxtx rxtx;       /**< rx/tx identifier */
	union spp_ability_data data;   /**< Port ability data */
};

/** Port class identifier for classifying */
struct spp_port_class_identifier {
	uint64_t mac_addr;                      /**< Mac address (binary) */
	char     mac_addr_str[SPP_MIN_STR_LEN]; /**< Mac address (text) */
	struct spp_vlantag_info vlantag;        /**< VLAN tag information */
};

/* Port info */
struct spp_port_info {
	enum port_type iface_type;      /**< Interface type (phy/vhost/ring) */
	int            iface_no;        /**< Interface number */
	int            dpdk_port;       /**< DPDK port number */
	struct spp_port_class_identifier class_id;
					/**< Port class identifier */
	struct spp_port_ability ability[SPP_PORT_ABILITY_MAX];
					/**< Port ability */
};

/* Component info */
struct spp_component_info {
	char name[SPP_NAME_STR_LEN];    /**< Component name */
	enum spp_component_type type;   /**< Component type */
	unsigned int lcore_id;          /**< Logical core ID for component */
	int component_id;               /**< Component ID */
	int num_rx_port;                /**< The number of rx ports */
	int num_tx_port;                /**< The number of tx ports */
	struct spp_port_info *rx_ports[RTE_MAX_ETHPORTS];
					/**< Array of pointers to rx ports */
	struct spp_port_info *tx_ports[RTE_MAX_ETHPORTS];
					/**< Array of pointers to tx ports */
};

/* Manage given options as global variable */
struct startup_param {
	int client_id;		/* Client ID */
	char server_ip[INET_ADDRSTRLEN];
				/* IP address stiring of spp-ctl */
	int server_port;	/* Port Number of spp-ctl */
};

/* Manage interfaces and port information as global variable */
struct iface_info {
	int num_nic;            /* The number of phy */
	int num_ring;           /* The number of ring */
	struct spp_port_info nic[RTE_MAX_ETHPORTS];
				/* Port information of phy */
	struct spp_port_info ring[RTE_MAX_ETHPORTS];
				/* Port information of ring */
};

/* Manage core status and component information as global variable */
struct core_mng_info {
	/* Status of cpu core */
	volatile enum spp_core_status status;
};

struct spp_iterate_core_params;
/**
 * definition of iterated core element procedure function
 * which is member of spp_iterate_core_params structure.
 * Above structure is used when listing core information
 * (e.g) create resonse to status command.
 */
typedef int (*spp_iterate_core_element_proc)(
		struct spp_iterate_core_params *params,
		const unsigned int lcore_id,
		const char *name,
		const char *type,
		const int num_rx,
		const struct spp_port_index *rx_ports,
		const int num_tx,
		const struct spp_port_index *tx_ports);

/**
 * iterate core table parameters which is
 * used when listing core table content
 * (e.g.) create response to status command.
 */
struct spp_iterate_core_params {
	/** Output buffer */
	char *output;

	/** The function for creating core information */
	spp_iterate_core_element_proc element_proc;
};

/**
 * added ring_pmd
 *
 * @param ring_id
 *  added ring id.
 *
 * @retval 0~   ring_port_id.
 * @retval -1   failed.
 */
int add_ring_pmd(int ring_id);

/**
 * Get core status
 *
 * @param lcore_id
 *  Logical core ID.
 *
 * @return
 *  Status of specified logical core.
 */
enum spp_core_status spp_get_core_status(unsigned int lcore_id);

/**
 * Run check_core_status() for SPP_CORE_STATUS_CHECK_MAX times with
 * interval time (1sec)
 *
 * @param status
 *  wait check status.
 *
 * @retval 0  succeeded.
 * @retval -1 failed.
 */
int check_core_status_wait(enum spp_core_status status);

/**
 * Set core status
 *
 * @param lcore_id
 *  Logical core ID.
 * @param status
 *  set status.
 *
 */
void set_core_status(unsigned int lcore_id, enum spp_core_status status);

/**
 * Set all core status to given
 *
 * @param status
 *  set status.
 *
 */
void set_all_core_status(enum spp_core_status status);

/**
 * Set all of component status to SPP_CORE_STOP_REQUEST if received signal
 * is SIGTERM or SIGINT
 *
 * @param signl
 *  received signal.
 *
 */
void stop_process(int signal);

/**
 * Return port info of given type and num of interface
 *
 * @param iface_type
 *  Interface type to be validated.
 * @param iface_no
 *  Interface number to be validated.
 *
 * @retval !NULL  spp_port_info.
 * @retval NULL   failed.
 */
struct spp_port_info *
get_iface_info(enum port_type iface_type, int iface_no);

/**
 * Setup management info for spp_vf
 */
int init_mng_data(void);

/**
 * Get component type of target core
 *
 * @param lcore_id
 *  Logical core ID.
 *
 * @return
 *  Type of component executed on specified logical core
 */
enum spp_component_type
spp_get_component_type(unsigned int lcore_id);

/* Get core information which is in use */
struct core_info *get_core_info(unsigned int lcore_id);

/**
 * Port type to string
 *
 * @param port
 *  Character string of Port type to be converted.
 * @param iface_type
 *  port interface type
 * @param iface_no
 *  interface no
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int
spp_format_port_string(char *port, enum port_type iface_type, int iface_no);

/**
 * Port type to string
 *
 * @param port
 *  Character string of Port type to be converted.
 * @param iface_type
 *  port interface type
 * @param iface_no
 *  interface no
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int
spp_format_port_string(char *port, enum port_type iface_type, int iface_no);

/**
 * Set mange data address
 *
 * @param startup_param_addr
 *  g_startup_param address
 * @param iface_addr
 *  g_iface_info address
 * @param core_mng_addr
 *  g_core_info address
 * @param capture_request_addr
 *  g_capture_request address
 * @param capture_status_addr
 *  g_capture_status address
 * @param main_lcore_id
 *  main_lcore_id mask
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int spp_set_mng_data_addr(struct startup_param *startup_param_addr,
			  struct iface_info *iface_addr,
			  struct core_mng_info *core_mng_addr,
			  int *capture_request_addr,
			  int *capture_status_addr,
			  unsigned int main_lcore_id);

/**
 * Get mange data address
 *
 * @param iface_addr
 *  g_startup_param write address
 * @param iface_addr
 *  g_iface_info write address
 * @param core_mng_addr
 *  g_core_mng_info write address
 * @param change_core_addr
 *  g_capture_request write address
 * @param change_component_addr
 *  g_capture_status write address
 */
void spp_get_mng_data_addr(struct startup_param **startup_param_addr,
			   struct iface_info **iface_addr,
			   struct core_mng_info **core_mng_addr,
			   int **capture_request_addr,
			   int **capture_status_addr);

#endif /* _SPP_PROC_H_ */
