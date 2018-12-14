..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _spp_pcap_explain:

spp_pcap
========

The following sections provide some explanation of the code.

Initializing
------------

A manager thread of ``spp_pcap`` initialize eal by ``rte_eal_init()``.
Then each of component threads are launched by
``rte_eal_remote_launch()``.


.. code-block:: c

    /* spp_pcap.c */
    int ret_dpdk = rte_eal_init(argc, argv);

    /* Start worker threads of classifier and forwarder */
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        g_core_info[lcore_id].core[0].num = 1;
        g_pcap_info[lcore_id].thread_no = thread_no++;
        rte_eal_remote_launch(slave_main, NULL, lcore_id);
    }


Main function of slave thread
-----------------------------

``slave_main()`` is called from ``rte_eal_remote_launch()``.
It call ``pcap_proc_receive()`` or ``pcap_proc_write()``
depending on the core assignment.
``pcap_proc_write();`` provides function for ``receive``,
and ``pcap_proc_write();`` provides function for ``write``.

.. code-block:: c

    /* spp_pcap.c */
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

Receive Pakcet
--------------

``pcap_proc_receive()`` is the function to realize
receiving incoming packets. This function is called in the while loop and
receive packets. Everytime it receves packet via ``spp_eth_rx_burst()``, then
it enqueue those packet into the ring using ``rte_ring_enqueue_bulk()``.
Those packets are trnsfered to ``write`` cores via the ring.


.. code-block:: c

        /* spp_pcap.c */
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


Write Packet
------------

In ``pcap_proc_write()``, it dequeue packets from ring.Then it writes to
storage after data compression using LZ4 libraries. ``compress_file_packet``
is the function to write packet with LZ4. LZ4 is lossless compression
algorithm, providing compression speed > 500 MB/s per core, scalable with
multi-cores CPU. It features an extremely fast decoder, with speed in multiple
GB/s per core, typically reaching RAM speed limits on multi-core systems.
Please see details in
`LZ4
<https://github.com/lz4/lz4>`_

.. code-block:: c

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
