/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Nippon Telegraph and Telephone Corporation
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <sys/stat.h>

#include <rte_common.h>
#include <rte_cycles.h>

#include <lz4frame.h>

#include "common.h"
#include "spp_proc.h"
#include "spp_pcap.h"
#include "command_proc.h"
#include "command_dec.h"
#include "spp_port.h"

/* Declare global variables */
#define RTE_LOGTYPE_PCAP RTE_LOGTYPE_USER1

#define COMPRESS_FILEPATH_LENGTH 128
#define COMPRESS_FILENAME_LENGTH 64
#define COMPRESS_FILEDATE_LENGTH 16
#define TCPDUMP_MAGIC            0xa1b2c3d4
#define PCAP_VERSION_MAJOR       2
#define PCAP_VERSION_MINOR       4
#define PCAP_SNAPLEN_DEFAULT     65535
#define PCAP_NETWORK             1
#define IN_CHUNK_SIZE            (16*1024)
#define DEFAULT_FILE_PATH        "/tmp"
#define DEFAULT_FILE_LIMIT       1073741824
#define PORT_STR_SIZE            16
#define NUM_MBUFS_DEFAULT        8192
/* macro */
#define SNAPLEN_REVISION(a, b) (((a) < (b))?(a):(b))

/* cpature thread type */
enum spp_capture_type {
	TYPE_UNUSE,  /* Not used */
	TYPE_RECIVE, /* thread type receive */
	TYPE_WRITE   /* thread type write */
};

/* compress file generate mode */
enum comp_file_generate_mode {
	INIT_MODE,   /* initial generation mode */
	UPDATE_MODE, /* update generation mode */
	CLOSE_MODE   /* close mode */
};

/* capture thread name string  */
const char *CAPTURE_THREAD_TYPE_SRINGS[] = {
	SPP_TYPE_UNUSE_STR,
	"receive",
	"write",

	/* termination */ "",
};

/* lz4 preferences */
static const LZ4F_preferences_t g_kprefs = {
	{
		LZ4F_max256KB,
		LZ4F_blockLinked,
		LZ4F_noContentChecksum,
		LZ4F_frame,
		0,                   /* unknown content size */
		{ 0, 0},             /* reserved, must be set to 0 */
	},
	0,                           /* compression level; 0 == default */
	0,                           /* autoflush */
	{ 0, 0, 0, 0},               /* reserved, must be set to 0 */
};

/* pcap file header */
struct __attribute__((__packed__)) pcap_header {
	uint32_t magic_number;  /* magic number */
	uint16_t version_major; /* major version number */
	uint16_t version_minor; /* minor version number */
	int32_t  thiszone;      /* GMT to local correction */
	uint32_t sigfigs;       /* accuracy of timestamps */
	uint32_t snaplen;       /* max length of captured packets, in octets */
	uint32_t network;       /* data link type */
};

/* pcap packet header */
struct pcap_packet_header {
	uint32_t ts_sec;        /* time stamp seconds */
	uint32_t ts_usec;       /* time stamp micro seconds */
	uint32_t write_len;     /* write length */
	uint32_t packet_len;    /* packet length */
};

/* Option for pcap. */
struct pcap_option {
	struct timespec start_time; /* start time */
	uint64_t file_limit;        /* file size limit */
	char compress_file_path[COMPRESS_FILEPATH_LENGTH]; /* file path */
	char compress_file_date[COMPRESS_FILEDATE_LENGTH]; /* file name date */
	struct spp_port_info port_cap;  /* capture port */
	struct rte_ring *cap_ring;      /* RTE ring structure */
};

/* pcap mange info */
struct pcap_mng_info {
	volatile enum spp_capture_type type; /* thread type */
	enum spp_capture_status status; /* thread status */
	int thread_no;                 /* thread no */
	int file_no;                   /* file no */
	char compress_file_name[COMPRESS_FILENAME_LENGTH]; /* lz4 file name */
	LZ4F_compressionContext_t ctx; /* lz4 file Ccontext */
	FILE *compress_fp;             /* lzf file pointer */
	size_t outbuf_capacity;        /* compress date buffer size */
	void *outbuff;                 /* compress date buffer */
	uint64_t file_size;            /* file write size */
};

/* Logical core ID for main process */
static unsigned int g_main_lcore_id = 0xffffffff;

/* Execution parameter of spp_pcap */
static struct startup_param g_startup_param;

/* Interface management information */
static struct iface_info g_iface_info;

/* Component management information */
static struct spp_component_info g_component_info[RTE_MAX_LCORE];

/* Core management information */
static struct core_mng_info g_core_info[RTE_MAX_LCORE];

/* Packet capture request information */
static int g_capture_request[RTE_MAX_LCORE];

/* Packet capture status information */
static int g_capture_status[RTE_MAX_LCORE];

/* Backup information for cancel command */
static struct cancel_backup_info g_backup_info;

/* pcap option */
static struct pcap_option g_pcap_option;

/* pcap manged info */
static struct pcap_mng_info g_pcap_info[RTE_MAX_LCORE];

/* Print help message */
static void
usage(const char *progname)
{
	RTE_LOG(INFO, PCAP, "Usage: %s [EAL args] --"
		" --client-id CLIENT_ID"
		" -s SERVER_IP:SERVER_PORT"
		" -i INPUT_PORT"
		" [--output FILE_OUT_PUT_PATH]"
		" [--limit_file_size LIMIT_FILE_SIZE]\n"
		" --client-id CLIENT_ID   : My client ID\n"
		" -s SERVER_IP:SERVER_PORT  : "
				"Access information to the server\n"
		" -i                        : capture port(phy,ring)\n"
		" --output                  : file path(default:/tmp)\n"
		" --limit_file_size         : "
				"file limit size(default:1073741824 Byte)\n"
		, progname);
}

/**
 * Convert string of given client id to integer
 *
 * If succeeded, client id of integer is assigned to client_id and
 * return SPP_RET_OK. Or return -SPP_RET_NG if failed.
 */
static int
parse_app_client_id(const char *client_id_str, int *client_id)
{
	int id = 0;
	char *endptr = NULL;

	id = strtol(client_id_str, &endptr, 0);
	if (unlikely(client_id_str == endptr) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	if (id >= RTE_MAX_LCORE)
		return SPP_RET_NG;

	*client_id = id;
	RTE_LOG(DEBUG, PCAP, "Set client id = %d\n", *client_id);
	return SPP_RET_OK;
}

/* Parse options for server IP and port */
static int
parse_app_server(const char *server_str, char *server_ip, int *server_port)
{
	const char delim[2] = ":";
	unsigned int pos = 0;
	int port = 0;
	char *endptr = NULL;

	pos = strcspn(server_str, delim);
	if (pos >= strlen(server_str))
		return SPP_RET_NG;

	port = strtol(&server_str[pos+1], &endptr, 0);
	if (unlikely(&server_str[pos+1] == endptr) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	memcpy(server_ip, server_str, pos);
	server_ip[pos] = '\0';
	*server_port = port;
	RTE_LOG(DEBUG, PCAP, "Set server IP   = %s\n", server_ip);
	RTE_LOG(DEBUG, PCAP, "Set server port = %d\n", *server_port);
	return SPP_RET_OK;
}


/* Parse options for limit file zise */
static int
parse_app_limit_file_size(const char *limit_size_str, uint64_t *limit_size)
{
	uint64_t file_limit = 0;
	char *endptr = NULL;

	file_limit = strtoull(limit_size_str, &endptr, 10);
	if (unlikely(limit_size_str == endptr) || unlikely(*endptr != '\0'))
		return SPP_RET_NG;

	*limit_size = file_limit;
	RTE_LOG(DEBUG, PCAP, "Set limit file zise = %ld\n", *limit_size);
	return SPP_RET_OK;
}

/* Parse options for port */
static int
parse_app_capture_port(const char *port_str, enum port_type *iface_type,
			int *iface_no)
{
	enum port_type type = UNDEF;
	const char *no_str = NULL;
	char *endptr = NULL;

	/* Find out which type of interface from port */
	if (strncmp(port_str, SPP_IFTYPE_NIC_STR ":",
			strlen(SPP_IFTYPE_NIC_STR)+1) == 0) {
		/* NIC */
		type = PHY;
		no_str = &port_str[strlen(SPP_IFTYPE_NIC_STR)+1];
	} else if (strncmp(port_str, SPP_IFTYPE_RING_STR ":",
			strlen(SPP_IFTYPE_RING_STR)+1) == 0) {
		/* RING */
		type = RING;
		no_str = &port_str[strlen(SPP_IFTYPE_RING_STR)+1];
	} else {
		/* OTHER */
		RTE_LOG(ERR, PCAP, "The interface that does not suppor. "
					"(port = %s)\n", port_str);
		return SPP_RET_NG;
	}

	/* Change type of number of interface */
	int ret_no = strtol(no_str, &endptr, 0);
	if (unlikely(no_str == endptr) || unlikely(*endptr != '\0')) {
		/* No IF number */
		RTE_LOG(ERR, PCAP, "No interface number. (port = %s)\n",
								port_str);
		return SPP_RET_NG;
	}

	*iface_type = type;
	*iface_no = ret_no;

	RTE_LOG(DEBUG, PCAP, "Port = %s => Type = %d No = %d\n",
					port_str, *iface_type, *iface_no);
	return SPP_RET_OK;
}

/* Parse options for client app */
static int
parse_app_args(int argc, char *argv[])
{
	int cnt;
	int proc_flg = 0;
	int server_flg = 0;
	int port_flg = 0;
	int option_index, opt;
	const int argcopt = argc;
	char *argvopt[argcopt];
	const char *progname = argv[0];
	char port_str[PORT_STR_SIZE];
	static struct option lgopts[] = {
			{ "client-id",       required_argument, NULL,
					SPP_LONGOPT_RETVAL_CLIENT_ID },
			{ "output",          required_argument, NULL,
					SPP_LONGOPT_RETVAL_OUTPUT },
			{ "limit_file_size", required_argument, NULL,
					SPP_LONGOPT_RETVAL_LIMIT_FILE_SIZE},
			{ 0 },
	};
	/**
	 * Save argv to argvopt to avoid losing the order of options
	 * by getopt_long()
	 */
	for (cnt = 0; cnt < argcopt; cnt++)
		argvopt[cnt] = argv[cnt];

	/* Clear startup parameters */
	memset(&g_startup_param, 0x00, sizeof(g_startup_param));

	/* option parameters init */
	memset(&g_pcap_option, 0x00, sizeof(g_pcap_option));
	strcpy(g_pcap_option.compress_file_path, DEFAULT_FILE_PATH);
	g_pcap_option.file_limit = DEFAULT_FILE_LIMIT;

	/* Check options of application */
	optind = 0;
	opterr = 0;
	while ((opt = getopt_long(argc, argvopt, "i:s:", lgopts,
			&option_index)) != EOF) {
		switch (opt) {
		case SPP_LONGOPT_RETVAL_CLIENT_ID:
			if (parse_app_client_id(optarg,
					&g_startup_param.client_id) !=
								SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			proc_flg = 1;
			break;
		case SPP_LONGOPT_RETVAL_OUTPUT:
			strcpy(g_pcap_option.compress_file_path, optarg);
			struct stat statBuf;
			if (g_pcap_option.compress_file_path[0] == '\0' ||
						stat(optarg, &statBuf) != 0) {
				usage(progname);
				return SPP_RET_NG;
			}
			break;
		case SPP_LONGOPT_RETVAL_LIMIT_FILE_SIZE:
			if (parse_app_limit_file_size(optarg,
						&g_pcap_option.file_limit) !=
						SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			break;
		case 'i':
			strcpy(port_str, optarg);
			if (parse_app_capture_port(optarg,
					&g_pcap_option.port_cap.iface_type,
					&g_pcap_option.port_cap.iface_no) !=
					SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			port_flg = 1;
			break;
		case 's':
			if (parse_app_server(optarg, g_startup_param.server_ip,
					&g_startup_param.server_port) !=
								SPP_RET_OK) {
				usage(progname);
				return SPP_RET_NG;
			}
			server_flg = 1;
			break;
		default:
			usage(progname);
			return SPP_RET_NG;
		}
	}

	/* Check mandatory parameters */
	if ((proc_flg == 0) || (server_flg == 0) || (port_flg == 0)) {
		usage(progname);
		return SPP_RET_NG;
	}
	g_startup_param.secondary_type = SECONDARY_TYPE_PCAP;

	RTE_LOG(INFO, PCAP,
			"app opts (client_id=%d,type=%d,server=%s:%d,"
			"port=%s,output=%s,limit_file_size=%ld)\n",
			g_startup_param.client_id,
			g_startup_param.secondary_type,
			g_startup_param.server_ip,
			g_startup_param.server_port,
			port_str,
			g_pcap_option.compress_file_path,
			g_pcap_option.file_limit);
	return SPP_RET_OK;
}

/* Pcap get component status */
int
spp_pcap_get_component_status(
		unsigned int lcore_id, int id __attribute__ ((unused)),
		struct spp_iterate_core_params *params)
{
	int ret = SPP_RET_NG;
	const char *role_type = NULL;
	struct pcap_mng_info *info = &g_pcap_info[lcore_id];
	const char *file_name = NULL;
	struct spp_port_index rx_ports[1];
	int rx_num = 0;

	if (info->type == TYPE_RECIVE) {
		memset(rx_ports, 0x00, sizeof(rx_ports));
		rx_ports[0].iface_type = g_pcap_option.port_cap.iface_type;
		rx_ports[0].iface_no   = g_pcap_option.port_cap.iface_no;
		rx_num = 1;
	}
	if (info->type == TYPE_WRITE) {
		file_name = "";
		if (info->compress_fp != NULL)
			file_name = info->compress_file_name;
	}

	role_type = CAPTURE_THREAD_TYPE_SRINGS[info->type];

	/* Set the information with the function specified by the command. */
	ret = (*params->element_proc)(
		params, lcore_id,
		file_name, role_type,
		rx_num, rx_ports, 0, NULL);
	if (unlikely(ret != 0))
		return SPP_RET_NG;

	return SPP_RET_OK;
}

/* compress file write  */
static int file_write(FILE *compress_fp, void *srcbuf, size_t write_len)
{
	size_t write_size;

	if (write_len == 0)
		return SPP_RET_OK;
	write_size = fwrite(srcbuf, write_len, 1, compress_fp);
	if (write_size != 1) {
		RTE_LOG(ERR, PCAP, "file write error len=%lu\n", write_len);
		return SPP_RET_NG;
	}
	return SPP_RET_OK;
}

/* data compress & file write */
static int compress_file_write(struct pcap_mng_info *info,
			       void *srcbuf,
			       int src_len)
{
	size_t compress_len;

	compress_len = LZ4F_compressUpdate(info->ctx, info->outbuff,
				info->outbuf_capacity, srcbuf, src_len, NULL);
	if (LZ4F_isError(compress_len)) {
		RTE_LOG(ERR, PCAP, "Compression failed: error %ld\n",
								compress_len);
		return SPP_RET_NG;
	}
	RTE_LOG(DEBUG, PCAP, "src len=%d\n", src_len);
	RTE_LOG(DEBUG, PCAP, "compress len=%ld\n", compress_len);
	if (file_write(info->compress_fp, info->outbuff, compress_len) != 0)
		return SPP_RET_NG;

	info->file_size += compress_len;

	return SPP_RET_OK;
}

/* Compressed file operation (open,update,close) */
static int compress_file_operation(struct pcap_mng_info *info,
				   enum comp_file_generate_mode mode)
{
	struct pcap_header pcap_h;
	size_t ctxCreation;
	size_t headerSize;
	size_t compress_len;
	char temp_file[COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH];
	char save_file[COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH];
	const char *iface_type_str;

	if (mode == INIT_MODE) {
		/* initial generation mode */
		/* write buffer size get */
		info->outbuf_capacity = LZ4F_compressBound(IN_CHUNK_SIZE,
								&g_kprefs);
		/* write buff allocation */
		info->outbuff = malloc(info->outbuf_capacity);

		/* common memory initialise */
		info->file_size = 0;
		info->file_no = 1;
		if (g_pcap_option.port_cap.iface_type == PHY)
			iface_type_str = SPP_IFTYPE_NIC_STR;
		else
			iface_type_str = SPP_IFTYPE_RING_STR;
		snprintf(info->compress_file_name,
					COMPRESS_FILENAME_LENGTH - 1,
					"spp_pcap.%s.%s%d.%u.%u.pcap.lz4",
					g_pcap_option.compress_file_date,
					iface_type_str,
					g_pcap_option.port_cap.iface_no,
					info->thread_no,
					info->file_no);
	} else if (mode == UPDATE_MODE) {
		/* update generation mode */
		/* old compress file close */
		/* flush whatever remains within internal buffers */
		compress_len = LZ4F_compressEnd(info->ctx, info->outbuff,
					info->outbuf_capacity, NULL);
		if (LZ4F_isError(compress_len)) {
			RTE_LOG(ERR, PCAP, "Failed to end compression: "
					"error %ld\n", compress_len);
			fclose(info->compress_fp);
			info->compress_fp = NULL;
			free(info->outbuff);
			return SPP_RET_NG;
		}
		if (file_write(info->compress_fp, info->outbuff, compress_len)
							!= SPP_RET_OK) {
			fclose(info->compress_fp);
			info->compress_fp = NULL;
			free(info->outbuff);
			return SPP_RET_NG;
		}

		fclose(info->compress_fp);
		info->compress_fp = NULL;

		/* file rename */
		memset(temp_file, 0,
			COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH);
		memset(save_file, 0,
			COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH);
		snprintf(temp_file,
		    (COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH) - 1,
		    "%s/%s.tmp", g_pcap_option.compress_file_path,
		    info->compress_file_name);
		snprintf(save_file,
		    (COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH) - 1,
		    "%s/%s", g_pcap_option.compress_file_path,
		    info->compress_file_name);
		rename(temp_file, save_file);

		/* common memory initialise */
		info->file_size = 0;
		info->file_no++;
		if (g_pcap_option.port_cap.iface_type == PHY)
			iface_type_str = SPP_IFTYPE_NIC_STR;
		else
			iface_type_str = SPP_IFTYPE_RING_STR;
		snprintf(info->compress_file_name,
					COMPRESS_FILENAME_LENGTH - 1,
					"spp_pcap.%s.%s%d.%u.%u.pcap.lz4",
					g_pcap_option.compress_file_date,
					iface_type_str,
					g_pcap_option.port_cap.iface_no,
					info->thread_no,
					info->file_no);
	} else {
		/* close mode */
		/* flush whatever remains within internal buffers */
		if (info->compress_fp == NULL)
			return SPP_RET_OK;
		compress_len = LZ4F_compressEnd(info->ctx, info->outbuff,
					info->outbuf_capacity, NULL);
		if (LZ4F_isError(compress_len)) {
			RTE_LOG(ERR, PCAP, "Failed to end compression: "
					"error %ld\n", compress_len);
		} else {
			file_write(info->compress_fp, info->outbuff,
							compress_len);
			info->file_size += compress_len;
		}
		fclose(info->compress_fp);

		/* file rename */
		memset(temp_file, 0,
			COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH);
		memset(save_file, 0,
			COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH);
		snprintf(temp_file,
		    (COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH) - 1,
		    "%s/%s.tmp", g_pcap_option.compress_file_path,
		    info->compress_file_name);
		snprintf(save_file,
		    (COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH) - 1,
		    "%s/%s", g_pcap_option.compress_file_path,
		    info->compress_file_name);
		rename(temp_file, save_file);

		info->compress_fp = NULL;
		free(info->outbuff);
		return SPP_RET_OK;
	}

	/* file open */
	memset(temp_file, 0,
		COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH);
	snprintf(temp_file,
		(COMPRESS_FILEPATH_LENGTH + COMPRESS_FILENAME_LENGTH) - 1,
		"%s/%s.tmp", g_pcap_option.compress_file_path,
		info->compress_file_name);
	RTE_LOG(INFO, PCAP, "open compress filename=%s\n", temp_file);
	info->compress_fp = fopen(temp_file, "wb");
	if (info->compress_fp == NULL) {
		RTE_LOG(ERR, PCAP, "file open error! filename=%s\n",
						info->compress_file_name);
		free(info->outbuff);
		return SPP_RET_NG;
	}

	/* init lz4 stream */
	ctxCreation = LZ4F_createCompressionContext(&info->ctx, LZ4F_VERSION);
	if (LZ4F_isError(ctxCreation)) {
		RTE_LOG(ERR, PCAP, "LZ4F_createCompressionContext error "
						"(%ld)\n", ctxCreation);
		fclose(info->compress_fp);
		info->compress_fp = NULL;
		free(info->outbuff);
		return SPP_RET_NG;
	}

	/* write compress frame header */
	headerSize = LZ4F_compressBegin(info->ctx, info->outbuff,
					info->outbuf_capacity, &g_kprefs);
	if (LZ4F_isError(headerSize)) {
		RTE_LOG(ERR, PCAP, "Failed to start compression: "
					"error %ld\n", headerSize);
		fclose(info->compress_fp);
		info->compress_fp = NULL;
		free(info->outbuff);
		return SPP_RET_NG;
	}
	RTE_LOG(DEBUG, PCAP, "Buffer size is %ld bytes, header size %ld "
			"bytes\n", info->outbuf_capacity, headerSize);
	if (file_write(info->compress_fp, info->outbuff, headerSize) != 0) {
		fclose(info->compress_fp);
		info->compress_fp = NULL;
		free(info->outbuff);
		return SPP_RET_NG;
	}
	info->file_size = headerSize;

	/* init the common pcap header */
	pcap_h.magic_number = TCPDUMP_MAGIC;
	pcap_h.version_major = PCAP_VERSION_MAJOR;
	pcap_h.version_minor = PCAP_VERSION_MINOR;
	pcap_h.thiszone = 0;
	pcap_h.sigfigs = 0;
	pcap_h.snaplen = PCAP_SNAPLEN_DEFAULT;
	pcap_h.network = PCAP_NETWORK;

	/* pcap header write */
	if (compress_file_write(info, &pcap_h, sizeof(struct pcap_header))
							!= SPP_RET_OK) {
		RTE_LOG(ERR, PCAP, "pcap header write  error!\n");
		fclose(info->compress_fp);
		info->compress_fp = NULL;
		free(info->outbuff);
		return SPP_RET_NG;
	}

	return SPP_RET_OK;
}

/* paket compress */
static int compress_file_packet(struct pcap_mng_info *info,
				struct rte_mbuf *cap_pkt)
{
	unsigned int write_packet_length;
	unsigned int packet_length;
	struct timespec cap_time;
	struct pcap_packet_header pcap_packet_h;
	unsigned int remaining_bytes;
	int bytes_to_write;

	if (info->compress_fp == NULL)
		return SPP_RET_OK;

	/* capture file rool */
	if (info->file_size > g_pcap_option.file_limit) {
		if (compress_file_operation(info, UPDATE_MODE) != SPP_RET_OK)
			return SPP_RET_NG;
	}

	/* cast to packet */
	packet_length = rte_pktmbuf_pkt_len(cap_pkt);

	/* truncate packet if needed */
	write_packet_length = SNAPLEN_REVISION(PCAP_SNAPLEN_DEFAULT,
							packet_length);

	/* get time */
	clock_gettime(CLOCK_REALTIME, &cap_time);

	/* write block header */
	pcap_packet_h.ts_sec = (int32_t)cap_time.tv_sec;
	pcap_packet_h.ts_usec = (int32_t)(cap_time.tv_nsec / 1000);
	pcap_packet_h.write_len = write_packet_length;
	pcap_packet_h.packet_len = packet_length;

	/* compress file write */
	if (compress_file_write(info, &pcap_packet_h.ts_sec,
			sizeof(struct pcap_packet_header)) != SPP_RET_OK) {
		compress_file_operation(info, CLOSE_MODE);
		return SPP_RET_NG;
	}
	info->file_size += sizeof(struct pcap_packet_header);

	/* write content */
	remaining_bytes = write_packet_length;
	while (cap_pkt != NULL && remaining_bytes > 0) {
		/* write file */
		bytes_to_write = SNAPLEN_REVISION(
					rte_pktmbuf_data_len(cap_pkt),
					remaining_bytes);

		/* compress file write */
		if (compress_file_write(info, rte_pktmbuf_mtod(cap_pkt, void*),
						bytes_to_write) != 0) {
			compress_file_operation(info, CLOSE_MODE);
			return SPP_RET_NG;
		}
		cap_pkt = cap_pkt->next;
		remaining_bytes -= bytes_to_write;
		info->file_size += bytes_to_write;
	}

	return SPP_RET_OK;
}

/* receive thread */
static int pcap_proc_receive(int lcore_id)
{
	struct timespec now_time;
	struct tm l_time;
	int buf;
	int nb_rx = 0;
	int nb_tx = 0;
	struct spp_port_info *rx;
	struct rte_mbuf *bufs[MAX_PKT_BURST];
	struct pcap_mng_info *info = &g_pcap_info[lcore_id];
	struct rte_ring *write_ring = g_pcap_option.cap_ring;

	if (g_capture_request[0] == SPP_CAPTURE_IDLE) {
		if (info->status == SPP_CAPTURE_RUNNING) {
			info->status = SPP_CAPTURE_IDLE;
			g_capture_status[0] = SPP_CAPTURE_IDLE;
		}
		return SPP_RET_OK;
	}
	if (info->status == SPP_CAPTURE_IDLE) {
		/* get time */
		clock_gettime(CLOCK_REALTIME, &now_time);
		memset(g_pcap_option.compress_file_date, 0,
					COMPRESS_FILEDATE_LENGTH);
		localtime_r(&now_time.tv_sec, &l_time);
		strftime(g_pcap_option.compress_file_date,
					COMPRESS_FILEDATE_LENGTH,
					"%Y%m%d%H%M%S", &l_time);
		info->status = SPP_CAPTURE_RUNNING;
		g_capture_status[0] = SPP_CAPTURE_RUNNING;
	}

	/* Receive packets */
	rx = &g_pcap_option.port_cap;

	nb_rx = spp_eth_rx_burst(rx->dpdk_port, 0, bufs, MAX_PKT_BURST);
	if (unlikely(nb_rx == 0))
		return SPP_RET_OK;

	/* Write ring packets */
	nb_tx = rte_ring_enqueue_bulk(write_ring, (void *)bufs, nb_rx, NULL);

	/* Discard remained packets to release mbuf */
	if (unlikely(nb_tx < nb_rx)) {
		for (buf = nb_tx; buf < nb_rx; buf++)
			rte_pktmbuf_free(bufs[buf]);
	}

	return SPP_RET_OK;
}

/* write thread */
static int pcap_proc_write(int lcore_id)
{
	int ret = SPP_RET_OK;
	int buf;
	int nb_rx = 0;
	struct rte_mbuf *bufs[MAX_PKT_BURST];
	struct rte_mbuf *mbuf = NULL;
	struct pcap_mng_info *info = &g_pcap_info[lcore_id];
	struct rte_ring *read_ring = g_pcap_option.cap_ring;

	if (g_capture_status[0] == SPP_CAPTURE_IDLE) {
		if (info->status == SPP_CAPTURE_RUNNING) {
			info->status = SPP_CAPTURE_IDLE;
			if (compress_file_operation(info, CLOSE_MODE)
							!= SPP_RET_OK)
				return SPP_RET_NG;
		}
		return SPP_RET_OK;
	}
	if (info->status == SPP_CAPTURE_IDLE) {
		info->status = SPP_CAPTURE_RUNNING;
		if (compress_file_operation(info, INIT_MODE)
						!= SPP_RET_OK) {
			info->status = SPP_CAPTURE_IDLE;
			return SPP_RET_NG;
		}
	}

	/* Read packets */
	nb_rx =  rte_ring_dequeue_bulk(read_ring, (void *)bufs, MAX_PKT_BURST,
									NULL);
	if (unlikely(nb_rx == 0))
		return SPP_RET_OK;

	for (buf = 0; buf < nb_rx; buf++) {
		mbuf = bufs[buf];
		rte_prefetch0(rte_pktmbuf_mtod(mbuf, void *));
		if (compress_file_packet(&g_pcap_info[lcore_id], mbuf)
							!= SPP_RET_OK) {
			RTE_LOG(ERR, PCAP, "capture file write error: "
				"%d (%s)\n", errno, strerror(errno));
			ret = SPP_RET_NG;
			info->status = SPP_CAPTURE_IDLE;
			compress_file_operation(info, CLOSE_MODE);
			break;
		}
	}
	for (buf = nb_rx; buf < nb_rx; buf++)
		rte_pktmbuf_free(bufs[buf]);
	return ret;
}

/* Main process of slave core */
static int
slave_main(void *arg __attribute__ ((unused)))
{
	int ret = SPP_RET_OK;
	unsigned int lcore_id = rte_lcore_id();
	enum spp_core_status status = SPP_CORE_STOP;
	struct pcap_mng_info *pcap_info = &g_pcap_info[lcore_id];

	if (pcap_info->thread_no == 0) {
		RTE_LOG(INFO, PCAP, "Core[%d] Start recive.\n", lcore_id);
		pcap_info->type = TYPE_RECIVE;
	} else {
		RTE_LOG(INFO, PCAP, "Core[%d] Start write(%d).\n",
					lcore_id, pcap_info->thread_no);
		pcap_info->type = TYPE_WRITE;
	}
	RTE_LOG(INFO, PCAP, "Core[%d] Start.\n", lcore_id);
	set_core_status(lcore_id, SPP_CORE_IDLE);

	while ((status = spp_get_core_status(lcore_id)) !=
			SPP_CORE_STOP_REQUEST) {

		if (pcap_info->type == TYPE_RECIVE)
			ret = pcap_proc_receive(lcore_id);
		else
			ret = pcap_proc_write(lcore_id);
		if (unlikely(ret != SPP_RET_OK)) {
			RTE_LOG(ERR, PCAP, "Core[%d] Thread Error.\n",
								lcore_id);
			break;
		}
	}

	set_core_status(lcore_id, SPP_CORE_STOP);
	RTE_LOG(INFO, PCAP, "Core[%d] End.\n", lcore_id);
	return ret;
}

/**
 * Main function
 *
 * Return SPP_RET_NG explicitly if error is occurred.
 */
int
main(int argc, char *argv[])
{
	int ret = SPP_RET_NG;
#ifdef SPP_DEMONIZE
	/* Daemonize process */
	int ret_daemon = daemon(0, 0);
	if (unlikely(ret_daemon != 0)) {
		RTE_LOG(ERR, PCAP, "daemonize is failed. (ret = %d)\n",
				ret_daemon);
		return ret_daemon;
	}
#endif
#ifdef DEBUG_LOG
	rte_log_set_level(RTE_LOGTYPE_USER1, RTE_LOG_DEBUG);
#endif

	/* Signal handler registration (SIGTERM/SIGINT) */
	signal(SIGTERM, stop_process);
	signal(SIGINT,  stop_process);

	while (1) {
		int ret_dpdk = rte_eal_init(argc, argv);
		if (unlikely(ret_dpdk < 0))
			break;

		argc -= ret_dpdk;
		argv += ret_dpdk;

		/* Parse spp_pcap specific parameters */
		int ret_parse = parse_app_args(argc, argv);
		if (unlikely(ret_parse != 0))
			break;

		/* Get lcore id of main thread to set its status after */
		g_main_lcore_id = rte_lcore_id();

		/* set manage address */
		if (spp_set_mng_data_addr(&g_startup_param,
					  &g_iface_info,
					  g_component_info,
					  g_core_info,
					  g_capture_request,
					  g_capture_status,
					  &g_backup_info,
					  g_main_lcore_id) < 0) {
			RTE_LOG(ERR, PCAP, "manage address set is failed.\n");
			break;
		}

		int ret_mng = init_mng_data();
		if (unlikely(ret_mng != 0))
			break;

		/* Setup connection for accepting commands from controller */
		int ret_command_init = spp_command_proc_init(
				g_startup_param.server_ip,
				g_startup_param.server_port);
		if (unlikely(ret_command_init != SPP_RET_OK))
			break;

		/* capture port setup */
		struct spp_port_info *port_cap = &g_pcap_option.port_cap;
		struct spp_port_info *port_info = get_iface_info(
						port_cap->iface_type,
						port_cap->iface_no);
		if (port_info == NULL) {
			RTE_LOG(ERR, PCAP, "caputre port undefined.\n");
			break;
		}
		if (port_cap->iface_type == PHY) {
			if (port_info->iface_type != UNDEF)
				port_cap->dpdk_port = port_info->dpdk_port;
			else {
				RTE_LOG(ERR, PCAP, "caputre port undefined."
					"(phy:%d)\n", port_cap->iface_no);
				break;
			}
		} else {
			if (port_info->iface_type != UNDEF) {
				ret = add_ring_pmd(port_info->iface_no);
				if (ret == SPP_RET_NG) {
					RTE_LOG(ERR, PCAP, "caputre port "
						"undefined.(ring:%d)\n",
						port_cap->iface_no);
					break;
				}
				port_cap->dpdk_port = ret;
			} else {
				RTE_LOG(ERR, PCAP, "caputre port "
						"undefined.(ring:%d)\n",
						port_cap->iface_no);
				break;
			}
		}

		/* create ring */
		char ring_name[PORT_STR_SIZE];
		memset(ring_name, 0x00, PORT_STR_SIZE);
		snprintf(ring_name, PORT_STR_SIZE,  "cap_ring_%d",
						g_startup_param.client_id);
		g_pcap_option.cap_ring = rte_ring_create(ring_name,
					rte_align32pow2(NUM_MBUFS_DEFAULT),
					rte_socket_id(), 0);
		if (g_pcap_option.cap_ring == NULL) {
			RTE_LOG(ERR, PCAP, "ring create error(%s).\n",
						rte_strerror(rte_errno));
			break;
		}

		/* Start worker threads of recive or write */
		unsigned int lcore_id = 0;
		unsigned int thread_no = 0;
		RTE_LCORE_FOREACH_SLAVE(lcore_id) {
			g_core_info[lcore_id].core[0].num = 1;
			g_pcap_info[lcore_id].thread_no = thread_no++;
			rte_eal_remote_launch(slave_main, NULL, lcore_id);
		}

		/* Set the status of main thread to idle */
		g_core_info[g_main_lcore_id].status = SPP_CORE_IDLE;
		int ret_wait = check_core_status_wait(SPP_CORE_IDLE);
		if (unlikely(ret_wait != 0))
			break;

		/* Start secondary */
		set_all_core_status(SPP_CORE_FORWARD);
		RTE_LOG(INFO, PCAP, "[Press Ctrl-C to quit ...]\n");

		/* Backup the management information after initialization */
		backup_mng_info(&g_backup_info);

		/* Enter loop for accepting commands */
		int ret_do = 0;
		while (likely(g_core_info[g_main_lcore_id].status !=
				SPP_CORE_STOP_REQUEST)) {
			/* Receive command */
			ret_do = spp_command_proc_do();
			if (unlikely(ret_do != SPP_RET_OK))
				break;

			sleep(1);
		}

		if (unlikely(ret_do != SPP_RET_OK)) {
			set_all_core_status(SPP_CORE_STOP_REQUEST);
			break;
		}

		ret = SPP_RET_OK;
		break;
	}

	/* Finalize to exit */
	if (g_main_lcore_id == rte_lcore_id()) {
		g_core_info[g_main_lcore_id].status = SPP_CORE_STOP;
		int ret_core_end = check_core_status_wait(SPP_CORE_STOP);
		if (unlikely(ret_core_end != 0))
			RTE_LOG(ERR, PCAP, "Core did not stop.\n");

		/* capture write ring free */
		if (g_pcap_option.cap_ring != NULL)
			rte_ring_free(g_pcap_option.cap_ring);
	}


	RTE_LOG(INFO, PCAP, "spp_pcap exit.\n");
	return ret;
}
