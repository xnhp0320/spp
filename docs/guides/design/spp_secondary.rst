..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation


.. _spp_design_spp_sec:

SPP Secondary
=============

SPP secondary process is a worker process in client-server multp-process
application model. Basically, the role of secondary process is to connenct
each of application running on host, containers or VMs for packet forwarding.
Spp secondary process forwards packets from source port to destination port
with DPDK's high-performance forwarding mechanizm. In other word, it behaves
as a cable to connect two patches ports.

All of secondary processes are able to attach ring PMD and vhost PMD ports
for sending or receiving packets with other processes. Ring port is used to
communicate with a process running on host or container if it is implemented
as secondary process to access shared ring memory.
Vhost port is used for a process on container or VM and implemented as primary
process, and no need to access shared memory of SPP primary.

In addition to the basic forwarding, SPP secondary process provides several
networking features. One of the typical example is packet cauture.
``spp_nfv`` is the simplest SPP secondary and used to connect two of processes
or other feature ports including PCAP PMD port. PCAP PMD is to dump packets to
a file or retrieve from.

There are more specific or funcional features than ``spp_nfv``. ``spp_vf`` is
a simple pseudo SR-IOV feature for classifying or merging packets.
``spp_mirror`` is to duplicate incoming packets to several destination ports.


.. _spp_design_spp_sec_nfv:

spp_nfv
-------

``spp_nfv`` is the simplest SPP secondary to connect two of processes or other
feature ports. Each of ``spp_nfv`` processes has a list of entries including
source and destination ports, and forwards packets by referring the list.
It means that one ``spp_nfv`` might have several forwarding paths, but
throughput is gradually decreased if it has too much paths.
This list is implemented as an array of ``port`` structure and named
``ports_fwd_array``. The index of ``ports_fwd_array`` is the same as unique
port ID.

.. code-block:: c

    struct port {
      int in_port_id;
      int out_port_id;
      ...
    };
    ...

    /* ports_fwd_array is an array of port */
    static struct port ports_fwd_array[RTE_MAX_ETHPORTS];

:numref:`figure_design_spp_sec_nfv_port_fwd_array` describes an example of
forwarding between ports. In this case, ``spp_nfv`` is responsible for
forwarding from ``port#0`` to ``port#2``. You notice that each of ``out_port``
entry has the destination port ID.

.. _figure_design_spp_sec_nfv_port_fwd_array:

.. figure:: ../images/design/spp_design_spp_sec_nfv.*
   :width: 80%

   Forwarding by referring ports_fwd_array

``spp_nfv`` consists of main thread and worker thread to update the entry
while running the process. Main thread is for waiting user command for
updating the entry. Worker thread is for dedicating packet forwarding.
:numref:`figure_design_spp_sec_nfv_threads` describes tasks in each of
threads. Worker thread is launched from main thread after initialization.
In worker thread, it starts forwarding if user send forward command and
main thread accepts it.

.. _figure_design_spp_sec_nfv_threads:

.. figure:: ../images/design/spp_design_spp_sec_nfv_threads.*
   :width: 70%

   Main thread and worker thread in spp_nfv
