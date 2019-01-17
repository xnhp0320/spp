..
   SPDX-License-Identifier: BSD-3-Clause
   Copyright(c) 2019 Nippon Telegraph and Telephone Corporation


.. _spp_mirror_simple_use_cases_usecase:

Mirroring packet from NIC
=========================

This section describes a usage for mirroring from a ``phy:0`` to ``phy:1`` and
``phy:2`` through spp_mirror.  Traffic from host2 is returned from other NICs.


Network Configuration
---------------------

Detailed configuration of is described in
:numref:`figure_spp_mirror_simple_usecase_nwconfig`.
In this scenario, incoming packets from ``phy:0`` are mirrored.
In ``spp_mirror`` process, worker thread ``mirror1`` copies incoming packets and
sends to two destinations ``phy:1`` and ``phy:2``.

.. _figure_spp_mirror_simple_usecase_nwconfig:

.. figure:: ../../images/spp_vf/simple_usecase_mirror_nwconfig.*
     :width: 80%

     Network configuration of mirroring


Setup SPP
---------

Launch ``spp-ctl`` before launching SPP primary and secondary processes.
You also need to launch ``spp.py``  if you use ``spp_mirror`` from CLI.
``-b`` option is for binding IP address to communicate other SPP processes,
but no need to give it explicitly if ``127.0.0.1`` or ``localhost`` .

.. code-block:: console

    $cd /path/to/spp

    # Launch spp-ctl and spp.py
    $ python3 ./src/spp-ctl/spp-ctl -b 127.0.0.1
    $ python ./src/spp.py -b 127.0.0.1

Start spp_primary with core list option ``-l 1``. It should be started
with ``-n 16`` for giving enough number of rings.

.. code-block:: console

   # Type the following in different terminal
   $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
       -l 1 -n 4 \
       --socket-mem 512,512 \
       --huge-dir=/run/hugepages/kvm \
       --proc-type=primary \
       -- \
       -p 0x07 -n 16 -s 127.0.0.1:5555

Launch spp_mirror
~~~~~~~~~~~~~~~~~

Change directory to spp and confirm that it is already compiled.

.. code-block:: console

   $ cd /path/to/spp

Run secondary process ``spp_mirror``.

.. code-block:: console

   $ sudo ./src/mirror/x86_64-native-linuxapp-gcc/app/spp_mirror \
     -l 0,2 -n 4 --proc-type=secondary \
     -- \
     --client-id 1 \
     -s 127.0.0.1:6666 \

Start mirror component with core ID 2.

.. code-block:: console

    # Start component of spp_mirror on Core_ID 2
    spp > mirror 1; component start mirror1 2 mirror

Add ``phy:0`` as rx ports and add ``phy:1`` and ``phy:2`` as tx port
to mirror.

.. code-block:: console

   # mirror1
   spp > mirror 1; port add phy:0 rx mirror1
   spp > mirror 1; port add phy:1 tx mirror1
   spp > mirror 1; port add phy:2 tx mirror1


Send packet from host2
~~~~~~~~~~~~~~~~~~~~~~
You can send packet from host2 and at the same time, receive packets from host1.
The same packet would be captured in ens1 and ens2.

.. code-block:: console

   # capture on ens1 and ens2
   $ sudo tcpdump -i ens1
   $ sudo tcpdump -i ens2

Now, you can send packet from host1.

.. code-block:: console

   # send packet from NIC0 of host1
   $ ping 192.168.140.21 -I ens1


Stop Mirroring
~~~~~~~~~~~~~~

Firstly, delete ports for components.

Delete ports for components.

.. code-block:: console

   # Delete port for mirror1
   spp > mirror 1; port del phy:0 rx mirror1
   spp > mirror 1; port del phy:1 tx mirror1
   spp > mirror 1; port del phy:2 tx mirror1

Next, stop components.

.. code-block:: console

   # Stop mirror
   spp > mirror 1; component stop mirror1 2 mirror

Terminate spp_mirror.

.. code-block:: console

    spp > mirror 1; exit
