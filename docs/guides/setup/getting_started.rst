..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation
    Copyright(c) 2017-2019 Nippon Telegraph and Telephone Corporation

.. _getting_started:

Getting Started
===============

This documentation is described for Ubuntu 16.04 and later.

Setup
-----

Reserving Hugepages
~~~~~~~~~~~~~~~~~~~

Hugepages must be enabled for running DPDK with high performance.
Hugepage support is required to reserve large amount size of pages,
2MB or 1GB per page, to less TLB (Translation Lookaside Buffers) and
to reduce cache miss.
Less TLB means that it reduce the time for translating virtual address
to physical.

Hugepage reservation might be different for 2MB or 1GB.

For 1GB page, hugepage setting must be activated while booting system.
It must be defined in boot loader configuration, usually is
``/etc/default/grub``.
Add an entry to define pagesize and the number of pages.
Here is an example. ``hugepagesz`` is for the size and ``hugepages``
is for the number of pages.

.. code-block:: console

    # /etc/default/grub
    GRUB_CMDLINE_LINUX="default_hugepagesz=1G hugepagesz=1G hugepages=8"

.. note::

    1GB hugepages might not be supported in your machine. It depends on
    that CPUs support 1GB pages or not. You can check it by referring
    ``/proc/cpuinfo``. If it is supported, you can find ``pdpe1gb`` in
    the ``flags`` attribute.

    .. code-block:: console

        $ cat /proc/cpuinfo | grep pdpe1gb
        flags           : fpu vme ... pdpe1gb ...

You should run ``update-grub`` after editing to update grub's config file,
or this configuration is not activated.

.. code-block:: console

   $ sudo update-grub
   Generating grub configuration file ...

For 2MB page, you can activate hugepages while booting or at anytime
after system is booted.
Define hugepages setting in ``/etc/default/grub`` to activate it while
booting, or overwrite the number of 2MB hugepages as following.

.. code-block:: console

    $ echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

In this case, 1024 pages of 2MB (totally 2048 MB) are reserved.


Mount hugepages
~~~~~~~~~~~~~~~

Make the memory available for using hugepages from DPDK.

.. code-block:: console

    $ mkdir /mnt/huge
    $ mount -t hugetlbfs nodev /mnt/huge

It is also available while booting by adding a configuration of mount
point in ``/etc/fstab``, or after booted.

The mount point for 2MB or 1GB can be made permanent accross reboot.
For 2MB, it is no need to declare the size of hugepages explicity.

.. code-block:: console

    # /etc/fstab
    nodev /mnt/huge hugetlbfs defaults 0 0

For 1GB, the size of hugepage must be specified.

.. code-block:: console

    # /etc/fstab
    nodev /mnt/huge_1GB hugetlbfs pagesize=1GB 0 0


Disable ASLR
~~~~~~~~~~~~

SPP is a DPDK multi-process application and there are a number of
`limitations
<https://dpdk.org/doc/guides/prog_guide/multi_proc_support.html#multi-process-limitations>`_
.

Address-Space Layout Randomization (ASLR) is a security feature for
memory protection, but may cause a failure of memory
mapping while starting multi-process application as discussed in
`dpdk-dev
<http://dpdk.org/ml/archives/dev/2014-September/005236.html>`_
.

ASLR can be disabled by assigning ``kernel.randomize_va_space`` to
``0``, or be enabled by assigning it to ``2``.

.. code-block:: console

    # disable ASLR
    $ sudo sysctl -w kernel.randomize_va_space=0

    # enable ASLR
    $ sudo sysctl -w kernel.randomize_va_space=2

You can check the value as following.

.. code-block:: console

    $ sysctl -n kernel.randomize_va_space

.. _install_dpdk_spp:

Install DPDK and SPP
--------------------

Before using SPP, you need to install DPDK.
In this document, briefly describ how to install and setup DPDK.
Refer to `DPDK documentation
<https://dpdk.org/doc/guides/>`_ for more details.
For Linux, see `Getting Started Guide for Linux
<http://www.dpdk.org/doc/guides/linux_gsg/index.html>`_ .

DPDK
~~~~

Clone repository and compile DPDK in any directory.

.. code-block:: console

    $ cd /path/to/any
    $ git clone http://dpdk.org/git/dpdk

To compile DPDK, required to install libnuma-devel library.

.. code-block:: console

    $ sudo apt install libnuma-dev

Python and pip are also required if not installed.

.. code-block:: console

    # Python2
    $ sudo apt install python python-pip

    # Python3
    $ sudo apt install python3 python3-pip

SPP provides libpcap-based PMD for dumping packet to a file or retrieve
it from the file.
To use PCAP PMD, install ``libpcap-dev`` and enable it.
``text2pcap`` is also required for creating pcap file which
is included in ``wireshark``.

.. code-block:: console

    $ sudo apt install libpcap-dev
    $ sudo apt install wireshark

PCAP is disabled by default in DPDK configuration.
``CONFIG_RTE_LIBRTE_PMD_PCAP`` and ``CONFIG_RTE_PORT_PCAP`` define the
configuration and enabled it to ``y``.

.. code-block:: console

    # dpdk/config/common_base
    CONFIG_RTE_LIBRTE_PMD_PCAP=y
    ...
    CONFIG_RTE_PORT_PCAP=y

Compile DPDK with target environment.

.. code-block:: console

    $ cd dpdk
    $ export RTE_SDK=$(pwd)
    $ export RTE_TARGET=x86_64-native-linuxapp-gcc  # depends on your env
    $ make install T=$RTE_TARGET


SPP
~~~

Clone repository and compile SPP in any directory.

.. code-block:: console

    $ cd /path/to/any
    $ git clone http://dpdk.org/git/apps/spp
    $ cd spp
    $ make  # Confirm that $RTE_SDK and $RTE_TARGET are set

It also required to install Python3 and packages for running python scripts
as following.
You might need to run ``pip3`` with ``sudo`` if it is failed.

.. code-block:: console

    $ sudo apt update
    $ sudo apt install python3
    $ sudo apt install python3-pip
    $ pip3 install -r requirements.txt


Python 2 or 3 ?
~~~~~~~~~~~~~~~

In SPP, Python3 is required only for running ``spp-ctl``. Other python scripts
are able to be launched both of Python2 and 3.

Howevrer, Python2 will not be maintained after 2020 and SPP is going to update
only supporting Python3.
In SPP, it is planned to support only Python3 before the end of 2019.


Binding Network Ports to DPDK
-----------------------------

Network ports must be bound to DPDK with a UIO (Userspace IO) driver.
UIO driver is for mapping device memory to userspace and registering
interrupts.

UIO Drivers
~~~~~~~~~~~

You usually use the standard ``uio_pci_generic`` for many use cases
or ``vfio-pci`` for more robust and secure cases.
Both of drivers are included by default in modern Linux kernel.

.. code-block:: console

    # Activate uio_pci_generic
    $ sudo modprobe uio_pci_generic

    # or vfio-pci
    $ sudo modprobe vfio-pci

You can also use kmod included in DPDK instead of ``uio_pci_generic``
or ``vfio-pci``.

.. code-block:: console

    $ sudo modprobe uio
    $ sudo insmod kmod/igb_uio.ko

Binding Network Ports
~~~~~~~~~~~~~~~~~~~~~

Once UIO driver is activated, bind network ports with the driver.
DPDK provides ``usertools/dpdk-devbind.py`` for managing devices.

Find ports for binding to DPDK by running the tool with ``-s`` option.

.. code-block:: console

    $ $RTE_SDK/usertools/dpdk-devbind.py --status

    Network devices using DPDK-compatible driver
    ============================================
    <none>

    Network devices using kernel driver
    ===================================
    0000:29:00.0 '82571EB ... 10bc' if=enp41s0f0 drv=e1000e unused=
    0000:29:00.1 '82571EB ... 10bc' if=enp41s0f1 drv=e1000e unused=
    0000:2a:00.0 '82571EB ... 10bc' if=enp42s0f0 drv=e1000e unused=
    0000:2a:00.1 '82571EB ... 10bc' if=enp42s0f1 drv=e1000e unused=

    Other Network devices
    =====================
    <none>
    ....

You can find network ports are bound to kernel driver and not to DPDK.
To bind a port to DPDK, run ``dpdk-devbind.py`` with specifying a driver
and a device ID.
Device ID is a PCI address of the device or more friendly style like
``eth0`` found by ``ifconfig`` or ``ip`` command..

.. code-block:: console

    # Bind a port with 2a:00.0 (PCI address)
    ./usertools/dpdk-devbind.py --bind=uio_pci_generic 2a:00.0

    # or eth0
    ./usertools/dpdk-devbind.py --bind=uio_pci_generic eth0


After binding two ports, you can find it is under the DPDK driver and
cannot find it by using ``ifconfig`` or ``ip``.

.. code-block:: console

    $ $RTE_SDK/usertools/dpdk-devbind.py -s

    Network devices using DPDK-compatible driver
    ============================================
    0000:2a:00.0 '82571EB ... 10bc' drv=uio_pci_generic unused=vfio-pci
    0000:2a:00.1 '82571EB ... 10bc' drv=uio_pci_generic unused=vfio-pci

    Network devices using kernel driver
    ===================================
    0000:29:00.0 '...' if=enp41s0f0 drv=e1000e unused=vfio-pci,uio_pci_generic
    0000:29:00.1 '...' if=enp41s0f1 drv=e1000e unused=vfio-pci,uio_pci_generic

    Other Network devices
    =====================
    <none>
    ....


Confirm DPDK is setup properly
------------------------------

You can confirm if you are ready to use DPDK by running DPDK's sample
application. ``l2fwd`` is good choice to confirm it before SPP because
it is very similar to SPP's worker process for forwarding.

.. code-block:: console

   $ cd $RTE_SDK/examples/l2fwd
   $ make
     CC main.o
     LD l2fwd
     INSTALL-APP l2fwd
     INSTALL-MAP l2fwd.map

In this case, run this application simply with just two options
while DPDK has many kinds of options.

  - -l: core list
  - -p: port mask

.. code-block:: console

   $ sudo ./build/app/l2fwd \
     -l 1-2 \
     -- -p 0x3

It must be separated with ``--`` to specify which option is
for EAL or application.
Refer to `L2 Forwarding Sample Application
<https://dpdk.org/doc/guides/sample_app_ug/l2_forward_real_virtual.html>`_
for more details.


Build Documentation
-------------------

This documentation is able to be biult as HTML and PDF formats from make
command. Before compiling the documentation, you need to install some of
packages required to compile.

For HTML documentation, install sphinx and additional theme.

.. code-block:: console

    $ pip install sphinx
    $ pip install sphinx-rtd-theme

For PDF, inkscape and latex packages are required.

.. code-block:: console

    $ sudo apt install inkscape
    $ sudo apt install texlive-latex-extra
    $ sudo apt install texlive-latex-recommended

HTML documentation is compiled by running make with ``doc-html``. This
command launch sphinx for compiling HTML documents.
Compiled HTML files are created in ``docs/guides/_build/html/`` and
You can find the top page ``index.html`` in the directory.

.. code-block:: console

    $ make doc-html

PDF documentation is compiled with ``doc-pdf`` which runs latex for.
Compiled PDF file is created as ``docs/guides/_build/html/SoftPatchPanel.pdf``.

.. code-block:: console

    $ make doc-pdf

You can also compile both of HTML and PDF documentations with ``doc`` or
``doc-all``.

.. code-block:: console

    $ make doc
    # or
    $ make doc-all
