/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation
 */

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include <rte_mbuf.h>
#include <rte_log.h>
#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>

#include "ringlatencystats.h"

#define NS_PER_SEC 1E9

/**
 * TODO(Ogasawara) change log names.
 *  After a naming convention decision.
 */
#define RTE_LOGTYPE_SPP_RING_LATENCY_STATS RTE_LOGTYPE_USER2

#ifdef SPP_RINGLATENCYSTATS_ENABLE

enum spp_return_value {
	SPP_RET_OK = 0,  /**< succeeded */
	SPP_RET_NG = -1, /**< failed */
};

/** ring latency statistics information */
struct ring_latency_stats_info {
	uint64_t timer_tsc;     /**< sampling interval counter */
	uint64_t prev_tsc;      /**< previous time */
	struct spp_ringlatencystats_ring_latency_stats stats;
				/**< ring latency statistics list */
};

/** sampling interval */
static uint64_t g_samp_intvl;

/** ring latency statistics information instance */
static struct ring_latency_stats_info *g_stats_info;

/** number of ring latency statistics */
static uint16_t g_stats_count;

/* clock cycles per nano second */
static inline uint64_t
cycles_per_ns(void)
{
	return rte_get_timer_hz() / NS_PER_SEC;
}

int
spp_ringlatencystats_init(uint64_t samp_intvl, uint16_t stats_count)
{
	/* allocate memory for ring latency statistics information */
	g_stats_info = rte_zmalloc(
			"global ring_latency_stats_info",
			sizeof(struct ring_latency_stats_info) * stats_count,
			0);
	if (unlikely(g_stats_info == NULL)) {
		RTE_LOG(ERR, SPP_RING_LATENCY_STATS, "Cannot allocate memory "
				"for ring latency stats info\n");
		return SPP_RET_NG;
	}

	/* store global information for ring latency statistics */
	g_samp_intvl = samp_intvl * cycles_per_ns();
	g_stats_count = stats_count;

	RTE_LOG(DEBUG, SPP_RING_LATENCY_STATS,
			"g_samp_intvl=%lu, g_stats_count=%hu, "
			"cpns=%lu, NS_PER_SEC=%f\n",
			g_samp_intvl, g_stats_count,
			cycles_per_ns(), NS_PER_SEC);

	return SPP_RET_OK;
}

void
spp_ringlatencystats_uninit(void)
{
	/* free memory for ring latency statistics information */
	if (likely(g_stats_info != NULL)) {
		rte_free(g_stats_info);
		g_stats_count = 0;
	}
}

void
spp_ringlatencystats_add_time_stamp(int ring_id,
			struct rte_mbuf **pkts, uint16_t nb_pkts)
{
	unsigned int i;
	uint64_t diff_tsc, now;
	struct ring_latency_stats_info *stats_info = &g_stats_info[ring_id];

	for (i = 0; i < nb_pkts; i++) {

		/* get tsc now */
		now = rte_rdtsc();

		/* calculate difference from the previous processing time */
		diff_tsc = now - stats_info->prev_tsc;
		stats_info->timer_tsc += diff_tsc;

		/* when it is over sampling interval */
		/* set tsc to mbuf::timestamp */
		if (unlikely(stats_info->timer_tsc >= g_samp_intvl)) {
			RTE_LOG(DEBUG, SPP_RING_LATENCY_STATS,
					"Set timestamp. ring_id=%d, "
					"pkts_index=%u, timestamp=%lu\n",
					ring_id, i, now);
			pkts[i]->timestamp = now;
			stats_info->timer_tsc = 0;
		}

		/* update previous tsc */
		stats_info->prev_tsc = now;
	}
}

void
spp_ringlatencystats_calculate_latency(int ring_id,
			struct rte_mbuf **pkts, uint16_t nb_pkts)
{
	unsigned int i;
	uint64_t now;
	int64_t latency;
	struct ring_latency_stats_info *stats_info = &g_stats_info[ring_id];

	now = rte_rdtsc();
	for (i = 0; i < nb_pkts; i++) {
		if (likely(pkts[i]->timestamp == 0))
			continue;

		/* when mbuf::timestamp is not zero */
		/* calculate latency */
		latency = (uint64_t)floor((now - pkts[i]->timestamp) /
				cycles_per_ns());
		if (likely(latency < SPP_RINGLATENCYSTATS_STATS_SLOT_COUNT-1))
			stats_info->stats.slot[latency]++;
		else
			stats_info->stats.slot[
					SPP_RINGLATENCYSTATS_STATS_SLOT_COUNT
					-1]++;
	}
}

int
spp_ringlatencystats_get_count(void)
{
	return g_stats_count;
}

void
spp_ringlatencystats_get_stats(int ring_id,
		struct spp_ringlatencystats_ring_latency_stats *stats)
{
	struct ring_latency_stats_info *stats_info = &g_stats_info[ring_id];

	rte_memcpy(stats, &stats_info->stats,
			sizeof(struct spp_ringlatencystats_ring_latency_stats));
}

#endif /* SPP_RINGLATENCYSTATS_ENABLE */
