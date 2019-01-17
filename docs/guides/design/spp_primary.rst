..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation
    Copyright(c) 2018-2019 Nippon Telegraph and Telephone Corporation


.. _spp_overview_design_spp_primary:

SPP Primary
===========

SPP is originally derived from
`Client-Server Multi-process Example
<https://doc.dpdk.org/guides/sample_app_ug/multi_process.html#client-server-multi-process-example>`_
of
`Multi-process Sample Application
<https://doc.dpdk.org/guides/sample_app_ug/multi_process.html>`_
in DPDK's sample applications.
``spp_primary`` is a server process for other secondary processes and
basically working as described in
"How the Application Works" section of the sample application.

However, there are also differences between ``spp_primary`` and
the server process of the sample application.
``spp_primary`` has no limitation of the number of secondary processes.
It does not work for packet forwaring, but just provide rings and memory pools
for secondary processes.
