..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_vf_design:

Design
======

.. _spp_vf_design_port:

Ports
-----

Both of ``spp_vf`` and ``spp_mirror`` support three types of port,
``phy``, ``ring`` and ``vhost``.
``phy`` port is used to retrieve packets from specific physical NIC or sent to.
``ring`` is basically used to connect a process or a thread to make a network
path between them.
``vhost`` is used to forward packets from a VM or sent to.

.. _spp_vf_design_spp_vf:

spp_vf
------

``spp_vf`` is a kind of secondary process and consists of several
threads called component.
There are three types of components, ``forwarder``,
``merger`` and ``classifier``.

.. figure:: ../images/spp_vf/spp_vf_overview.*
    :width: 75%

    SPP VF components

Forwarder
^^^^^^^^^

Simply forwards packets from rx to tx port.

Merger
^^^^^^

Receives packets from multiple rx ports to aggregate
packets and sends to a desctination port.

Classifier
^^^^^^^^^^

Sends packets to multiple tx ports based on entries of
MAC address and destination port in a classifier table.
This component also supports VLAN tag.

For VLAN addressing, classifier has other tables than defalut.
Classifier prepares tables for each of VLAN ID and decides
which of table is referred
if TPID (Tag Protocol Indetifier) is included in a packet and
equals to 0x8100 as defined in IEEE 802.1Q standard.

.. _spp_vf_design_spp_mirror:

spp_mirror
----------

``spp_mirror`` is another kind of secondary process. The keyword ``mirror``
means that it duplicates incoming packets and forwards to additional
destination.
It supports only one type of component called ``mirror`` for duplicating.
In :numref:`figure_spp_mirror_design`, incoming packets are duplicated with
``mirror`` component and sent to original and additional destinations.

.. _figure_spp_mirror_design:

.. figure:: ../images/spp_vf/spp_mirror_design.*
    :width: 45%

    Spp_mirror component

Mirror
^^^^^^

``mirror`` component has one ``rx`` port and two ``tx`` ports. Incoming packets
from ``rx`` port are duplicated and sent to each of ``tx`` ports.

In general, copying packet is time-consuming because it requires to make a new
region on memory space. Considering to minimize impact for performance,
``spp_mirror`` provides a choice of copying methods, ``shallowocopy`` or
``deepcopy``.
The difference between those methods is ``shallowocopy`` does not copy whole of
packet data but share without header actually.
``shallowcopy`` is to share mbuf between packets to get better performance
than ``deepcopy``, but it should be used for read only for the packet.

.. note::

    ``shallowcopy`` calls ``rte_pktmbuf_clone()`` internally and
    ``deepcopy`` create a new mbuf region.

You should choose ``deepcopy`` if you use VLAN feature to make no change for
original packet while copied packet is modified.

.. _spp_vf_design_spp_pcap:

spp_pcap
--------

When launched, ``spp_pcap`` assigns master core as ``receiver`` thread and
the rest of the cores are assigned for ``writer`` threads.
You should have enough cores if you need to capture large amount of packets.

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

In :numref:`figure_spp_pcap_design`,
the internal structure of ``spp_pcap`` is shown.

.. figure:: ../images/spp_pcap/spp_pcap_design.*
    :width: 75%

    spp_pcap internal structure

.. _spp_pcap_design_output_file_format:

The figure shows the case when ``spp_pcap`` is connected with ``phy:0``.
There is only one ``receiver`` thread and multiple ``writer`` threads.
Each ``writer`` writes packets into file.
Once exceeds maximum file size ,
it creates new file so that multiple output files are created.


Startup options
^^^^^^^^^^^^^^^

Like primary process, ``spp_pcap`` has two kinds of options. One is for DPDK,
the other is ``spp_pcap``.

``spp_pcap`` specific options are:

 * -client-id: client id which can be seen as secondary ID from spp.py.
 * -s: IPv4 address and port for spp-ctl.
 * -i: port to which spp_pcap attached with.
 * --output: Output file path
   where capture files are written.\
   When this parameter is omitted,
   ``/tmp`` is used.
 * --port_name: port_name which can be specified as
   either of phy:N or \
   ring:N.
   When used as part of file name ``:`` is removed to avoid misconversion.
 * --limit_file_option: Maximum size of a capture file.
   Default value is ``1GiB``.

The output file format is as following:

.. code-block:: none

    spp_pcap.YYYYMMDDhhmmss.[port_name].[wcore_num]
    wcore_num is write core number which starts with 1

Each ``writer`` thread has
unique integer number which is used to determine the name of capture file.
YYYYMMDDhhmmss is the time when ``spp_pcap`` receives ``start`` command.
This example shows that ``receiver`` thread receives ``start`` command at
20181108110600.
Port is ring:0, wcore_num is 1 and sequential number is 2.

Until writing is finished, packets are stored into temporary file.
The following is the example.

.. code-block:: none

    /tmp/spp_pcap.20181108110600.ring0.1.2.pcap.lz4.tmp

The example is as following:

.. code-block:: none

    /tmp/spp_pcap.20181108110600.ring0.1.2.pcap.lz4.tmp

Captured files are not deleted automatically
because file rotation is not supported.
