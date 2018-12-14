..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _spp_pcap_overview:

Overview
========

This section describes an overview of SPP's extensions, ``spp_pcap``.
SPP provides a connectivity between VM and NIC as a virtual patch panel.
However, for more practical use, operator and/or developer needs to capture
packets. For such use, spp_pcap provides packet capturing feature from
specific port. It is aimed to capture up to 10Gbps packets.

``spp_pcap`` is a SPP secondary process for capturing packets from specific
``port``. :numref:`figure_spp_pcap_overview` shows an overview of use of
``spp_pcap`` in which ``spp_pcap`` process receives packets from ``phy:0``
for capturing.

``spp_pcap`` provides packet capturing capability as a SPP secondary process.
``spp_pcap`` has one manager thread like spp_vf, and has two types of worker
threads unlike spp_vf.


.. _figure_spp_pcap_overview:

.. figure:: ../images/spp_pcap/spp_pcap_overview.*
   :width: 70%

   Overview of spp_pcap
