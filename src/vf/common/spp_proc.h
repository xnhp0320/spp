/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018-2019 Nippon Telegraph and Telephone Corporation
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
#include "shared/common.h"

/**
 * TODO(Yamashita) change type names.
 *  "merge" -> "merger", "forward" -> "forwarder".
 */
/** Identifier string for each component (status command) */
#define SPP_TYPE_CLASSIFIER_MAC_STR "classifier_mac"
#define SPP_TYPE_MERGE_STR	    "merge"
#define SPP_TYPE_FORWARD_STR	    "forward"
#define SPP_TYPE_MIRROR_STR	    "mirror"
#define SPP_TYPE_UNUSE_STR	    "unuse"

/** Identifier string for each interface */
#define SPP_IFTYPE_NIC_STR   "phy"
#define SPP_IFTYPE_VHOST_STR "vhost"
#define SPP_IFTYPE_RING_STR  "ring"

/** Update wait timer (micro sec) */
#define SPP_CHANGE_UPDATE_INTERVAL 10

/** The max number of buffer for management */
#define SPP_INFO_AREA_MAX 2

/** The length of shortest character string */
#define SPP_MIN_STR_LEN   32

/** The length of NAME string */
#define SPP_NAME_STR_LEN  128

/** Maximum number of port abilities available */
#define SPP_PORT_ABILITY_MAX 4

/** Number of VLAN ID */
#define SPP_NUM_VLAN_VID 4096

/** Maximum VLAN PCP */
#define SPP_VLAN_PCP_MAX 7

/* Max number of core status check */
#define SPP_CORE_STATUS_CHECK_MAX 5

/** Character sting for default port of classifier */
#define SPP_DEFAULT_CLASSIFIED_SPEC_STR     "default"

/** Value for default MAC address of classifier */
#define SPP_DEFAULT_CLASSIFIED_DMY_ADDR     0x010000000000

/** Character sting for default MAC address of classifier */
#define SPP_DEFAULT_CLASSIFIED_DMY_ADDR_STR "00:00:00:00:00:01"

/* Sampling interval timer for latency evaluation */
#define SPP_RING_LATENCY_STATS_SAMPLING_INTERVAL 1000000

/**
 * TODO(Yamashita) change type names.
 *  "merge" -> "merger", "forward" -> "forwarder".
 */
/* Name string for each component */
#define CORE_TYPE_CLASSIFIER_MAC_STR "classifier_mac"
#define CORE_TYPE_MERGE_STR	     "merge"
#define CORE_TYPE_FORWARD_STR	     "forward"
#define CORE_TYPE_MIRROR_STR	     "mirror"

/* State on component */
enum spp_core_status {
	SPP_CORE_UNUSE,        /**< Not used */
	SPP_CORE_STOP,         /**< Stopped */
	SPP_CORE_IDLE,         /**< Idling */
	SPP_CORE_FORWARD,      /**< Forwarding  */
	SPP_CORE_STOP_REQUEST, /**< Request stopping */
	SPP_CORE_IDLE_REQUEST /**< Request idling */
};

/* Process type for each component */
enum spp_component_type {
	SPP_COMPONENT_UNUSE,          /**< Not used */
	SPP_COMPONENT_CLASSIFIER_MAC, /**< Classifier_mac */
	SPP_COMPONENT_MERGE,	      /**< Merger */
	SPP_COMPONENT_FORWARD,	      /**< Forwarder */
	SPP_COMPONENT_MIRROR,	      /**< Mirror */
};

/* Classifier Type */
enum spp_classifier_type {
	SPP_CLASSIFIER_TYPE_NONE, /**< Type none */
	SPP_CLASSIFIER_TYPE_MAC,  /**< MAC address */
	SPP_CLASSIFIER_TYPE_VLAN  /**< VLAN ID */
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

/**
 * Port ability operation which indicates vlan tag operation on the port
 * (e.g. add vlan tag or delete vlan tag)
 */
enum spp_port_ability_ope {
	SPP_PORT_ABILITY_OPE_NONE,	  /**< none */
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
	SPP_LONGOPT_RETVAL_CLIENT_ID,   /* --client-id    */
	SPP_LONGOPT_RETVAL_VHOST_CLIENT /* --vhost-client */
};

/* Flag of processing type to copy management information */
enum copy_mng_flg {
	COPY_MNG_FLG_NONE,
	COPY_MNG_FLG_UPDCOPY,
	COPY_MNG_FLG_ALLCOPY,
};

/* secondary process type used only from spp_vf and spp_mirror */
enum secondary_type {
	SECONDARY_TYPE_NONE,
	SECONDARY_TYPE_VF,
	SECONDARY_TYPE_MIRROR,
};

/**
 * Interface information structure
 */
struct spp_port_index {
	enum port_type  iface_type; /**< Interface type (phy/vhost/ring) */
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
	char name[SPP_NAME_STR_LEN];	/**< Component name */
	enum spp_component_type type;	/**< Component type */
	unsigned int lcore_id;		/**< Logical core ID for component */
	int component_id;		/**< Component ID */
	int num_rx_port;		/**< The number of rx ports */
	int num_tx_port;		/**< The number of tx ports */
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
	int vhost_client;	/* Flag for --vhost-client option */
	enum secondary_type secondary_type;
				/* secondary type */
};

/* Manage number of interfaces  and port information as global variable */
struct iface_info {
	int num_nic;		/* The number of phy */
	int num_vhost;		/* The number of vhost */
	int num_ring;		/* The number of ring */
	struct spp_port_info nic[RTE_MAX_ETHPORTS];
				/* Port information of phy */
	struct spp_port_info vhost[RTE_MAX_ETHPORTS];
				/* Port information of vhost */
	struct spp_port_info ring[RTE_MAX_ETHPORTS];
				/* Port information of ring */
};

/* Manage component running in core as global variable */
struct core_info {
	int num;	       /* The number of IDs below */
	int id[RTE_MAX_LCORE]; /* ID list of components executed on cpu core */
};

/* Manage core status and component information as global variable */
struct core_mng_info {
	/* Status of cpu core */
	volatile enum spp_core_status status;

	/* Index number of core information for reference */
	volatile int ref_index;

	/* Index number of core information for updating */
	volatile int upd_index;

	/* Core information of each cpu core */
	struct core_info core[SPP_INFO_AREA_MAX];
};

/* Manage data to be backup */
struct cancel_backup_info {
	/* Backup data of core information */
	struct core_mng_info core[RTE_MAX_LCORE];

	/* Backup data of component information */
	struct spp_component_info component[RTE_MAX_LCORE];

	/* Backup data of interface information */
	struct iface_info interface;
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

struct spp_iterate_classifier_table_params;
/**
 * definition of iterated classifier element procedure function
 * which is member of spp_iterate_classifier_table_params structure.
 * Above structure is used when listing classifier table information
 * (e.g) create resonse to status command.
 */
typedef int (*spp_iterate_classifier_element_proc)(
		struct spp_iterate_classifier_table_params *params,
		enum spp_classifier_type type,
		int vid, const char *mac,
		const struct spp_port_index *port);

/**
 * iterate classifier table parameters which is
 * used when listing classifier table content
 * (e.g.) create response to status command.
 */
struct spp_iterate_classifier_table_params {
	/* Output buffer */
	void *output;

	/* The function for creating classifier table information */
	spp_iterate_classifier_element_proc element_proc;
};

/**
 * Make a hexdump of an array data in every 4 byte
 *
 * @param name
 *  dump name.
 * @param addr
 *  dump address.
 * @param size
 *  dump byte size.
 *
 */
void dump_buff(const char *name, const void *addr, const size_t size);

/**
 * added ring_pmd
 *
 * @param ring_id
 *  added ring id.
 *
 * @retval 0~   ring_port_id.
 * @retval -1   failed.
 */
int spp_vf_add_ring_pmd(int ring_id);

/**
 * added vhost_pmd
 *
 * @param index
 *  add vohst id.
 * @param client
 *  add client id.
 *
 * @retval 0~   vhost_port_id.
 * @retval -1   failed.
 */
int spp_vf_add_vhost_pmd(int index, int client);

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
 * Get component type of target component_info
 *
 * @param id
 *  component ID.
 *
 * @return
 *  Type of component executed
 */
enum spp_component_type spp_get_component_type(int id);

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

/* Dump of core information */
void dump_core_info(const struct core_mng_info *core_info);

/* Dump of component information */
void dump_component_info(const struct spp_component_info *component_info);

/* Dump of interface information */
void dump_interface_info(const struct iface_info *iface_info);

/* Dump of all management information */
void dump_all_mng_info(
		const struct core_mng_info *core,
		const struct spp_component_info *component,
		const struct iface_info *interface);

/* Copy management information */
void copy_mng_info(
		struct core_mng_info *dst_core,
		struct spp_component_info *dst_component,
		struct iface_info *dst_interface,
		const struct core_mng_info *src_core,
		const struct spp_component_info *src_component,
		const struct iface_info *src_interface,
		enum copy_mng_flg flg);

/* Backup the management information */
void backup_mng_info(struct cancel_backup_info *backup);

/**
 * Setup management info for spp_vf
 */
int init_mng_data(void);

#ifdef SPP_RINGLATENCYSTATS_ENABLE
/**
 * Print statistics of time for packet processing in ring interface
 */
void print_ring_latency_stats(void);
#endif /* SPP_RINGLATENCYSTATS_ENABLE */

/* Remove sock file if spp is not running */
void  del_vhost_sockfile(struct spp_port_info *vhost);

/**
 * Get core ID of target component
 *
 * @param component_id
 *  unique component ID.
 *
 * @return
 *  Logical core id of specified component.
 */
unsigned int spp_get_component_core(int component_id);

/* Get core information which is in use */
struct core_info *get_core_info(unsigned int lcore_id);

/**
 * Check core index change
 *
 * @param lcore_id
 *  Logical core ID.
 *
 *  True if index has changed.
 * @retval SPP_RET_OK index has changed.
 * @retval SPP_RET_NG index not changed.
 */
int spp_check_core_update(unsigned int lcore_id);

/**
 * Check if component is using port.
 *
 * @param iface_type
 *  Interface type to be validated.
 * @param iface_no
 *  Interface number to be validated.
 * @param rxtx
 *  tx/rx type to be validated.
 *
 * @retval 0~127      match component ID
 * @retval SPP_RET_NG failed.
 */
int spp_check_used_port(
		enum port_type iface_type,
		int iface_no,
		enum spp_port_rxtx rxtx);

/**
 * Set component update flag for given port.
 *
 * @param port
 *  spp_port_info address
 * @param rxtx
 *  enum spp_port_rxtx
 *
 */
void
set_component_change_port(struct spp_port_info *port, enum spp_port_rxtx rxtx);

/**
 * Get unused component id
 *
 * @retval 0~127 Component ID.
 * @retval -1    failed.
 */
int get_free_component(void);

/**
 * Get component id for specified component name
 *
 * @param name
 *  Component name.
 *
 * @retval 0~127      Component ID.
 * @retval SPP_RET_NG failed.
 */
int spp_get_component_id(const char *name);

/**
 *  Delete component information.
 *
 * @param component_id
 *  check data
 * @param component_num
 *  array check count
 * @param componet_array
 *  check array address
 *
 * @retval 0  succeeded.
 * @retval -1 failed.
 */
int
del_component_info(int component_id, int component_num, int *componet_array);

/**
 * get port element which matches the condition.
 *
 * @param info
 *  spp_port_info address
 * @param num
 *  port count
 * @param array[]
 *  spp_port_info array address
 *
 * @retval 0~ match index.
 * @retval -1 failed.
 */
int check_port_element(
		struct spp_port_info *info,
		int num,
		struct spp_port_info *array[]);

/**
 *  search matched port_info from array and delete it.
 *
 * @param info
 *  spp_port_info address
 * @param num
 *  port count
 * @param array[]
 *  spp_port_info array address
 *
 * @retval 0  succeeded.
 * @retval -1 failed.
 */
int get_del_port_element(
		struct spp_port_info *info,
		int num,
		struct spp_port_info *array[]);

/**
 * Flush initial setting of each interface.
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int flush_port(void);

/**
 *  Flush changed core.
 */
void flush_core(void);

/**
 *  Flush change for forwarder or classifier_mac.
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int flush_component(void);

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
 * Change mac address string to int64
 *
 * @param mac
 *  Character string of MAC address to be converted.
 *
 * @retval 0< int64 that store mac address
 * @retval SPP_RET_NG
 */
int64_t spp_change_mac_str_to_int64(const char *mac);

/**
 * Set mange data address
 *
 * @param startup_param_addr
 *  g_startup_param address
 * @param iface_addr
 *  g_iface_info address
 * @param component_addr
 *  g_component_info address
 * @param core_mng_addr
 *  g_core_info address
 * @param change_core_addr
 *  g_change_core address
 * @param change_component_addr
 *  g_change_component address
 * @param backup_info_addr
 *  g_backup_info address
 * @param main_lcore_id
 *  main_lcore_id mask
 *
 * @retval SPP_RET_OK succeeded.
 * @retval SPP_RET_NG failed.
 */
int spp_set_mng_data_addr(struct startup_param *startup_param_addr,
			  struct iface_info *iface_addr,
			  struct spp_component_info *component_addr,
			  struct core_mng_info *core_mng_addr,
			  int *change_core_addr,
			  int *change_component_addr,
			  struct cancel_backup_info *backup_info_addr,
			  unsigned int main_lcore_id);

/**
 * Get mange data address
 *
 * @param iface_addr
 *  g_startup_param write address
 * @param iface_addr
 *  g_iface_info write address
 * @param component_addr
 *  g_component_info write address
 * @param core_mng_addr
 *  g_core_mng_info write address
 * @param change_core_addr
 *  g_change_core write address
 * @param change_component_addr
 *  g_change_component write address
 * @param backup_info_addr
 *  g_backup_info write address
 */
void spp_get_mng_data_addr(struct startup_param **startup_param_addr,
			   struct iface_info **iface_addr,
			   struct spp_component_info **component_addr,
			   struct core_mng_info **core_mng_addr,
			   int **change_core_addr,
			   int **change_component_addr,
			   struct cancel_backup_info **backup_info_addr);

#endif /* _SPP_PROC_H_ */
