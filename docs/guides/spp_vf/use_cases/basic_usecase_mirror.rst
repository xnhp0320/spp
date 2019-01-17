.. SPDX-License-Identifier: BSD-3-Clause
   Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_mirror_use_cases_basic:

Mirroring Packets from NIC
==========================

This usecase uses two hosts. ``spp_mirror`` is running on localhost. Remote host
sends ARP packets to localhost by using ping command. ``spp_mirror`` duplicates
and sends packets to destination ports.

Network Configuration
---------------------

Detailed configuration is described in
:numref:`figure_spp_mirror_use_cases_nw_config`.
In this diagram, incoming packets from ``phy:0`` are mirrored.
In ``spp_mirror`` process, worker thread ``mir1`` copies incoming packets and
sends to two destinations ``phy:1`` and ``phy:2``.

.. _figure_spp_mirror_use_cases_nw_config:

.. figure:: ../../images/spp_vf/basic_usecase_mirror_nwconfig.*
     :width: 80%

     Network configuration of mirroring


Setup SPP
---------

Change directory to spp and confirm that it is already compiled.

.. code-block:: console

    $ cd /path/to/spp

Launch ``spp-ctl`` before launching SPP primary and secondary processes.
You also need to launch ``spp.py``  if you use ``spp_mirror`` from CLI.
``-b`` option is for binding IP address to communicate other SPP processes,
but no need to give it explicitly if ``127.0.0.1`` or ``localhost`` .

.. code-block:: console

    # terminal#1
    # Launch spp-ctl
    $ python3 ./src/spp-ctl/spp-ctl -b 192.168.1.100

.. code-block:: console

    # terminal#2
    # Launch SPP CLI
    $ python ./src/spp.py -b 192.168.1.100

Start ``spp_primary`` with core list option ``-l 1``.

.. code-block:: console

   # terminal#3
   $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
       -l 1 -n 4 \
       --socket-mem 512,512 \
       --huge-dir=/run/hugepages/kvm \
       --proc-type=primary \
       -- \
       -p 0x07 -n 10 -s 192.168.1.100:5555


Launch spp_mirror
~~~~~~~~~~~~~~~~~

Run secondary process ``spp_mirror``.

.. code-block:: console

    # terminal#4
    $ sudo ./src/mirror/x86_64-native-linuxapp-gcc/app/spp_mirror \
     -l 0,2 -n 4 --proc-type=secondary \
     -- \
     --client-id 1 \
     -s 192.168.1.100:6666 \

Start mirror component with ``CORE_ID`` 2.

.. code-block:: console

    # Start component on CORE_ID 2
    spp > mirror 1; component start mir1 2 mirror

Add ``phy:0`` as rx ports and add ``phy:1`` and ``phy:2`` as tx port
to mirror.

.. code-block:: none

   # add ports to mir1
   spp > mirror 1; port add phy:0 rx mir1
   spp > mirror 1; port add phy:1 tx mir1
   spp > mirror 1; port add phy:2 tx mir1


Confirm Original Packet is Duplicated
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To check sent packets are mirrored, you run tcpdump for ``ens1`` and ``ens2``
first. As you run ping for ``ens0``, you will see the same ARP requests trying
to resolve ``192.168.140.21`` on terminal 1 and 2.

.. code-block:: console

   # terminal#1 at host1
   # capture on ens1
   $ sudo tcpdump -i ens1
    tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
    listening on ens1, link-type EN10MB (Ethernet), capture size 262144 bytes
    21:18:44.183261 ARP, Request who-has 192.168.140.21 tell R740n15, length 28
    21:18:45.202182 ARP, Request who-has 192.168.140.21 tell R740n15, length 28
    ...

.. code-block:: console

   # terminal#2 at host1
   # capture on ens2
   $ sudo tcpdump -i ens2
    tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
    listening on ens2, link-type EN10MB (Ethernet), capture size 262144 bytes
    21:18:44.183261 ARP, Request who-has 192.168.140.21 tell R740n15, length 28
    21:18:45.202182 ARP, Request who-has 192.168.140.21 tell R740n15, length 28
    ...

Start to send ARP request with ping.

.. code-block:: console

   # terminal#3 at host1
   # send packet from NIC0
   $ ping 192.168.140.21 -I ens0


Stop Mirroring
~~~~~~~~~~~~~~

Delete ports for components.

.. code-block:: none

   # Delete port for mir1
   spp > mirror 1; port del phy:0 rx mir1
   spp > mirror 1; port del phy:1 tx mir1
   spp > mirror 1; port del phy:2 tx mir1

Next, stop components.

.. code-block:: console

   # Stop mirror
   spp > mirror 1; component stop mir1 2 mirror

   spp > mirror 1; status
   Basic Information:
     - client-id: 1
     - ports: [phy:0, phy:1]
   Components:
     - core:2 '' (type: unuse)

Finally, terminate ``spp_mirror`` to finish this usecase.

.. code-block:: console

    spp > mirror 1; exit
