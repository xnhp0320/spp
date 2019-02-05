..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_vf_use_cases_basic:

Classify and merge ICMP packets
===============================

This usecase uses two hosts. ``spp_vf`` is running on localhost and remote host
sends ICMP packet towards localhost to confirm packet from remote host is
forwarded to remote host by ``spp_vf``. This section describes a usecase for
L2 Forwarding through ``spp_vf``. To send ICMP echo packet from remote host to
local host, you use ``ping`` command. Classifier receives incoming packets and
classify to send to destinations based on MAC address table. Forwarder sends
packets to merger. Merger aggregates those incoming packets from two forwarders
and sends to remote host from outgoing port.

Launch SPP Processes
--------------------

Change directory to spp and confirm that it is already compiled.

.. code-block:: console

    $ cd /path/to/spp

Launch ``spp-ctl`` before launching SPP primary and secondary processes.
You also need to launch ``SPP CLI`` if you use ``spp_vf`` from CLI.
``-b`` option is for binding IP address to communicate other SPP processes,
but no need to give it explicitly if ``127.0.0.1`` or ``localhost`` .

.. code-block:: console

    # terminal#1
    # Launch spp-ctl
    $ python3 ./src/spp-ctl/spp-ctl -b 127.0.0.1

.. code-block:: console

    # terminal#2
    # Launch SPP CLI
    $ python ./src/spp.py -b 127.0.0.1

Then, run ``spp_primary`` on the second core with ``-l 1``.

.. code-block:: console

    # terminal#3
    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
        -l 1 -n 4 \
        --socket-mem 512,512 \
        --huge-dir=/run/hugepages/kvm \
        --proc-type=primary \
        -- \
        -p 0x03 -n 8 -s 127.0.0.1:5555

After ``spp_primary`` is launched, run secondary process ``spp_vf``.
Core list ``-l 2-6`` indicates to use five cores.

.. code-block:: console

     # terminal#4
     $ sudo ./src/vf/x86_64-native-linuxapp-gcc/spp_vf \
        -l 2-6 -n 4 --proc-type=secondary \
        -- \
        --client-id 1 \
        -s 127.0.0.1:6666 \

Network Configuration
---------------------

Detailed configuration is described below.
In this usecase, there are two NICs on host1 and host2 and one NIC
is used to send packet and the other is used to receive packet.

Incoming packets from NIC0 are classified based on destination address.
For example, cls1 sends packets to fwd1 and fwd2.
Outgoing packets are aggregated to mgr1 and sent to host1 via NIC1.

.. _figure_spp_vf_use_cases_nw_config:

.. figure:: ../../images/spp_vf/basic_usecase_vf_nwconfig.*
    :width: 90%

    Network Configuration

First, launch threads of SPP VF called ``component`` with its CORE_ID
and a directive for behavior.
It is launched from ``component`` subcommand with options.

.. code-block:: console

    spp > sec SEC_ID; component start NAME CORE_ID BEHAVIOUR

In this usecase, ``spp_vf`` is launched with ``SEC_ID`` 1.
Let's start components for the first login path.
``BEHAVIOUR`` for classifier ``classifier_mac`` means to classify with MAC
address.
``CORE_ID`` is the ID of the core that is assigned for each component.
In this example, ``CORE_ID`` from 3 to 6 are assigned as following.

.. code-block:: console

    # Start component to spp_vf
    spp > vf 1; component start cls1 3 classifier_mac
    spp > vf 1; component start fwd1 4 forward
    spp > vf 1; component start fwd2 5 forward
    spp > vf 1; component start mgr1 6 merge

Each of components must have rx and tx ports.
Number of tx port and rx port are different among components.
Add ports for each of components as following.
You might notice that classifier has two tx ports and merger has two rx ports.

.. code-block:: console

    # cls1
    spp > vf 1; port add phy:0 rx cls1
    spp > vf 1; port add ring:0 tx cls1
    spp > vf 1; port add ring:1 tx cls1

    # fwd1
    spp > vf 1; port add ring:0 rx fwd1
    spp > vf 1; port add ring:2 tx fwd1

    # fwd2
    spp > vf 1; port add ring:1 rx fwd2
    spp > vf 1; port add ring:3 tx fwd2

    # mgr1
    spp > vf 1; port add ring:2 rx mgr1
    spp > vf 1; port add ring:3 rx mgr1
    spp > vf 1; port add phy:1 tx mgr1

As given ``classifier_mac``, classifier component decides
the destination with MAC address by referring ``classifier_table``.
MAC address and corresponding port is registered to the table with
``classifier_table add mac`` command.

.. code-block:: console

    spp > vf SEC_ID; classifier_table add mac MAC_ADDR RES_UID

In this usecase, you need to register two MAC addresses. Although
any MAC address can be used, you assign ``52:54:00:12:34:56``
and ``52:54:00:12:34:58`` for each port in this example.

.. code-block:: console

    # Register MAC address to classifier
    spp > vf 1; classifier_table add mac 52:54:00:12:34:56 ring:0
    spp > vf 1; classifier_table add mac 52:54:00:12:34:58 ring:1

Send packet from host1
----------------------

Configure IP address of ``ens0`` and add arp entry for two MAC
addresses statically to resolve address.

.. code-block:: console

    # terminal#1 at host1
    # configure ip address of ens0
    $ sudo ifconfig ens0 192.168.140.1 255.255.255.0 up

    # set MAC address
    $ sudo arp -i ens0 -s 192.168.140.2 52:54:00:12:34:56
    $ sudo arp -i ens0 -s 192.168.140.3 52:54:00:12:34:58

Start capture on ``ens1``.
You can see ICMP Echo request received when ping is executed.

.. code-block:: console

    # terminal#2 at host1
    # capture on ens1
    $ sudo tcpdump -i ens1

Start ping on different terminals to send ICMP Echo request.

.. code-block:: console

    # terminal#3 at host1
    # ping via NIC0
    $ ping 192.168.140.2

.. code-block:: console

    # terminal#4 at host1
    # ping via NIC0
    $ ping 192.168.140.3

.. _spp_vf_use_cases_shutdown_comps:

Shutdown spp_vf Components
--------------------------

Basically, you can shutdown all the SPP processes with bye all command.
However there is a case when user want to shutdown specific secondary process
only.
This section describes such a shutting down process for SPP VF components.

First, delete entries of ``classifier_table`` and ports of components.

.. code-block:: console

    # Delete MAC address from Classifier
    spp > vf 1; classifier_table del mac 52:54:00:12:34:56 ring:0
    spp > vf 1; classifier_table del mac 52:54:00:12:34:58 ring:1

.. code-block:: console

    # cls1
    spp > vf 1; port del phy:0 rx cls1
    spp > vf 1; port del ring:0 tx cls1
    spp > vf 1; port del ring:1 tx cls1

    # fwd1
    spp > vf 1; port del ring:0 rx fwd1
    spp > vf 1; port del vhost:0 tx fwd1

    # fwd2
    spp > vf 1; port del ring:1 rx fwd2
    spp > vf 1; port del vhost:2 tx fwd2

    # mgr1
    spp > vf 1; port del ring:2 rx mgr1
    spp > vf 1; port del ring:3 rx mgr1
    spp > vf 1; port del phy:0 tx mgr1

Then, stop components.

.. code-block:: console

    # Stop component to spp_vf
    spp > vf 1; component stop cls1
    spp > vf 1; component stop fwd1
    spp > vf 1; component stop fwd2
    spp > vf 1; component stop mgr1

    spp > vf 1; status
    Basic Information:
      - client-id: 1
      - ports: [phy:0, phy:1]
    Classifier Table:
      No entries.
    Components:
      - core:3 '' (type: unuse)
      - core:4 '' (type: unuse)
      - core:5 '' (type: unuse)
      - core:6 '' (type: unuse)

Finally, terminate spp_vf to finish this usecase.

.. code-block:: console

    spp > vf 0; exit
