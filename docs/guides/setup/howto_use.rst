..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _spp_setup_howto_use:

How to Use
==========

As described in :ref:`Design<spp_overview_design>`, SPP consists of
primary process for managing resources, secondary processes for
forwarding packet, and SPP controller to accept user commands and
send it to SPP processes.

You should keep in mind the order of launching processes.
Primary process must be launched before secondary processes.
``spp-ctl`` need to be launched before SPP CLI, but no need to be launched
before other processes. SPP CLI is launched from ``spp.py``.
If ``spp-ctl`` is not running after primary and
secondary processes are launched, processes wait ``spp-ctl`` is launched.

In general, ``spp-ctl`` should be launched first, then SPP CLI and
``spp_primary`` in each of terminals without running as background process.
After ``spp_primary``, you launch secondary processes for your usage.
If you just patch two DPDK applications on host, it is enough to use one
``spp_nfv``, or use ``spp_vf`` if you need to classify packets.
How to use of these secondary processes is described in next chapters.


SPP Controller
--------------

SPP Controller consists of ``spp-ctl`` and SPP CLI.

spp-ctl
~~~~~~~

``spp-ctl`` is a HTTP server for REST APIs for managing SPP
processes. In default, it is accessed with URL ``http://127.0.0.1:7777``
or ``http://localhost:7777``.
``spp-ctl`` shows no messages at first after launched, but shows
log messages for events such as receiving a request or terminating
a process.

.. code-block:: console

    # terminal 1
    $ cd /path/to/spp
    $ python3 src/spp-ctl/spp-ctl

Notice that ``spp-ctl`` is implemented in ``python3`` and cannot be
launched with ``python`` or ``python2``.

It has a option ``-b`` for binding address explicitly to be accessed
from other than default, ``127.0.0.1`` or ``localhost``.
If you deploy SPP on multiple nodes, you might need to use ``-b`` option
to be accessed from other processes running on other than local node.

.. code-block:: console

    # launch with URL http://192.168.1.100:7777
    $ python3 src/spp-ctl/spp-ctl -b 192.168.1.100

``spp-ctl`` is also launched as a daemon process, or managed
by ``systemd``.
Here is a simple example of service file for systemd.

.. code-block:: none

    [Unit]
    Description = SPP Controller

    [Service]
    ExecStart = /usr/bin/python3 /path/to/spp/src/spp-ctl/spp-ctl
    User = root

All of options can be referred with help option ``-h``.

.. code-block:: console

    python3 ./src/spp-ctl/spp-ctl -h
    usage: spp-ctl [-h] [-b BIND_ADDR] [-p PRI_PORT] [-s SEC_PORT] [-a API_PORT]

    SPP Controller

    optional arguments:
      -h, --help            show this help message and exit
      -b BIND_ADDR, --bind-addr BIND_ADDR
                            bind address, default=localhost
      -p PRI_PORT           primary port, default=5555
      -s SEC_PORT           secondary port, default=6666
      -a API_PORT           web api port, default=7777

.. _spp_setup_howto_use_spp_cli:

SPP CLI
~~~~~~~

If ``spp-ctl`` is launched, go to the next terminal and launch SPP CLI.
It supports both of Python 2 and 3, so use ``python`` in this case.

.. code-block:: console

    # terminal 2
    $ cd /path/to/spp
    $ python src/spp.py
    Welcome to the spp.   Type help or ? to list commands.

    spp >

If you launched ``spp-ctl`` with ``-b`` option, you also need to use the same
option for ``spp.py``, or failed to connect and to launch.

.. code-block:: console

    # to send request to http://192.168.1.100:7777
    $ python src/spp.py -b 192.168.1.100
    Welcome to the spp.   Type help or ? to list commands.

    spp >

One of the typical usecase of this option is to deploy multiple SPP nodes.
:numref:`figure_spp_howto_multi_spp` is an exmaple of multiple nodes case.
There are three nodes on each of which ``spp-ctl`` is running for accepting
requests for SPP. These ``spp-ctl`` processes are controlled from
``spp.py`` on host1 and all of paths are configured across the nodes.
It is also able to be configured between hosts by changing
soure or destination of phy ports.

.. _figure_spp_howto_multi_spp:

.. figure:: ../images/setup/howto_use/spp_howto_multi_spp.*
   :width: 80%

   Multiple SPP nodes

Launch SPP CLI with three entries of binding addresses with ``-b`` option
for specifying ``spp-ctl``. Here is an example.

.. code-block:: console

    # Launch SPP CLI
    $ python src/spp.py -b 192.168.11.101 \
        -b 192.168.11.102 \
        -b 192.168.11.103 \

You can find the host under the management of SPP CLI and switch with
``server`` command.

.. code-block:: console

    spp > server list
      1: 192.168.1.101:7777 *
      2: 192.168.1.102:7777
      3: 192.168.1.103:7777

To change the server, add an index number after ``server``.

.. code-block:: console

    # Launch SPP CLI
    spp > server 3
    Switch spp-ctl to "3: 192.168.1.103:7777".

All of options can be referred with help option ``-h``.

.. code-block:: console

    $ python src/spp.py -h
    usage: spp.py [-h] [-b BIND_ADDR] [-a API_PORT]

    SPP Controller

    optional arguments:
      -h, --help            show this help message and exit
      -b BIND_ADDR, --bind-addr BIND_ADDR
                            bind address, default=127.0.0.1
      -a API_PORT, --api-port API_PORT
                        bind address, default=777

All of SPP CLI commands are described in :doc:`../../commands/index`.


SPP Primary
-----------

SPP primary is a resource manager and has a responsibility for
initializing EAL for secondary processes. It should be launched before
secondary.

To launch SPP primary, run ``spp_primary`` with specific options.

.. code-block:: console

    # terminal 3
    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
        -l 1 -n 4 \
        --socket-mem 512,512 \
        --huge-dir=/dev/hugepages \
        --proc-type=primary \
        -- \
        -p 0x03 \
        -n 10 \
        -s 192.168.1.100:5555

SPP primary takes EAL options and application specific options.

Core list option ``-l`` is for assigining cores and SPP primary requires just
one core. You can use core mask option ``-c`` instead of ``-l``.

You can use ``-m`` for memory reservation instead of ``--socket-mem`` if you
use single NUMA node.

.. note::

    Spp primary shows messages in the terminal after launched. However, the
    contents of the message is different for the number of lcores assigned.

    If you assign two lcores, SPP primary show statistics within
    interval time periodically. On the other hand, just one lcore, it shows
    log messages.

    Anyway, you can retrieve it with ``status`` command of spp_primary.
    The second core of spp_primary is not used for counting
    packets actually, but used just for displaying the statistics.

Primary process sets up physical ports of given port mask with ``-p`` option
and ring ports of the number of ``-n`` option. Ports of  ``-p`` option is for
accepting incomming packets and ``-n`` option is for inter-process packet
forwarding. You can also add ports initialized with ``--vdev`` option to
physical ports. However, ports added with ``--vdev`` cannot referred from
secondary processes.

.. code-block:: console

    $ sudo ./src/primary/x86_64-native-linuxapp-gcc/spp_primary \
        -l 1 -n 4 \
        --socket-mem 512,512 \
        --huge-dir=/dev/hugepages \
        --vdev eth_vhost1,iface=/tmp/sock1  # used as 1st phy port
        --vdev eth_vhost2,iface=/tmp/sock2  # used as 2nd phy port
        --proc-type=primary \
        -- \
        -p 0x03 \
        -n 10 \
        -s 192.168.1.100:5555

- EAL options:

  - -l: core list
  - --socket-mem: memory size on each of NUMA nodes
  - --huge-dir: path of hugepage dir
  - --proc-type: process type

- Application options:

  - -p: port mask
  - -n: number of ring PMD
  - -s: IP address of controller and port prepared for primary


SPP Secondary
-------------

Secondary process behaves as a client of primary process and a worker
for doing tasks for packet processing.

This section describes about ``spp_nfv`` and ``spp_vm``,
which just simply forward packets similar to ``l2fwd``.
The difference between them is running on host or VM.
``spp_vm`` runs inside a VM as described in name.


Launch spp_nfv on Host
~~~~~~~~~~~~~~~~~~~~~~

Run ``spp_nfv`` with options.

.. code-block:: console

    # terminal 4
    $ cd /path/to/spp
    $ sudo ./src/nfv/x86_64-native-linuxapp-gcc/spp_nfv \
        -l 2-3 -n 4 \
        --proc-type=secondary \
        -- \
        -n 1 \
        -s 192.168.1.100:6666

- EAL options:

  - -l: core list (two cores required)
  - --proc-type: process type

- Application options:

  - -n: secondary ID
  - -s: IP address of controller and port prepared for secondary

Secondary ID is used to identify for sending messages and must be
unique among all of secondaries.
If you attempt to launch a secondary process with the same ID, it
is failed.

Launch spp_vf on VM
~~~~~~~~~~~~~~~~~~~

To communicate DPDK application running on a VM,
it is required to create a virtual device for the VM.
In this instruction, launch a VM with qemu command and
create ``vhost-user`` and ``virtio-net-pci`` devices on the VM.

Before launching VM, you need to prepare a socket file for creating
``vhost-user`` device.
Run ``add`` command with resource UID ``vhost:0`` to create socket file.

.. code-block:: console

    spp > nfv 1; add vhost:0

In this example, create socket file with index 0 from ``spp_nfv`` of ID 1.
Socket file is created as ``/tmp/sock0``.
It is used as a qemu option to add vhost interface.

Launch VM with ``qemu-system-x86_64`` for x86 64bit architecture.
Qemu takes many options for defining resources including virtual
devices.

.. code-block:: console

    $ sudo qemu-system-x86_64 \
        -cpu host \
        -enable-kvm \
        -numa node,memdev=mem \
        -mem-prealloc \
        -hda /path/to/image.qcow2 \
        -m 4096 \
        -smp cores=4,threads=1,sockets=1 \
        -object \
        memory-backend-file,id=mem,size=4096M,mem-path=/dev/hugepages,share=on \
        -device e1000,netdev=net0,mac=00:AD:BE:B3:11:00 \
        -netdev tap,id=net0,ifname=net0,script=/path/to/qemu-ifup \
        -nographic \
        -chardev socket,id=chr0,path=/tmp/sock0 \  # /tmp/sock0
        -netdev vhost-user,id=net1,chardev=chr0,vhostforce \
        -device virtio-net-pci,netdev=net1,mac=00:AD:BE:B4:11:00 \
        -monitor telnet::44911,server,nowait

This VM has two network interfaces.
``-device e1000`` is a management network port
which requires ``qemu-ifup`` to activate while launching.
Management network port is used for login and setup the VM.
``-device virtio-net-pci`` is created for SPP or DPDK application
running on the VM.

``vhost-user`` is a backend of ``virtio-net-pci`` which requires
a socket file ``/tmp/sock0`` created from secondary with ``-chardev``
option.

For other options, please refer to
`QEMU User Documentation
<https://qemu.weilnetz.de/doc/qemu-doc.html>`_.

.. note::

    In general, you need to prepare several qemu images for launcing
    several VMs, but installing DPDK and SPP for several images is bother
    and time consuming.

    You can shortcut this tasks by creating a template image and copy it
    to the VMs. It is just one time for installing for template.

After booted, you install DPDK and SPP in the VM as in the host.

Run ``spp_vm`` with options.

.. code-block:: console

    $ cd /path/to/spp
    $ sudo ./src/vm/x86_64-native-linuxapp-gcc/spp_vm \
        -l 0-1 -n 4 \
        --proc-type=primary \
        -- \
        -p 0x01 \
        -n 1 \
        -s 192.168.1.100:6666

- EAL options:

  - -l: core list (two cores required)
  - --proc-type: process type

- Application options:

  - -p: port mask
  - -n: secondary ID
  - -s: IP address of controller and port prepared for secondary

``spp_vm`` is also managed from SPP controller as same as on host.
Secondary ID is used to identify for sending messages and must be
unique among all of secondaries.
If you attempt to launch a secondary process with the same ID,
it is failed.

In this case, port mask option is ``-p 0x01`` (using one port) because
the VM is launched with just one vhost interface.
You can use two or more ports if you launch VM with several
``vhost-user`` and ``virtio-net-pci`` interfaces.

Notice that ``spp_vm`` takes options similar to ``spp_primary``, not
``spp_nfv``.
It means that ``spp_vm`` has responsibilities for initializing EAL
and forwarding packets in the VM.

.. note::

    ``spp_vm`` is actually running as primary process on a VM,
    but managed as secondary process from SPP controller.
    SPP does not support running resource manager as primary inside
    a VM. Client behaves as secondary, but actually a primary, running
    on the VM to communicate with other SPP procesess on host.

    ``spp_vm`` must be launched with ``--proc-type=primary`` and
    ``-p [PORTMASK]`` options similar to primary to initialize EAL.
