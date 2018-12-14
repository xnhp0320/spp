..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _spp_pcap_design:

Design
======

.. _spp_pcap_design_outline:


Design Overview
---------------

This section outlines design of ``spp_pcap``.

When launched, ``spp_pcap`` assigns master core as ``receiver`` thread and
the rest of the cores are assigned ``writer`` thread(s). Writing packets into
storage takes much cpu power to write packets. If writing is done by only
one core, then there is a risk of packet be dropped. That is the reason
why there can be multiple ``writer`` threads. Each ``writer`` thread has
unique integer number which is used to create capture file name.
When launched, ``spp_pcap`` connect to the ``port`` which is specified
in startup parameter. Note that ``vhost`` can not be specified as ``port``.
When spp_pcap has launched, initially it is not capturing.

With ``start`` command, you can start capturing.
Incoming packets are received by ``receiver`` thread and it is transferred to
``writer`` thread(s) via multi-producer/multi-consumer ring.
Multi-producer/multi-consumer ring is the ring which multiple producers
can enqueue and multiple consumers can dequeue. When those packets are
received by ``writer`` thread(s), it will be compressed using LZ4 library and
then be written to storage. In case more than 1 cores are assigned,
incoming packets are written into storage per core basis so packet capture file
will be divided per core.
When ``spp_pcap`` has already been started, ``start`` command cannot
be accepted.

With ``stop`` command, capture will be stopped. When spp_pcap has already
been stopped, ``stop`` command cannot be accepted.

With ``exit`` command, ``spp_pcap`` exits the program. ``exit`` command
during started state, stops capturing and then exits the program.

With ``status`` command, status related to ``spp_pcap`` is shown.

The following depicts the internal structure of ``spp_pcap``.

.. figure:: ../images/spp_pcap/spp_pcap_design.*
    :width: 75%

    spp_pcap internal structure

.. _spp_pcap_design_output_file_format:

Startup options
---------------

Like primary process, ``spp_pcap`` has two kinds of options. One is for DPDK,
the other is ``spp_pcap``.

``spp_pcap`` specific options are:

 * -client-id: client id which can be seen as secondary ID from spp.py
 * -s: IPv4 address and port for spp-ctl
 * -i: port to which spp_pcap attached with
 * --output: Output file path where capture files are written.\
   When this parameter is omitted,``/tmp`` is used.

The output file format is as following:

    spp_pcap.YYYMMDDhhmmss.[port_name].[write_core_number(1,2...)]
    .[sequence_number(0,1..)].pcap.lz4

YYYYMMDDhhmmss is the time when ``spp_pcap`` receives ``start`` command.

The example file name is as follwoing:

    /tmp/spp_pcap.20181108110600.ring0.1.2.pcap.lz4

When ``writer`` core is  writing the file, ``.tmp`` extension is appended at
the end of the file name.
The example is as following:

    /tmp/spp_pcap.20181108110600.ring0.1.2.pcap.lz4.tmp

 * -port_name: port_name which can be specified as either of phy:N or \
   ring:N.When used as part of file name : is removed to avoid misconversion.

 * --limit_file_option: When written size to file exceeded this value,
``spp_pcap`` closes current writing file and then newly open the file with
incrementing sequence_number and again start writing. When this parameter is
omitted, ``1073741824(1GB)`` is used. This feature is not aimed to be file
rotation, captured files are not deleted automatically.
