..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_vf_use_cases_usecase1:

Simple L2 Forwarding
====================

This section describes a usecase for simple L2 Forwarding through SPP VF.
Incoming packets are classified based on destination addresses defined
in ``classifier``.
Returned packets are aggregated to ``merger`` to send it an outgoing
port.


Launch SPP Processes
--------------------

Change directory to spp and confirm that it is already compiled.

.. code-block:: console

    $ cd /path/to/spp

Launch ``spp-ctl`` before launching SPP primary and secondary processes.
You also need to launch ``spp.py``  if you use ``spp_vf`` from CLI.
``-b`` option is for binding IP address to communicate other SPP processes,
but no need to give it explicitly if ``127.0.0.1`` or ``localhost`` .

.. code-block:: console

    # Launch spp-ctl and spp.py
    $ python3 ./src/spp-ctl/spp-ctl -b 127.0.0.1
    $ python ./src/spp.py -b 127.0.0.1

Then, run ``spp_primary`` on the second core with ``-c 0x02``.

.. code-block:: console

    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
        -l 1 -n 4 \
        --socket-mem 512,512 \
        --huge-dir=/run/hugepages/kvm \
        --proc-type=primary \
        -- \
        -p 0x03 -n 8 -s 127.0.0.1:5555

After ``spp_primary`` is launched, run secondary process ``spp_vf``.
Core list ``-l 2-5`` indicates to use 4 cores.

.. code-block:: console

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
For example, classifier1 sends packets to forwarder1 and forwarder2.
Outgoing packets are aggregated to merger1 and sent to host1 via NIC1.

.. _figure_network_config:

.. figure:: ../../images/spp_vf/simple_usecase_vf_nwconfig.*
    :width: 100%

    Network Configuration

First, launch threads of SPP VF called ``component`` with its CORE_ID
and a directive for behavior.
It is launched from ``component`` subcommand with options.

.. code-block:: console

    spp > sec SEC_ID; component start NAME CORE_ID BEHAVIOUR

In this usecase, spp_vf is launched with ID=1. Let's start components
for the first login path.
Directive for classifier ``classifier_mac`` means to classify with MAC
address.
CORE_ID from 3 to 6 are assigned to each of components.

.. code-block:: console

    # Start component to spp_vf
    spp > vf 1; component start classifier1 3 classifier_mac
    spp > vf 1; component start forwarder1 4 forward
    spp > vf 1; component start forwarder2 5 forward
    spp > vf 1; component start merger1 6 merge

Each of components must have rx and tx ports for forwarding.
Add ports for each of components as following. You might notice that classifier has two tx ports and merger has two rx ports.

.. code-block:: console

    # classifier1
    spp > vf 1; port add phy:0 rx classifier1
    spp > vf 1; port add ring:0 tx classifier1
    spp > vf 1; port add ring:1 tx classifier1

    # forwarder1
    spp > vf 1; port add ring:0 rx forwarder1
    spp > vf 1; port add ring:2 tx forwarder1

    # forwarder2
    spp > vf 1; port add ring:1 rx forwarder2
    spp > vf 1; port add ring:3 tx forwarder2

    # merger1
    spp > vf 1; port add ring:2 rx merger1
    spp > vf 1; port add ring:3 rx merger1
    spp > vf 1; port add phy:1 tx merger1

As given ``classifier_mac``, classifier component decides
the destination with MAC address by referring ``classifier_table``.
MAC address and corresponding port is registered to the table with
``classifier_table add mac`` command.

.. code-block:: console

    spp > vf SEC_ID; classifier_table add mac MAC_ADDR PORT

In this usecase, you need to register two MAC addresses.

.. code-block:: console

    # Register MAC address to classifier
    spp > vf 1; classifier_table add mac 52:54:00:12:34:56 ring:0
    spp > vf 1; classifier_table add mac 52:54:00:12:34:58 ring:1

Send packet from host1
----------------------

Now, you can send packets from host1 to host2.

.. code-block:: console

    # capture on ens1
    $ sudo tcpdump -i ens1

    # configure ip address of ens0
    $ sudo ifconfig ens0 192.168.140.1 255.255.255.0 up

    # set mac address statically
    $ sudo arp -i ens0 -s 192.168.140.2 52:54:00:12:34:56
    $ sudo arp -i ens0 -s 192.168.140.3 52:54:00:12:34:58

    # ping via NIC0
    $ ping 192.168.140.2

    # ping via NIC0
    $ ping 192.168.140.3

.. _spp_vf_use_cases_usecase1_shutdown_spp_vf_components:

Shutdown spp_vf Components
--------------------------

Basically, you can shutdown all the SPP processes with bye all command. However there is a case when user want to shutdown specific secondary process only.
This section describes such a shutting down process for SPP VF components.

First, delete entries of ``classifier_table`` and ports of components.

.. code-block:: console

    # Delete MAC address from Classifier
    spp > vf 1; classifier_table del mac 52:54:00:12:34:56 ring:0
    spp > vf 1; classifier_table del mac 52:54:00:12:34:58 ring:1

.. code-block:: console

    # classifier1
    spp > vf 1; port del phy:0 rx classifier1
    spp > vf 1; port del ring:0 tx classifier1
    spp > vf 1; port del ring:1 tx classifier1
    # forwarder1
    spp > vf 1; port del ring:0 rx forwarder1
    spp > vf 1; port del vhost:0 tx forwarder1
    # forwarder2
    spp > vf 1; port del ring:1 rx forwarder2
    spp > vf 1; port del vhost:2 tx forwarder2

    # merger1
    spp > vf 1; port del ring:2 rx merger1
    spp > vf 1; port del ring:3 rx merger1
    spp > vf 1; port del phy:0 tx merger1

Then, stop components.

.. code-block:: console

    # Stop component to spp_vf
    spp > vf 1; component stop classifier1
    spp > vf 1; component stop forwarder1
    spp > vf 1; component stop forwarder2
    spp > vf 1; component stop merger1

Terminate spp_vf.

.. code-block:: console

    spp > vf 1; exit
