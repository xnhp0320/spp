..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation
    Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation

spp_nfv
=======

.. _single_spp_nfv:

Single spp_nfv
--------------

The most simple usecase mainly for testing performance of packet
forwarding on host.
One ``spp_nfv`` and two physical ports.

In this usecase, try to configure two senarios.

- Configure ``spp_nfv`` as L2fwd
- Configure ``spp_nfv`` for Loopback


First of all, Check the status of ``spp_nfv`` from SPP CLI.

.. code-block:: console

    spp > nfv 1; status
    - status: idling
    - ports:
      - phy:0
      - phy:1

This status message explains that ``nfv 1`` has two physical ports.


Configure spp_nfv as L2fwd
~~~~~~~~~~~~~~~~~~~~~~~~~~

Assing the destination of ports with ``patch`` subcommand and
start forwarding.
Patch from ``phy:0`` to ``phy:1`` and ``phy:1`` to ``phy:0``,
which means it is bi-directional connection.

.. code-block:: console

    spp > nfv 1; patch phy:0 phy:1
    Patch ports (phy:0 -> phy:1).
    spp > nfv 1; patch phy:1 phy:0
    Patch ports (phy:1 -> phy:0).
    spp > nfv 1; forward
    Start forwarding.

Confirm that status of ``nfv 1`` is updated to ``running`` and ports are
patches as you defined.

.. code-block:: console

    spp > nfv 1; status
    - status: running
    - ports:
      - phy:0 -> phy:1
      - phy:1 -> phy:0

.. _figure_spp_nfv_as_l2fwd:

.. figure:: ../images/setup/use_cases/spp_nfv_l2fwd.*
   :width: 60%

   spp_nfv as l2fwd


Stop forwarding and reset patch to clear configuration.
``patch reset`` is to clear all of patch configurations.

.. code-block:: console

    spp > nfv 1; stop
    Stop forwarding.
    spp > nfv 1; patch reset
    Clear all of patches.


Configure spp_nfv for Loopback
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Patch ``phy:0`` to ``phy:0`` and ``phy:1`` to ``phy:1``
for loopback.

.. code-block:: console

    spp > nfv 1; patch phy:0 phy:0
    Patch ports (phy:0 -> phy:0).
    spp > nfv 1; patch phy:1 phy:1
    Patch ports (phy:1 -> phy:1).
    spp > nfv 1; forward
    Start forwarding.


Dual spp_nfv
------------

Use case for testing performance of packet forwarding
with two ``spp_nfv`` on host.
Throughput is expected to be better than
:ref:`Single spp_nfv<single_spp_nfv>`
usecase because bi-directional forwarding of single ``spp_nfv`` is shared
with two of uni-directional forwarding between dual ``spp_nfv``.

In this usecase, configure two senarios almost similar to previous section.

- Configure Two ``spp_nfv`` as L2fwd
- Configure Two ``spp_nfv`` for Loopback


Configure Two spp_nfv as L2fwd
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Assing the destination of ports with ``patch`` subcommand and
start forwarding.
Patch from ``phy:0`` to ``phy:1`` for ``nfv 1`` and
from ``phy:1`` to ``phy:0`` for ``nfv 2``.

.. code-block:: console

    spp > nfv 1; patch phy:0 phy:1
    Patch ports (phy:0 -> phy:1).
    spp > nfv 2; patch phy:1 phy:0
    Patch ports (phy:1 -> phy:0).
    spp > nfv 1; forward
    Start forwarding.
    spp > nfv 2; forward
    Start forwarding.

.. _figure_spp_two_nfv_as_l2fwd:

.. figure:: ../images/setup/use_cases/spp_two_nfv_l2fwd.*
   :width: 60%

   Two spp_nfv as l2fwd


Configure two spp_nfv for Loopback
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Patch ``phy:0`` to ``phy:0`` for ``nfv 1`` and
``phy:1`` to ``phy:1`` for ``nfv 2`` for loopback.

.. code-block:: console

    spp > nfv 1; patch phy:0 phy:0
    Patch ports (phy:0 -> phy:0).
    spp > nfv 2; patch phy:1 phy:1
    Patch ports (phy:1 -> phy:1).
    spp > nfv 1; forward
    Start forwarding.
    spp > nfv 2; forward
    Start forwarding.

.. _figure_spp_two_nfv_loopback:

.. figure:: ../images/setup/use_cases/spp_two_nfv_loopback.*
   :width: 62%

   Two spp_nfv for loopback


Dual spp_nfv with Ring PMD
--------------------------

In this usecase, configure two senarios by using ring PMD.

- Uni-Directional L2fwd
- Bi-Directional L2fwd

Ring PMD
~~~~~~~~

Ring PMD is an interface for communicating between secondaries on host.
The maximum number of ring PMDs is defined as ``-n``  option of
``spp_primary`` and ring ID is started from 0.

Ring PMD is added by using ``add`` subcommand.
All of ring PMDs is showed with ``status`` subcommand.

.. code-block:: console

    spp > nfv 1; add ring:0
    Add ring:0.
    spp > nfv 1; status
    - status: idling
    - ports:
      - phy:0
      - phy:1
      - ring:0

Notice that ``ring:0`` is added to ``nfv 1``.
You can delete it with ``del`` command if you do not need to
use it anymore.

.. code-block:: console

    spp > nfv 1; del ring:0
    Delete ring:0.
    spp > nfv 1; status
    - status: idling
    - ports:
      - phy:0
      - phy:1


Uni-Directional L2fwd
~~~~~~~~~~~~~~~~~~~~~

Add a ring PMD and connect two ``spp_nvf`` processes.
To configure network path, add ``ring:0`` to ``nfv 1`` and ``nfv 2``.
Then, connect it with ``patch`` subcommand.

.. code-block:: console

    spp > nfv 1; add ring:0
    Add ring:0.
    spp > nfv 2; add ring:0
    Add ring:0.
    spp > nfv 1; patch phy:0 ring:0
    Patch ports (phy:0 -> ring:0).
    spp > nfv 2; patch ring:0 phy:1
    Patch ports (ring:0 -> phy:1).
    spp > nfv 1; forward
    Start forwarding.
    spp > nfv 2; forward
    Start forwarding.

.. _figure_spp_uni_directional_l2fwd:

.. figure:: ../images/setup/use_cases/spp_unidir_l2fwd.*
   :width: 72%

   Uni-Directional l2fwd


Bi-Directional L2fwd
~~~~~~~~~~~~~~~~~~~~

Add two ring PMDs to two ``spp_nvf`` processes.
For bi-directional forwarding,
patch ``ring:0`` for a path from ``nfv 1`` to ``nfv 2``
and ``ring:1`` for another path from ``nfv 2`` to ``nfv 1``.

First, add ``ring:0`` and ``ring:1`` to ``nfv 1``.

.. code-block:: console

    spp > nfv 1; add ring:0
    Add ring:0.
    spp > nfv 1; add ring:1
    Add ring:1.
    spp > nfv 1; status
    - status: idling
    - ports:
      - phy:0
      - phy:1
      - ring:0
      - ring:1

Then, add ``ring:0`` and ``ring:1`` to ``nfv 2``.

.. code-block:: console

    spp > nfv 2; add ring:0
    Add ring:0.
    spp > nfv 2; add ring:1
    Add ring:1.
    spp > nfv 2; status
    - status: idling
    - ports:
      - phy:0
      - phy:1
      - ring:0
      - ring:1

.. code-block:: console

    spp > nfv 1; patch phy:0 ring:0
    Patch ports (phy:0 -> ring:0).
    spp > nfv 1; patch ring:1 phy:0
    Patch ports (ring:1 -> phy:0).
    spp > nfv 2; patch phy:1 ring:1
    Patch ports (phy:1 -> ring:0).
    spp > nfv 2; patch ring:0 phy:1
    Patch ports (ring:0 -> phy:1).
    spp > nfv 1; forward
    Start forwarding.
    spp > nfv 2; forward
    Start forwarding.

.. _figure_spp_bi_directional_l2fwd:

.. figure:: ../images/setup/use_cases/spp_bidir_l2fwd.*
   :width: 72%

   Bi-Directional l2fwd


Single spp_nfv with Vhost PMD
-----------------------------

Vhost PMD
~~~~~~~~~

Vhost PMD is an interface for communicating between on hsot and guest VM.
As described in
:ref:`How to Use<spp_setup_howto_use>`,
vhost must be created by ``add`` subcommand before the VM is launched.


Setup Vhost PMD
~~~~~~~~~~~~~~~

In this usecase, add ``vhost:0`` to ``nfv 1`` for communicating
with the VM.
First, check if ``/tmp/sock0`` is already exist.
You should remove it already exist to avoid a failure of socket file
creation.

.. code-block:: console

    $ ls /tmp | grep sock
    sock0 ...

    # remove it if exist
    $ sudo rm /tmp/sock0

Create ``/tmp/sock0`` from ``nfv 1``.

.. code-block:: console

    spp > nfv 1; add vhost:0
    Add vhost:0.


.. _usecase_unidir_l2fwd_vhost:

Uni-Directional L2fwd with Vhost PMD
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Launch a VM by using the vhost interface created as previous step.
Lauunching VM is described in
:ref:`How to Use<spp_setup_howto_use>`
and launch ``spp_vm`` with secondary ID 2.
You find ``nfv 2`` from controller after launched.

Patch ``phy:0`` and ``phy:1`` to ``vhost:0`` with ``nfv 1``
running on host.
Inside VM, configure loopback by patching ``phy:0`` and ``phy:0``
with ``nfv 2``.

.. code-block:: console

    spp > nfv 1; patch phy:0 vhost:0
    Patch ports (phy:0 -> vhost:0).
    spp > nfv 1; patch vhost:0 phy:1
    Patch ports (vhost:0 -> phy:1).
    spp > nfv 2; patch phy:0 phy:0
    Patch ports (phy:0 -> phy:0).
    spp > nfv 1; forward
    Start forwarding.
    spp > nfv 2; forward
    Start forwarding.

.. _figure_spp_uni_directional_l2fwd_vhost:

.. figure:: ../images/setup/use_cases/spp_unidir_l2fwd_vhost.*
   :width: 72%

   Uni-Directional l2fwd with vhost

Single spp_nfv with PCAP PMD
-----------------------------

PCAP PMD
~~~~~~~~

Pcap PMD is an interface for capturing or restoring traffic.
For usign pcap PMD, you should set ``CONFIG_RTE_LIBRTE_PMD_PCAP``
and ``CONFIG_RTE_PORT_PCAP`` to ``y`` and compile DPDK before SPP.
Refer to
:ref:`Install DPDK and SPP<install_dpdk_spp>`
for details of setting up.

Pcap PMD has two different streams for rx and tx.
Tx device is for capturing packets and rx is for restoring captured
packets.
For rx device, you can use any of pcap files other than SPP's pcap PMD.

To start using pcap pmd, just using ``add`` subcommand as ring.
Here is an example for creating pcap PMD ``pcap:1``.

.. code-block:: console

    spp > nfv 1; add pcap:1

After running it, you can find two of pcap files in ``/tmp``.

.. code-block:: console

    $ ls /tmp | grep pcap$
    spp-rx1.pcap
    spp-tx1.pcap

If you already have a dumped file, you can use it by it putting as
``/tmp/spp-rx1.pcap`` before running the ``add`` subcommand.
SPP does not overwrite rx pcap file if it already exist,
and it just overwrites tx pcap file.

Capture Incoming Packets
~~~~~~~~~~~~~~~~~~~~~~~~

As the first usecase, add a pcap PMD and capture incoming packets from
``phy:0``.

.. code-block:: console

    spp > nfv 1; add pcap 1
    Add pcap:1.
    spp > nfv 1; patch phy:0 pcap:1
    Patch ports (phy:0 -> pcap:1).
    spp > nfv 1; forward
    Start forwarding.

.. _figure_spp_pcap_incoming:

.. figure:: ../images/setup/use_cases/spp_pcap_incoming.*
   :width: 60%

   Rapture incoming packets

In this example, we use pktgen.
Once you start forwarding packets from pktgen, you can see
that the size of ``/tmp/spp-tx1.pcap`` is increased rapidly
(or gradually, it depends on the rate).

.. code-block:: console

    Pktgen:/> set 0 size 1024
    Pktgen:/> start 0

To stop capturing, simply stop forwarding of ``spp_nfv``.

.. code-block:: console

    spp > nfv 1; stop
    Stop forwarding.

You can analyze the dumped pcap file with other tools like as wireshark.

Restore dumped Packets
~~~~~~~~~~~~~~~~~~~~~~

In this usecase, use dumped file in previsou section.
Copy ``spp-tx1.pcap`` to ``spp-rx2.pcap`` first.

.. code-block:: console

    $ sudo cp /tmp/spp-tx1.pcap /tmp/spp-rx2.pcap

Then, add pcap PMD ``pcap:2`` to another ``spp_nfv``.

.. code-block:: console

    spp > nfv 2; add pcap:2
    Add pcap:2.

.. _figure_spp_pcap_restoring:

.. figure:: ../images/setup/use_cases/spp_pcap_restoring.*
   :width: 60%

   Restore dumped packets

You can find that ``spp-tx2.pcap`` is creaeted and ``spp-rx2.pcap``
still remained.

.. code-block:: console

    $ ls -al /tmp/spp*.pcap
    -rw-r--r-- 1 root root         24  ...  /tmp/spp-rx1.pcap
    -rw-r--r-- 1 root root 2936703640  ...  /tmp/spp-rx2.pcap
    -rw-r--r-- 1 root root 2936703640  ...  /tmp/spp-tx1.pcap
    -rw-r--r-- 1 root root          0  ...  /tmp/spp-tx2.pcap

To confirm packets are restored, patch ``pcap:2`` to ``phy:1``
and watch received packets on pktgen.

.. code-block:: console

    spp > nfv 2; patch pcap:2 phy:1
    Patch ports (pcap:2 -> phy:1).
    spp > nfv 2; forward
    Start forwarding.

After started forwarding, you can see that packet count is increased.


Multiple Nodes
--------------

SPP provides multi-node support for configuring network across several nodes
from SPP CLI. You can configure each of nodes step by step.

In :numref:`figure_spp_multi_nodes_vhost`, there are four nodes on which
SPP and service VMs are running. Host1 behaves as a patch panel for connecting
between other nodes. A request is sent from a VM on host2 to a VM on host3 or
host4. Host4 is a backup server for host3 and replaced with host3 by changing
network configuration. Blue lines are paths for host3 and red lines are for
host4, and changed alternatively.

.. _figure_spp_multi_nodes_vhost:

.. figure:: ../images/setup/use_cases/spp_multi_nodes_vhost.*
   :width: 100%

   Multiple nodes example

Launch SPP on Multiple Nodes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Before SPP CLI, launch spp-ctl on each of nodes. You should give IP address
with ``-b`` option to be accessed from outside of the node.
This is an example for launching spp-ctl on host1.

.. code-block:: console

    # Launch on host1
    $ python3 src/spp-ctl/spp-ctl -b 192.168.11.101

You also need to launch it on host2, host3 and host4 in each of terminals.

After all of spp-ctls are lauched, launch SPP CLI with four ``-b`` options
for each of hosts. SPP CLI is able to be launched on any of nodes.

.. code-block:: console

    # Launch SPP CLI
    $ python src/spp.py -b 192.168.11.101 \
        -b 192.168.11.102 \
        -b 192.168.11.103 \
        -b 192.168.11.104 \

If you succeeded to launch all of processes before, you can find them
by running ``sever list`` command.

.. code-block:: console

    # Launch SPP CLI
    spp > server list
      1: 192.168.1.101:7777 *
      2: 192.168.1.102:7777
      3: 192.168.1.103:7777
      4: 192.168.1.104:7777

You might notice that first entry is marked with ``*``. It means that
the current node under the management is the first node.

Switch Nodes
~~~~~~~~~~~~

SPP CLI manages a node marked with ``*``. If you configure other nodes,
change the managed node with ``server`` command.
Here is an example to switch to third node.

.. code-block:: console

    # Launch SPP CLI
    spp > server 3
    Switch spp-ctl to "3: 192.168.1.103:7777".

And the result after changed to host3.

.. code-block:: console

    spp > server list
      1: 192.168.1.101:7777
      2: 192.168.1.102:7777
      3: 192.168.1.103:7777 *
      4: 192.168.1.104:7777

You can also confirm this change by checking IP address of spp-ctl from
``status`` command.

.. code-block:: console

    spp > status
    - spp-ctl:
      - address: 192.168.1.103:7777
    - primary:
      - status: not running
    ...

Configure Patch Panel Node
~~~~~~~~~~~~~~~~~~~~~~~~~~

First of all of the network configuration, setup blue lines on host1
described in :numref:`figure_spp_multi_nodes_vhost`.
You should confirm the managed server is host1.

.. code-block:: console

    spp > server list
      1: 192.168.1.101:7777 *
      2: 192.168.1.102:7777
      ...

Patch two sets of physical ports and start forwarding.

.. code-block:: console

    spp > nfv 1; patch phy:1 phy:2
    Patch ports (phy:1 -> phy:2).
    spp > nfv 1; patch phy:3 phy:0
    Patch ports (phy:3 -> phy:0).
    spp > nfv 1; forward
    Start forwarding.

Configure Service VM Nodes
~~~~~~~~~~~~~~~~~~~~~~~~~~

It is almost similar as
:ref:`Uni-Directional L2fwd with Vhost PMD<usecase_unidir_l2fwd_vhost>`.
to setup for host2, host3, and host4.

For host2, swith server to host2 and run nfv commands.

.. code-block:: console

    # switch to server 2
    spp > server 2
    Switch spp-ctl to "2: 192.168.1.102:7777".

    # configure
    spp > nfv 1; patch phy:0 vhost:0
    Patch ports (phy:0 -> vhost:0).
    spp > nfv 1; patch vhost:0 phy:1
    Patch ports (vhost:0 -> phy:1).
    spp > nfv 1; forward
    Start forwarding.

Then, swith to host3 and host4 for doing the same configuration.

Change Path to Backup Node
~~~~~~~~~~~~~~~~~~~~~~~~~~

Finally, change path from blue lines to red lines.

.. code-block:: console

    # switch to server 1
    spp > server 2
    Switch spp-ctl to "2: 192.168.1.102:7777".

    # remove blue path
    spp > nfv 1; stop
    Stop forwarding.
    spp > nfv 1; patch reset

    # configure red path
    spp > nfv 2; patch phy:1 phy:4
    Patch ports (phy:1 -> phy:4).
    spp > nfv 2; patch phy:5 phy:0
    Patch ports (phy:5 -> phy:0).
    spp > nfv 2; forward
    Start forwarding.
