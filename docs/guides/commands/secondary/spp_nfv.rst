..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2010-2014 Intel Corporation

spp_nfv
=======

Each of ``spp_nfv`` and ``spp_vm`` processes is managed with ``sec`` command.
It is for sending sub commands to secondary with specific ID called
secondary ID.

``sec`` command takes an secondary ID and a sub command. They must be
separated with delimiter ``;``.
Some of sub commands take additional arguments for speicfying resource
owned by secondary process.

.. code-block:: console

    spp > sec SEC_ID; SUB_CMD

All of Sub commands are referred with ``help`` command.

.. code-block:: console

    spp > help sec

    Send a command to secondary process specified with ID.

        SPP secondary process is specified with secondary ID and takes
        sub commands.

        spp > sec 1; status
        spp > sec 1; add ring:0
        spp > sec 1; patch phy:0 ring:0

        You can refer all of sub commands by pressing TAB after
        'sec 1;'.

        spp > sec 1;  # press TAB
        add     del     exit    forward patch   status  stop

status
------

Show running status and ports assigned to the process. If a port is
patched to other port, source and destination ports are shown, or only
source if it is not patched.

.. code-block:: console

    spp > sec 1; status
    - status: idling
    - ports:
      - phy:0 -> ring:0
      - phy:1


add
---

Add a port to the secondary with resource ID.

For example, adding ``ring:0`` by

.. code-block:: console

    spp> sec 1; add ring:0

Or adding ``vhost:0`` by

.. code-block:: console

    spp> sec 1; add vhost:0


patch
------

Create a path between two ports, source and destination ports.
This command just creates a path and does not start forwarding.

.. code-block:: console

    spp > sec 1; patch phy:0 ring:0


forward
-------

Start forwarding.

.. code-block:: console

    spp > sec 1; forward

Running status is changed from ``idling`` to ``running`` by
executing it.

.. code-block:: console

    spp > sec 1; status
    - status: running
    - ports:
      - phy:0
      - phy:1


stop
----

Stop forwarding.

.. code-block:: console

    spp > sec 1; stop

Running status is changed from ``running`` to ``idling`` by
executing it.

.. code-block:: console

    spp > sec 1; status
    - status: idling
    - ports:
      - phy:0
      - phy:1


del
---

Delete a port from the secondary.

.. code-block:: console

    spp> sec 1; del ring:0


exit
----

Terminate the secondary. For terminating all secondaries,
use ``bye sec`` command instead of it.

.. code-block:: console

    spp> sec 1; exit