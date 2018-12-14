..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _spp_pcap_gsg_build:


This section assumes DPDK is already installed
and setup is already done properly.

Install tools
-------------

To uncompress compressed captured files, you need to install lz4-utils.

.. code-block:: console

    $sudo apt-get install liblz4-tool

To merge captured files, you need to install wireshark.

.. code-block:: console

    $ sudo apt-get install wireshark

Install spp_pcap
----------------

Clone SPP in any directory and compile it.

.. code-block:: console

    $ cd /path/to/any_dir
    $ git clone http://dpdk.org/git/apps/spp
    $ cd spp
    $ make


How to launch spp_pcap
----------------------

``spp_pcap`` can be launched with two kinds of options, like primary process.

Like primary process, ``spp_pcap`` has two kinds of options. One is for
DPDK, the other is ``spp_pcap``.

.. code-block:: console
   sudo ./src/pcap/x86_64-native-linuxapp-gcc/spp_pcap \
   -l 0-3 -n 4 --proc-type=secondary \
   -- --client-id 1 -s 127.0.0.1:6666 \
   -i phy:0 --output /mnt/pcap --limit_file_size 1073741824


``spp_pcap`` specific options are:

  * --client-id: client id which can be seen as secondary ID from spp.py
  * -s: IPv4 address and port for spp-ctl
  * -i: port to which spp_pcap attached with
  * --output: output directory where capture file will be written.
    When omitted,``/tmp`` is used.
  * --limit_file_size: file size in byte when exceeded this size, spp_pcap
    will create new file.
    When omitted, ``1073741824`` is used.
