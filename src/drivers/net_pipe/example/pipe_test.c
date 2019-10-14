/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019 Nippon Telegraph and Telephone Corporation
 */

#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_eth_ring.h>
#include <rte_bus_vdev.h>

#define RING_SIZE 128
#define BURST_SIZE 32
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 512

#define PKTMBUF_POOL_NAME "MProc_pktmbuf_pool"

static int tx_first = 0;
static char *device;
static char *devargs = NULL;
static int force_quit;

static struct option lopts[] = {
	{"send", no_argument, &tx_first, 1},
	{"create", required_argument, NULL, 'c'},
	{NULL, 0, 0, 0}
};

static const struct rte_eth_conf port_conf_default = {
	.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};

static int
parse_args(int argc, char *argv[])
{
        int c;

        while ((c = getopt_long(argc, argv, "", lopts, NULL)) != -1) {
                switch (c) {
                case 0:
                        /* long option */
                        break;
		case 'c':
			/* --create */
			devargs = optarg;
			break;
                default:
                        /* invalid option */
			return -1;
                }
        }

	if (optind != argc - 1) {
		return -1;
	}

	device = argv[optind];

        return 0;
}

static void
signal_handler(int signum)
{
	printf("signel %d recieved\n", signum);
	force_quit = 1;
}

int
main(int argc, char *argv[])
{
	int ret;
	uint16_t port_id;
	uint16_t nb_ports;
	struct rte_mempool *mbuf_pool = NULL;
        struct rte_mbuf *m;
	uint16_t nb_tx;
	struct rte_mbuf *bufs[BURST_SIZE];
	uint16_t nb_rx;
	struct rte_eth_conf port_conf = port_conf_default;
	struct rte_eth_stats stats;
	int is_pipe = 0;
	struct timeval t0, t1;
	long total;

	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "EAL initialization failed\n");
	}
	argc -= ret;
	argv += ret;

	ret = parse_args(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE,
			"usage: pipe_test <eal options> -- "
			"[--send] [--create devargs] vdev\n");
	}
	printf("device: %s tx_first: %d devargs: %s\n", device, tx_first, devargs);

	if (strncmp("net_pipe", device, 8) == 0) {
		is_pipe = 1;
		if (rte_eal_process_type() != RTE_PROC_SECONDARY) {
			rte_exit(EXIT_FAILURE, "must be secondary\n");
		}
	}

	if (devargs) {
		/* --create */
		ret = rte_eth_dev_get_port_by_name(device, &port_id);
		if (ret == 0) {
			rte_exit(EXIT_FAILURE,
				"%s already exists.\n", device);
		}
		ret = rte_vdev_init(device, devargs);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				"%s %s create failed.\n", device, devargs);
		}
	}

	ret = rte_eth_dev_get_port_by_name(device, &port_id);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "device not found\n");
	}
	printf("port_id: %u\n", (unsigned)port_id);

        nb_ports = rte_eth_dev_count_avail();
	/* just infomation */
        printf("num port: %u\n", (unsigned)nb_ports);

	if (is_pipe) {
		mbuf_pool = rte_mempool_lookup(PKTMBUF_POOL_NAME);
	} else {
                mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
			rte_socket_id());
	}
	if (mbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Cannot get mbuf pool\n");
	}

	ret = rte_eth_dev_configure(port_id, 1, 1, &port_conf);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "rte_eth_dev_configure failed\n");
	}

	ret = rte_eth_rx_queue_setup(port_id, 0, RING_SIZE,
		rte_eth_dev_socket_id(port_id), NULL, mbuf_pool);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup failed\n");
	}

	ret = rte_eth_tx_queue_setup(port_id, 0, RING_SIZE,
			rte_eth_dev_socket_id(port_id), NULL);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup failed\n");
	}

	ret = rte_eth_dev_start(port_id);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "rte_eth_dev_start failed\n");
	}

	force_quit = 0;
	signal(SIGINT, signal_handler);

	if (tx_first) {
		/* send a packet */
		m = rte_pktmbuf_alloc(mbuf_pool);
		if (m == NULL) {
			fprintf(stderr, "rte_pktmbuf_alloc failed\n");
			goto out;
		}

		nb_tx = rte_eth_tx_burst(port_id, 0, &m, 1);
		if (nb_tx != 1) {
			fprintf(stderr, "can not send a packet\n");
			rte_pktmbuf_free(m);
			goto out;
		}
		printf("send a packet\n");
		gettimeofday(&t0, NULL);
	}

	/* recieve and send a packet */
	while (!force_quit) {
		nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);
		if (nb_rx > 0) {
			nb_tx = rte_eth_tx_burst(port_id, 0, bufs, nb_rx);
			if (nb_tx < nb_rx) {
				fprintf(stderr, "can't send. recv: %u send: %u\n",
						nb_rx, nb_tx);
				break;
			}
		}
	}

	gettimeofday(&t1, NULL);

	ret = rte_eth_stats_get(port_id, &stats);
	if (ret == 0) {
		printf("ipackets: %lu\n", stats.ipackets);
		printf("opackets: %lu\n", stats.opackets);
		printf("ierrors: %lu\n", stats.ierrors);
		printf("oerrors: %lu\n", stats.oerrors);
		if (tx_first) {
			total = (t1.tv_sec - t0.tv_sec) * 1000000 \
				+ t1.tv_usec - t0.tv_usec;
			printf("%ld us: %.2f packet/s\n", total,
				(double)stats.ipackets / total * 1000000);
		}
	}

out:
	rte_eth_dev_stop(port_id);
	rte_eth_dev_close(port_id);

	rte_vdev_uninit(device);

	return 0;
}
