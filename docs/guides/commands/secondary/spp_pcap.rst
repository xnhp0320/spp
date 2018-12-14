..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

.. _commands_spp_pcap:

spp_pcap
========

``spp_pcap`` is a kind of SPP secondary process. It it introduced for
providing packet capture features.

Each of ``spp_pcap`` processes is managed with ``pcap`` command. It is for
sending sub commands with specific ID called secondary ID for starting or
stopping packet capture.

Secondary ID is referred as ``--client-id`` which is given as an argument
while launching ``spp_pcap``. It should be unique among all of secondary
processes including ``spp_nfv``, ``spp_vm`` and others.

``pcap`` command takes an secondary ID and one of sub commands. Secondary ID
and sub command should be separated with delimiter ``;``, or failed to a
command error.

.. code-block:: console

    spp > pcap SEC_ID; SUB_CMD

In this example, ``SEC_ID`` is a secondary ID and ``SUB_CMD`` is one of the
following sub commands. Details of each of sub commands are described in the
next sections.

* exit
* start
* status
* stop

``spp_pcap`` supports TAB completion. You can complete all of the name
of commands and its arguments. For instance, you find all of sub commands
by pressing TAB after ``pcap SEC_ID;``.

.. code-block:: console

    spp > pcap 1;  # press TAB key
    exit  start      status        stop

It tries to complete all of possible arguments.

.. code-block:: console

    spp > pcap 1; component st  # press TAB key to show args starting 'st'
    start  status  stop

If you are reached to the end of arguments, no candidate keyword is displayed.
It is a completed statement of ``start`` command, and TAB
completion does not work after ``start`` because it is ready to run.

.. code-block:: console

    spp > pcap 1; start
    Succeeded to start capture

It is also completed secondary IDs of ``spp_pcap`` and it is helpful if you run several ``spp_pcap`` processes.

.. code-block:: console

    spp > pcap  # press TAB after space following 'pcap'
    1;  3;    # you find two spp_pcap processes of sec ID 1, 3

By the way, it is also a case of no candidate keyword is displayed if your
command statement is wrong. You might be encountered an error if you run the
wrong command. Please take care.

.. code-block:: console

    spp > pcap 1; ste  # no candidate shown for wrong command
    Invalid command "ste".


.. _commands_spp_pcap_status:

status
------

Show the information of worker threads and its resources. Status information
consists of three parts.

.. code-block:: console

    spp > pcap 1; status
    Basic Information:
      - client-id: 3
      - status: running
      - core:2 'receive'
        - rx: phy:0
      - core:3 'write'
        - file:/tmp/spp_pcap.20181108110600.phy0.1.0.pcap
      - core:4 'write'
        - file:/tmp/spp_pcap.20181108110600.phy0.2.0.pcap
      - core:5 'write'
        - file:/tmp/spp_pcap.20181108110600.phy0.3.0.pcap

``Basic Information`` is for describing attributes of ``spp_pcap`` itself.
``client-id`` is a secondary ID of the process and ``status`` shows the
status of the process.

Then lists of core IDs and its role is shown. There are two types of the role
``receive`` and ``write``. If the role is ``receive``, port which ``spp_pcap``
is attached to is shown. Else if the role iw ``write``, file name in absolute
path is shown.

.. _commands_spp_pcap_start:

start
-----

Start packet capture. No additional arguments are taken.

.. code-block:: console

    # start capture
    spp > pcap SEC_ID; start

Here is a example of starting capture with ``start`` command.

.. code-block:: console

    # start capture
    spp > pcap 2; start

.. _commands_spp_pcap_stop:

stop
----

Stop packet capture. No additional arguments are taken.

.. code-block:: console

   # start capture
   spp > pcap SEC_ID; stop

Here is a example of stopping capture with ``stop`` command.

.. code-block:: console

    # stop capture
    spp > pcap 2; stop

.. _commands_spp_pcap_exit:

exit
----

Terminate the ``spp_pcap``.

.. code-block:: console

    spp > pcap 1; exit
