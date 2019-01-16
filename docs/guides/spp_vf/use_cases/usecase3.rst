..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_pcap_use_case:

Packet Capture
==============


This section describes a usecase for Packet Capture through ``spp_pcap``.

Incoming packets received by ``phy:0`` is captured by ``spp_pcap``.

.. _figure_simple_capture:

.. figure:: ../../images/spp_pcap/spp_pcap_overview.*
    :width: 40%

    Simple Packet Capture

Launch spp_pcap
~~~~~~~~~~~~~~~

Change directory to spp and confirm that it is already compiled.

.. code-block:: console

    $ cd /path/to/spp

As spp, launch spp-ctl and spp.py first.

.. code-block:: console

    # Launch spp-ctl and spp.py
    $ python3 ./src/spp-ctl/spp-ctl -b 127.0.0.1
    $ python ./src/spp.py -b 127.0.0.1


Then, run ``spp_primary``.

.. code-block:: console

    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
        -c 0x02 -n 4 \
        --socket-mem 512,512 \
        --huge-dir=/run/hugepages/kvm \
        --proc-type=primary \
        -- \
        -p 0x03 -n 8 -s 127.0.0.1:5555

After ``spp_primary`` is launched, run secondary process ``spp_pcap``.
If not ``--output`` directory is not created, please create it first.

.. code-block:: console

    $ sudo mkdir /mnt/pcap
    $ sudo ./src/pcap/x86_64-native-linuxapp-gcc/spp_pcap \
       -l 0-4 -n 4 --proc-type=secondary \
       -- \
       --client-id 1 -s 127.0.0.1:6666 \
       -i phy:0 --output /mnt/pcap --limit_file_size 1073741824

Start capturing
~~~~~~~~~~~~~~~
When you want to start capture, then type the following command.

.. code-block:: console

    spp > pcap SEC_ID; start

In this usecase, spp_pcap is launched with ID=1. Let's start capturing.

.. code-block:: console

    # Start packet capture
    spp > pcap 1;start

Stop capturing
~~~~~~~~~~~~~~

When you want to stop capture, then type the following command.

.. code-block:: console

    spp > pcap SEC_ID; stop

In this usecase, spp_pcap is launched with ID=1. Let's stop capturing.

.. code-block:: console

    # Stop packet capture
    spp > pcap 1;stop


Now, you can see capture file written in specified directory.

.. code-block:: console

    # show the content of directry
    $ cd /mnt/pcap
    $ ls
      spp_pcap.20181108110600.phy0.1.1.pcap.lz4
      spp_pcap.20181108110600.phy0.2.1.pcap.lz4
      spp_pcap.20181108110600.phy0.3.1.pcap.lz4

Each files are compressed using LZ4, so that to uncompress it,
use lz4 utils.

.. code-block:: console

    # uncompress lz4 files
    $ sudo lz4 -d -m spp_pcap.20181108110600.phy0.*
    $ ls
      spp_pcap.20181108110600.phy0.1.1.pcap
      spp_pcap.20181108110600.phy0.2.1.pcap
      spp_pcap.20181108110600.phy0.3.1.pcap
      spp_pcap.20181108110600.phy0.1.1.pcap.lz4
      spp_pcap.20181108110600.phy0.2.1.pcap.lz4
      spp_pcap.20181108110600.phy0.3.1.pcap.lz4

To combine those divided pcap files using mergecap utility.

.. code-block:: console

    # merge pcap files
    $ sudo mergecap spp_pcap.20181108110600.phy0.1.1.pcap \
      spp_pcap.20181108110600.phy0.2.1.pcap \
      spp_pcap.20181108110600.phy0.3.1.pcap \
      -w test.pcap
    $ ls
      spp_pcap.20181108110600.phy0.1.1.pcap
      spp_pcap.20181108110600.phy0.2.1.pcap
      spp_pcap.20181108110600.phy0.3.1.pcap
      spp_pcap.20181108110600.phy0.1.1.pcap.lz4
      spp_pcap.20181108110600.phy0.2.1.pcap.lz4
      spp_pcap.20181108110600.phy0.3.1.pcap.lz4
      test.pcap

.. _spp_pcap_use_case_shutdown:

Shutdown spp_pcap
~~~~~~~~~~~~~~~~~

Basically, you can shutdown all of SPP processes with ``bye all``
command.

This section describes graceful shutting down for ``spp_pcap``.

First, stop capturing using the following command if it is not
already stopped.

.. code-block:: console

    # Stop packet capture
    spp > pcap 1;stop

If you want to start capture again then use ``start`` command again.
Else if you want to quit ``spp_pcap`` itself, type the following command
and quit application.

.. code-block:: console

    # Exit packet capture
    spp > pcap 1;exit
