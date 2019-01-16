..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

.. _spp_vf_gsg_howto_use:

How to Use
==========

SPP Controller
--------------

Go to the SPP's directory first.

.. code-block:: console

    $ cd /path/to/spp

Launch ``spp-ctl`` before launching SPP primary and secondary processes.
You also need to launch ``spp.py``  if you use ``spp_vf`` from CLI.
``-b`` option is for binding IP address to communicate other SPP processes,
but no need to give it explicitly if ``127.0.0.1`` or ``localhost`` although
doing explicitly in this example to be more understandable.

.. code-block:: console

    # Launch spp-ctl and spp.py
    $ python3 ./src/spp-ctl/spp-ctl -b 127.0.0.1
    $ python ./src/spp.py -b 127.0.0.1


SPP Primary
-----------

SPP primary allocates and manages resources for secondary processes.
You need to run SPP primary before secondary processes.

SPP primary has two kinds of options for DPDK and spp.
Before ``--`` are for DPDK is, and after it are for spp.

See `Running a Sample Application
<http://dpdk.org/doc/guides/linux_gsg/build_sample_apps.html#running-a-sample-application>`_
in DPDK documentation for options.

Options of spp primary are:

  * -p : port mask
  * -n : number of rings
  * -s : IPv4 address and port for spp primary

Then, spp primary can be launched like this.

.. code-block:: console

    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
      -l 1 -n 4 --socket-mem 512,512 \
      --huge-dir=/run/hugepages/kvm \
      --proc-type=primary \
      -- \
      -p 0x03 -n 9 -s 127.0.0.1:5555


.. _spp_vf_gsg_howto_use_spp_vf:

spp_vf
------

``spp_vf`` can be launched with two kinds of options, like primary process.

Like primary process, ``spp_vf`` has two kinds of options. One is for
DPDK, the other is ``spp_vf``.

``spp_vf`` specific options are:

  * --client-id: client id which can be seen as secondary ID from spp.py
  * -s: IPv4 address and port for spp secondary
  * --vhost-client: vhost-user client enable setting

``spp_vf`` can be launched like this.

.. code-block:: console

    $ sudo ./src/vf/x86_64-native-linuxapp-gcc/spp_vf \
      -l 0,2-13 -n 4 \
      --proc-type=secondary \
      -- \
      --client-id 1 \
      -s 127.0.0.1:6666 \
      --vhost-client

If ``--vhost-client`` option is specified, then ``vhost-user`` act as
the client, otherwise the server.
For reconnect feature from SPP to VM, ``--vhost-client`` option can be
used. This reconnect features requires QEMU 2.7 (or later).
See also `Vhost Sample Application
<http://dpdk.org/doc/guides/sample_app_ug/vhost.html>`_.


.. _spp_vf_gsg_howto_use_spp_mirror:

spp_mirror
----------

``spp_mirror`` takes the same options as ``spp_vf``. Here is an example.

.. code-block:: console

    $ sudo ./src/mirror/x86_64-native-linuxapp-gcc/spp_mirror \
      -l 2 -n 4 \
      --proc-type=secondary \
      -- \
      --client-id 1 \
      -s 127.0.0.1:6666 \
      -vhost-client

.. _spp_vf_gsg_howto_use_spp_pcap:

spp_pcap
--------

After run ``spp_primary`` is launched, run secondary process ``spp_pcap``.

.. code-block:: console

    $ sudo ./src/pcap/x86_64-native-linuxapp-gcc/spp_pcap \
      -l 0-3 -n 4 \
      --proc-type=secondary \
      -- \
      --client-id 1 \
      -s 127.0.0.1:6666 \
      -i phy:0 \
      --output /mnt/pcap \
      --limit_file_size 107374182

VM
--

VM is launched with ``virsh`` command.

.. code-block:: console

    $ virsh start [VM]

It is required to add network configuration for processes running on the VMs.
If this configuration is skipped, processes cannot communicate with others
via SPP.

On the VMs, add an interface and disable offload.

.. code-block:: console

    # Add interface
    $ sudo ifconfig [IF_NAME] inet [IP_ADDR] netmask [NETMASK] up

    # Disable offload
    $ sudo ethtool -K [IF_NAME] tx off

On host machine, it is also required to disable offload.

.. code-block:: console

    # Disable offload for VM
    $ sudo ethtool -K [IF_NAME] tx off
