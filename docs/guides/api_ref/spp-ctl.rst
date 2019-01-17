..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2018-2019 Nippon Telegraph and Telephone Corporation

.. _spp_ctl_rest_api_ref:

spp-ctl REST API
================

Overview
--------

``spp-ctl`` provides simple REST like API. It supports http only, not https.

Request and Response
~~~~~~~~~~~~~~~~~~~~

Request body is JSON format.
It is accepted both ``text/plain`` and ``application/json``
for the content-type header.

A response of ``GET`` is JSON format and the content-type is
``application/json`` if the request success.

.. code-block:: console

    $ curl http://127.0.0.1:7777/v1/processes
    [{"type": "primary"}, ..., {"client-id": 2, "type": "vf"}]

    $ curl -X POST http://localhost:7777/v1/vfs/1/components \
      -d '{"core": 2, "name": "forward_0_tx", "type": "forward"}'

If a request is failed, the response is a text which shows error reason
and the content-type is ``text/plain``.


Error code
~~~~~~~~~~


``spp-ctl`` does basic syntax and lexical check of a request.

.. _table_spp_ctl_error_codes:

.. table:: Error codes in spp-ctl.

    +-------+----------------------------------------------------------------+
    | Error | Description                                                    |
    |       |                                                                |
    +=======+================================================================+
    | 400   | Syntax or lexical error, or SPP returns error for the request. |
    +-------+----------------------------------------------------------------+
    | 404   | URL is not supported, or no SPP process of client-id in a URL. |
    +-------+----------------------------------------------------------------+
    | 500   | System error occured in ``spp-ctl``.                           |
    +-------+----------------------------------------------------------------+


API independent of the process type
-----------------------------------

GET /v1/processes
~~~~~~~~~~~~~~~~~

Show the SPP processes connected to the ``spp-ctl``.

* Normarl response codes: 200

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/processes

Response
^^^^^^^^

An array of process objects.

process object:

.. _table_spp_ctl_processes:

.. table:: Response params of getting processes info.

    +-----------+---------+-----------------------------------------------------------------+
    | Name      | Type    | Description                                                     |
    |           |         |                                                                 |
    +===========+=========+=================================================================+
    | type      | string  | process type. one of ``primary``, ``nfv`` or ``vf``.            |
    +-----------+---------+-----------------------------------------------------------------+
    | client-id | integer | client id. if type is ``primary`` this member does not exist.   |
    +-----------+---------+-----------------------------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    [
      {
        "type": "primary"
      },
      {
        "type": "vf",
        "client-id": 1
      },
      {
        "type": "nfv",
        "client-id": 2
      }
    ]


API for spp_primary
-------------------

GET /v1/primary/status
~~~~~~~~~~~~~~~~~~~~~~

Show statistical information.

* Normal response codes: 200

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/primary/status

Response
^^^^^^^^

.. _table_spp_ctl_primary_status:

.. table:: Response params of primary status.

    +------------+-------+-------------------------------------------+
    | Name       | Type  | Description                               |
    |            |       |                                           |
    +============+=======+===========================================+
    | phy_ports  | array | An array of statistics of physical ports. |
    +------------+-------+-------------------------------------------+
    | ring_ports | array | An array of statistics of ring ports.     |
    +------------+-------+-------------------------------------------+

Physical port object.

.. _table_spp_ctl_primary_status_phy:

.. table:: Attributes of physical port of primary status.

    +---------+---------+-----------------------------------------------------+
    | Name    | Type    | Description                                         |
    |         |         |                                                     |
    +=========+=========+=====================================================+
    | id      | integer | Port ID of the physical port.                       |
    +---------+---------+-----------------------------------------------------+
    | rx      | integer | The total number of received packets.               |
    +---------+---------+-----------------------------------------------------+
    | tx      | integer | The total number of transferred packets.            |
    +---------+---------+-----------------------------------------------------+
    | tx_drop | integer | The total number of dropped packets of transferred. |
    +---------+---------+-----------------------------------------------------+
    | eth     | string  | MAC address of the port.                            |
    +---------+---------+-----------------------------------------------------+

Ring port object.

.. _table_spp_ctl_primary_status_ring:

.. table:: Attributes of ring port of primary status.

    +---------+---------+-----------------------------------------------------+
    | Name    | Type    | Description                                         |
    |         |         |                                                     |
    +=========+=========+=====================================================+
    | id      | integer | Port ID of the ring port.                           |
    +---------+---------+-----------------------------------------------------+
    | rx      | integer | The total number of received packets.               |
    +---------+---------+-----------------------------------------------------+
    | rx_drop | integer | The total number of dropped packets of received.    |
    +---------+---------+-----------------------------------------------------+
    | tx      | integer | The total number of transferred packets.            |
    +---------+---------+-----------------------------------------------------+
    | tx_drop | integer | The total number of dropped packets of transferred. |
    +---------+---------+-----------------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    {
      "phy_ports": [
        {
          "id": 0,
          "rx": 0,
          "tx": 0,
          "tx_drop": 0,
          "eth": "56:48:4f:53:54:00"
        },
        {
          "id": 1,
          "rx": 0,
          "tx": 0,
          "tx_drop": 0,
          "eth": "56:48:4f:53:54:01"
        }
      ],
      "ring_ports": [
        {
          "id": 0,
          "rx": 0,
          "rx_drop": 0,
          "tx": 0,
          "tx_drop": 0
        },
        {
          "id": 1,
          "rx": 0,
          "rx_drop": 0,
          "tx": 0,
          "tx_drop": 0
        },
        {
          "id": 2,
          "rx": 0,
          "rx_drop": 0,
          "tx": 0,
          "tx_drop": 0
        },
        {
          "id": 3,
          "rx": 0,
          "rx_drop": 0,
          "tx": 0,
          "tx_drop": 0
        }
      ]
    }


DELETE /v1/primary/status
~~~~~~~~~~~~~~~~~~~~~~~~~

Clear statistical information.

* Normal response codes: 204

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/primary/status

Response
^^^^^^^^

There is no body content for the response of a successful ``DELETE`` request.

DELETE /v1/primary
~~~~~~~~~~~~~~~~~~

Terminate primary process.

* Normal response codes: 204

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/primary

Response
^^^^^^^^

There is no body content for the response of a successful ``DELETE`` request.


API for spp_nfv
---------------

GET /v1/nfvs/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~~

Get the information of ``spp_nfv``.

* Normal response codes: 200
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_nfvs_get:

.. table:: Request parameter for getting info of ``spp_nfv``.

    +-----------+---------+-------------------------------------+
    | Name      | Type    | Description                         |
    |           |         |                                     |
    +===========+=========+=====================================+
    | client_id | integer | client id.                          |
    +-----------+---------+-------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/nfvs/1

Response
^^^^^^^^

.. _table_spp_ctl_spp_nfv_res:

.. table:: Response params of getting info of ``spp_nfv``.

    +-----------+---------+---------------------------------------------+
    | Name      | Type    | Description                                 |
    |           |         |                                             |
    +===========+=========+=============================================+
    | client-id | integer | client id.                                  |
    +-----------+---------+---------------------------------------------+
    | status    | string  | ``Running`` or ``Idle``.                    |
    +-----------+---------+---------------------------------------------+
    | ports     | array   | an array of port ids used by the process.   |
    +-----------+---------+---------------------------------------------+
    | patches   | array   | an array of patches.                        |
    +-----------+---------+---------------------------------------------+

patch objest

.. _table_spp_ctl_patch_spp_nfv:

.. table:: Attributes of patch command of ``spp_nfv``.

    +------+--------+----------------------------------------------+
    | Name | Type   | Description                                  |
    |      |        |                                              |
    +======+========+==============================================+
    | src  | string | source port id.                              |
    +------+--------+----------------------------------------------+
    | dst  | string | destination port id.                         |
    +------+--------+----------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    {
      "client-id": 1,
      "status": "Running",
      "ports": [
        "phy:0", "phy:1", "vhost:0", "vhost:1", "ring:0", "ring:1", "ring:2", "ring:3"
      ],
      "patches": [
        {
          "src": "vhost:0", "dst": "ring:0"
        },
        {
          "src": "ring:1", "dst": "vhost:1"
        }
      ]
    }

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};status


PUT /v1/nfvs/{client_id}/forward
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start or Stop forwarding.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_forward_get:

.. table:: Request params of forward command of ``spp_nfv``.

    +-----------+---------+---------------------------------+
    | Name      | Type    | Description                     |
    |           |         |                                 |
    +===========+=========+=================================+
    | client_id | integer | client id.                      |
    +-----------+---------+---------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "start"}' \
    http://127.0.0.1:7777/v1/nfvs/1/forward

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_forward_get_body:

.. table:: Request body params of forward of ``spp_nfv``.

    +--------+--------+-------------------------------------+
    | Name   | Type   | Description                         |
    |        |        |                                     |
    +========+========+=====================================+
    | action | string | ``start`` or ``stop``.              |
    +--------+--------+-------------------------------------+

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

action is ``start``

.. code-block:: none

    sec {client_id};forward

action is ``stop``

.. code-block:: none

    sec {client_id};stop


PUT /v1/nfvs/{client_id}/ports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add or Delete port.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_ports_get:

.. table:: Request params of ports of ``spp_nfv``.

    +-----------+---------+--------------------------------+
    | Name      | Type    | Description                    |
    |           |         |                                |
    +===========+=========+================================+
    | client_id | integer | client id.                     |
    +-----------+---------+--------------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_ports_get_body:

.. table:: Request body params of ports of ``spp_nfv``.

    +--------+--------+---------------------------------------------------------------+
    | Name   | Type   | Description                                                   |
    |        |        |                                                               |
    +========+========+===============================================================+
    | action | string | ``add`` or ``del``.                                           |
    +--------+--------+---------------------------------------------------------------+
    | port   | string | port id. port id is the form {interface_type}:{interface_id}. |
    +--------+--------+---------------------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "add", "port": "ring:0"}' \
    http://127.0.0.1:7777/v1/nfvs/1/ports

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};{action} {interface_type} {interface_id}


PUT /v1/nfvs/{client_id}/patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add a patch.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_patches_get:

.. table:: Request params of patches of ``spp_nfv``.

    +-----------+---------+---------------------------------+
    | Name      | Type    | Description                     |
    |           |         |                                 |
    +===========+=========+=================================+
    | client_id | integer | client id.                      |
    +-----------+---------+---------------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_ports_patches_body:

.. table:: Request body params of patches of ``spp_nfv``.

    +------+--------+------------------------------------+
    | Name | Type   | Description                        |
    |      |        |                                    |
    +======+========+====================================+
    | src  | string | source port id.                    |
    +------+--------+------------------------------------+
    | dst  | string | destination port id.               |
    +------+--------+------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"src": "ring:0", "dst": "ring:1"}' \
    http://127.0.0.1:7777/v1/nfvs/1/patches

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};patch {src} {dst}


DELETE /v1/nfvs/{client_id}/patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reset patches.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_nfv_del_patches:

.. table:: Request params of deleting patches of ``spp_nfv``.

    +-----------+---------+---------------------------------------+
    | Name      | Type    | Description                           |
    |           |         |                                       |
    +===========+=========+=======================================+
    | client_id | integer | client id.                            |
    +-----------+---------+---------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/nfvs/1/patches

Response
^^^^^^^^

There is no body content for the response of a successful ``DELETE`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};patch reset


DELETE /v1/nfvs/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Terminate ``spp_nfv``.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_nfvs_delete:

.. table:: Request parameter for terminating ``spp_nfv``.

    +-----------+---------+-------------------------------------+
    | Name      | Type    | Description                         |
    |           |         |                                     |
    +===========+=========+=====================================+
    | client_id | integer | client id.                          |
    +-----------+---------+-------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/nfvs/1

Response example
^^^^^^^^^^^^^^^^

There is no body content for the response of a successful ``DELETE`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id}; exit


API for spp_vf
--------------

GET /v1/vfs/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~

Get the information of the ``spp_vf`` process.

* Normal response codes: 200
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_vfs_get:

.. table:: Request parameter for getting spp_vf.

    +-----------+---------+--------------------------+
    | Name      | Type    | Description              |
    |           |         |                          |
    +===========+=========+==========================+
    | client_id | integer | client id.               |
    +-----------+---------+--------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/vfs/1

Response
^^^^^^^^

.. _table_spp_ctl_spp_vf_res:

.. table:: Response params of getting spp_vf.

    +------------------+---------+-----------------------------------------------+
    | Name             | Type    | Description                                   |
    |                  |         |                                               |
    +==================+=========+===============================================+
    | client-id        | integer | client id.                                    |
    +------------------+---------+-----------------------------------------------+
    | ports            | array   | an array of port ids used by the process.     |
    +------------------+---------+-----------------------------------------------+
    | components       | array   | an array of component objects in the process. |
    +------------------+---------+-----------------------------------------------+
    | classifier_table | array   | an array of classifier tables in the process. |
    +------------------+---------+-----------------------------------------------+

component object:

.. _table_spp_ctl_spp_vf_res_comp:

.. table:: Component objects of getting spp_vf.

    +---------+---------+---------------------------------------------------------------------+
    | Name    | Type    | Description                                                         |
    |         |         |                                                                     |
    +=========+=========+=====================================================================+
    | core    | integer | core id running on the component                                    |
    +---------+---------+---------------------------------------------------------------------+
    | name    | string  | an array of port ids used by the process.                           |
    +---------+---------+---------------------------------------------------------------------+
    | type    | string  | an array of component objects in the process.                       |
    +---------+---------+---------------------------------------------------------------------+
    | rx_port | array   | an array of port objects connected to the rx side of the component. |
    +---------+---------+---------------------------------------------------------------------+
    | tx_port | array   | an array of port objects connected to the tx side of the component. |
    +---------+---------+---------------------------------------------------------------------+

port object:

.. _table_spp_ctl_spp_vf_res_port:

.. table:: Port objects of getting spp_vf.

    +---------+---------+---------------------------------------------------------------+
    | Name    | Type    | Description                                                   |
    |         |         |                                                               |
    +=========+=========+===============================================================+
    | port    | string  | port id. port id is the form {interface_type}:{interface_id}. |
    +---------+---------+---------------------------------------------------------------+
    | vlan    | object  | vlan operation which is applied to the port.                  |
    +---------+---------+---------------------------------------------------------------+

vlan object:

.. _table_spp_ctl_spp_vf_res_vlan:

.. table:: Vlan objects of getting spp_vf.

    +-----------+---------+-------------------------------+
    | Name      | Type    | Description                   |
    |           |         |                               |
    +===========+=========+===============================+
    | operation | string  | ``add``, ``del`` or ``none``. |
    +-----------+---------+-------------------------------+
    | id        | integer | vlan id.                      |
    +-----------+---------+-------------------------------+
    | pcp       | integer | vlan pcp.                     |
    +-----------+---------+-------------------------------+

classifier table:

.. _table_spp_ctl_spp_vf_res_cls:

.. table:: Vlan objects of getting spp_vf.

    +-----------+--------+------------------------------------------------------------+
    | Name      | Type   | Description                                                |
    |           |        |                                                            |
    +===========+========+============================================================+
    | type      | string | ``mac`` or ``vlan``.                                       |
    +-----------+--------+------------------------------------------------------------+
    | value     | string | mac_address for ``mac``, vlan_id/mac_address for ``vlan``. |
    +-----------+--------+------------------------------------------------------------+
    | port      | string | port id applied to classify.                               |
    +-----------+--------+------------------------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    {
      "client-id": 1,
      "ports": [
        "phy:0", "phy:1", "vhost:0", "vhost:1", "ring:0", "ring:1", "ring:2", "ring:3"
      ],
      "components": [
        {
          "core": 2,
          "name": "forward_0_tx",
          "type": "forward",
          "rx_port": [
            {
            "port": "ring:0",
            "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ],
          "tx_port": [
            {
              "port": "vhost:0",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ]
        },
        {
          "core": 3,
          "type": "unuse"
        },
        {
          "core": 4,
          "type": "unuse"
        },
        {
          "core": 5,
          "name": "forward_1_rx",
          "type": "forward",
          "rx_port": [
            {
            "port": "vhost:1",
            "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ],
          "tx_port": [
            {
              "port": "ring:3",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ]
        },
        {
          "core": 6,
          "name": "classifier",
          "type": "classifier_mac",
          "rx_port": [
            {
              "port": "phy:0",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ],
          "tx_port": [
            {
              "port": "ring:0",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            },
            {
              "port": "ring:2",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ]
        },
        {
          "core": 7,
          "name": "merger",
          "type": "merge",
          "rx_port": [
            {
              "port": "ring:1",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            },
            {
              "port": "ring:3",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ],
          "tx_port": [
            {
              "port": "phy:0",
              "vlan": { "operation": "none", "id": 0, "pcp": 0 }
            }
          ]
        },
      ],
      "classifier_table": [
        {
          "type": "mac",
          "value": "FA:16:3E:7D:CC:35",
          "port": "ring:0"
        }
      ]
    }

The component which type is ``unused`` is to indicate unused core.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};status


POST /v1/vfs/{client_id}/components
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_components:

.. table:: Request params of components of spp_vf.

    +-----------+---------+-------------+
    | Name      | Type    | Description |
    +===========+=========+=============+
    | client_id | integer | client id.  |
    +-----------+---------+-------------+


Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_components_res:

.. table:: Response params of components of spp_vf.

    +-----------+---------+----------------------------------------------------------------------+
    | Name      | Type    | Description                                                          |
    |           |         |                                                                      |
    +===========+=========+======================================================================+
    | name      | string  | component name. must be unique in the process.                       |
    +-----------+---------+----------------------------------------------------------------------+
    | core      | integer | core id.                                                             |
    +-----------+---------+----------------------------------------------------------------------+
    | type      | string  | component type. one of ``forward``, ``merge`` or ``classifier_mac``. |
    +-----------+---------+----------------------------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X POST -H 'application/json' \
    -d '{"name": "forwarder1", "core": 12, "type": "forward"}' \
    http://127.0.0.1:7777/v1/vfs/1/components

Response
^^^^^^^^

There is no body content for the response of a successful ``POST`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};component start {name} {core} {type}


DELETE /v1/vfs/{sec id}/components/{name}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Stop the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_del:

.. table:: Request params of deleting component of spp_vf.

    +-----------+---------+---------------------------------+
    | Name      | Type    | Description                     |
    |           |         |                                 |
    +===========+=========+=================================+
    | client_id | integer | client id.                      |
    +-----------+---------+---------------------------------+
    | name      | string  | component name.                 |
    +-----------+---------+---------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/vfs/1/components/forwarder1

Response
^^^^^^^^

There is no body content for the response of a successful ``POST`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};component stop {name}


PUT /v1/vfs/{client_id}/components/{name}/ports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add or Delete port to the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_comp_port:

.. table:: Request params for ports of component of spp_vf.

    +-----------+---------+---------------------------+
    | Name      | Type    | Description               |
    |           |         |                           |
    +===========+=========+===========================+
    | client_id | integer | client id.                |
    +-----------+---------+---------------------------+
    | name      | string  | component name.           |
    +-----------+---------+---------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_comp_port_body:

.. table:: Request body params for ports of component of spp_vf.

    +---------+---------+-----------------------------------------------------------------+
    | Name    | Type    | Description                                                     |
    |         |         |                                                                 |
    +=========+=========+=================================================================+
    | action  | string  | ``attach`` or ``detach``.                                       |
    +---------+---------+-----------------------------------------------------------------+
    | port    | string  | port id. port id is the form {interface_type}:{interface_id}.   |
    +---------+---------+-----------------------------------------------------------------+
    | dir     | string  | ``rx`` or ``tx``.                                               |
    +---------+---------+-----------------------------------------------------------------+
    | vlan    | object  | vlan operation which is applied to the port. it can be omitted. |
    +---------+---------+-----------------------------------------------------------------+

vlan object:

.. _table_spp_ctl_spp_vf_comp_port_body_vlan:

.. table:: Request body params for vlan ports of component of spp_vf.

    +-----------+---------+----------------------------------------------------------+
    | Name      | Type    | Description                                              |
    |           |         |                                                          |
    +===========+=========+==========================================================+
    | operation | string  | ``add``, ``del`` or ``none``.                            |
    +-----------+---------+----------------------------------------------------------+
    | id        | integer | vlan id. ignored when operation is ``del`` or ``none``.  |
    +-----------+---------+----------------------------------------------------------+
    | pcp       | integer | vlan pcp. ignored when operation is ``del`` or ``none``. |
    +-----------+---------+----------------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "attach", "port": "vhost:1", "dir": "rx", \
         "vlan": {"operation": "add", "id": 677, "pcp": 0}}' \
    http://127.0.0.1:7777/v1/vfs/1/components/forwarder1/ports

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "detach", "port": "vhost:0", "dir": "tx"} \
    http://127.0.0.1:7777/v1/vfs/1/components/forwarder1/ports

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

action is ``attach``

.. code-block:: none

    sec {client_id};port add {port} {dir} {name} [add_vlantag {id} {pcp} | del_vlantag]

action is ``detach``

.. code-block:: none

    sec {client_id};port del {port} {dir} {name}


PUT /v1/vfs/{sec id}/classifier_table
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Set or Unset classifier table.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_cls_table:

.. table:: Request params for classifier_table of spp_vf.

    +-----------+---------+---------------------------+
    | Name      | Type    | Description               |
    |           |         |                           |
    +===========+=========+===========================+
    | client_id | integer | client id.                |
    +-----------+---------+---------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_vf_cls_table_body:

.. table:: Request body params for classifier_table of spp_vf.

    +-------------+-----------------+----------------------------------------------------+
    | Name        | Type            | Description                                        |
    |             |                 |                                                    |
    +=============+=================+====================================================+
    | action      | string          | ``add`` or ``del``.                                |
    +-------------+-----------------+----------------------------------------------------+
    | type        | string          | ``mac`` or ``vlan``.                               |
    +-------------+-----------------+----------------------------------------------------+
    | vlan        | integer or null | vlan id for ``vlan``. null or omitted for ``mac``. |
    +-------------+-----------------+----------------------------------------------------+
    | mac_address | string          | mac address.                                       |
    +-------------+-----------------+----------------------------------------------------+
    | port        | string          | port id.                                           |
    +-------------+-----------------+----------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "add", "type": "mac", "mac_address": "FA:16:3E:7D:CC:35", \
       "port": "ring:0"}' \
    http://127.0.0.1:7777/v1/vfs/1/classifier_table

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "del", "type": "vlan", "vlan": 475, \
       "mac_address": "FA:16:3E:7D:CC:35", "port": "ring:0"}' \
    http://127.0.0.1:7777/v1/vfs/1/classifier_table

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

type is ``mac``

.. code-block:: none

    classifier_table {action} mac {mac_address} {port}

type is ``vlan``

.. code-block:: none

    classifier_table {action} vlan {vlan} {mac_address} {port}


API for spp_mirror
------------------

GET /v1/mirrors/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Get the information of the ``spp_mirror`` process.

* Normal response codes: 200
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_mirrors_get:

.. table:: Request parameter for getting spp_mirror.

    +-----------+---------+--------------------------+
    | Name      | Type    | Description              |
    |           |         |                          |
    +===========+=========+==========================+
    | client_id | integer | client id.               |
    +-----------+---------+--------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/mirrors/1

Response
^^^^^^^^

.. _table_spp_ctl_spp_mirror_res:

.. table:: Response params of getting spp_mirror.

    +------------------+---------+-----------------------------------------------+
    | Name             | Type    | Description                                   |
    |                  |         |                                               |
    +==================+=========+===============================================+
    | client-id        | integer | client id.                                    |
    +------------------+---------+-----------------------------------------------+
    | ports            | array   | an array of port ids used by the process.     |
    +------------------+---------+-----------------------------------------------+
    | components       | array   | an array of component objects in the process. |
    +------------------+---------+-----------------------------------------------+

component object:

.. _table_spp_ctl_spp_mirror_res_comp:

.. table:: Component objects of getting spp_mirror.

    +---------+---------+---------------------------------------------------------------------+
    | Name    | Type    | Description                                                         |
    |         |         |                                                                     |
    +=========+=========+=====================================================================+
    | core    | integer | core id running on the component                                    |
    +---------+---------+---------------------------------------------------------------------+
    | name    | string  | an array of port ids used by the process.                           |
    +---------+---------+---------------------------------------------------------------------+
    | type    | string  | an array of component objects in the process.                       |
    +---------+---------+---------------------------------------------------------------------+
    | rx_port | array   | an array of port objects connected to the rx side of the component. |
    +---------+---------+---------------------------------------------------------------------+
    | tx_port | array   | an array of port objects connected to the tx side of the component. |
    +---------+---------+---------------------------------------------------------------------+

port object:

.. _table_spp_ctl_spp_mirror_res_port:

.. table:: Port objects of getting spp_vf.

    +---------+---------+---------------------------------------------------------------+
    | Name    | Type    | Description                                                   |
    |         |         |                                                               |
    +=========+=========+===============================================================+
    | port    | string  | port id. port id is the form {interface_type}:{interface_id}. |
    +---------+---------+---------------------------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    {
      "client-id": 1,
      "ports": [
        "phy:0", "phy:1", "ring:0", "ring:1", "ring:2"
      ],
      "components": [
        {
          "core": 2,
          "name": "mirror_0",
          "type": "mirror",
          "rx_port": [
            {
            "port": "ring:0"
            }
          ],
          "tx_port": [
            {
              "port": "ring:1"
            },
            {
              "port": "ring:2"
            }
          ]
        },
        {
          "core": 3,
          "type": "unuse"
        }
      ]
    }

The component which type is ``unused`` is to indicate unused core.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};status


POST /v1/mirrors/{client_id}/components
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_mirror_components:

.. table:: Request params of components of spp_mirror.

    +-----------+---------+-------------+
    | Name      | Type    | Description |
    +===========+=========+=============+
    | client_id | integer | client id.  |
    +-----------+---------+-------------+


Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_mirror_components_res:

.. table:: Response params of components of spp_mirror.

    +-----------+---------+----------------------------------------------------------------------+
    | Name      | Type    | Description                                                          |
    |           |         |                                                                      |
    +===========+=========+======================================================================+
    | name      | string  | component name. must be unique in the process.                       |
    +-----------+---------+----------------------------------------------------------------------+
    | core      | integer | core id.                                                             |
    +-----------+---------+----------------------------------------------------------------------+
    | type      | string  | component type. only ``mirror`` is available.                        |
    +-----------+---------+----------------------------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X POST -H 'application/json' \
    -d '{"name": "mirror_1", "core": 12, "type": "mirror"}' \
    http://127.0.0.1:7777/v1/mirrors/1/components

Response
^^^^^^^^

There is no body content for the response of a successful ``POST`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};component start {name} {core} {type}


DELETE /v1/mirrors/{client_id}/components/{name}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Stop the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_mirror_del:

.. table:: Request params of deleting component of spp_mirror.

    +-----------+---------+---------------------------------+
    | Name      | Type    | Description                     |
    |           |         |                                 |
    +===========+=========+=================================+
    | client_id | integer | client id.                      |
    +-----------+---------+---------------------------------+
    | name      | string  | component name.                 |
    +-----------+---------+---------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/mirrors/1/components/mirror_1

Response
^^^^^^^^

There is no body content for the response of a successful ``POST`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    sec {client_id};component stop {name}


PUT /v1/mirrors/{client_id}/components/{name}/ports
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add or Delete port to the component.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_mirror_comp_port:

.. table:: Request params for ports of component of spp_mirror.

    +-----------+---------+---------------------------+
    | Name      | Type    | Description               |
    |           |         |                           |
    +===========+=========+===========================+
    | client_id | integer | client id.                |
    +-----------+---------+---------------------------+
    | name      | string  | component name.           |
    +-----------+---------+---------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_mirror_comp_port_body:

.. table:: Request body params for ports of component of spp_mirror.

    +---------+---------+-----------------------------------------------------------------+
    | Name    | Type    | Description                                                     |
    |         |         |                                                                 |
    +=========+=========+=================================================================+
    | action  | string  | ``attach`` or ``detach``.                                       |
    +---------+---------+-----------------------------------------------------------------+
    | port    | string  | port id. port id is the form {interface_type}:{interface_id}.   |
    +---------+---------+-----------------------------------------------------------------+
    | dir     | string  | ``rx`` or ``tx``.                                               |
    +---------+---------+-----------------------------------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "attach", "port": "ring:1", "dir": "rx"}' \
    http://127.0.0.1:7777/v1/mirrors/1/components/mirror_1/ports

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "detach", "port": "ring:0", "dir": "tx"} \
    http://127.0.0.1:7777/v1/mirrors/1/components/mirror_1/ports

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

action is ``attach``

.. code-block:: none

    sec {client_id};port add {port} {dir} {name}

action is ``detach``

.. code-block:: none

    sec {client_id};port del {port} {dir} {name}


API for spp_pcap
----------------

GET /v1/pcaps/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~~~

Get the information of the ``spp_pcap`` process.

* Normal response codes: 200
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_pcap_get:

.. table:: Request parameter for getting spp_pcap info.

    +-----------+---------+-------------------------------------+
    | Name      | Type    | Description                         |
    |           |         |                                     |
    +===========+=========+=====================================+
    | client_id | integer | client id.                          |
    +-----------+---------+-------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X GET -H 'application/json' \
    http://127.0.0.1:7777/v1/pcaps/1

Response
^^^^^^^^

.. _table_spp_ctl_spp_pcap_res:

.. table:: Response params of getting spp_pcap.

    +------------------+---------+-----------------------------------------------+
    | Name             | Type    | Description                                   |
    |                  |         |                                               |
    +==================+=========+===============================================+
    | client-id        | integer | client id.                                    |
    +------------------+---------+-----------------------------------------------+
    | status           | string  | status of the process. "running" or "idle".   |
    +------------------+---------+-----------------------------------------------+
    | core             | array   | an array of core objects in the process.      |
    +------------------+---------+-----------------------------------------------+

core object:

.. _table_spp_ctl_spp_pcap_res_core:

.. table:: Core objects of getting spp_pcap.

    +----------+---------+----------------------------------------------------------------------+
    | Name     | Type    | Description                                                          |
    |          |         |                                                                      |
    +==========+=========+======================================================================+
    | core     | integer | core id                                                              |
    +----------+---------+----------------------------------------------------------------------+
    | role     | string  | role of the task running on the core. "receive" or "write".          |
    +----------+---------+----------------------------------------------------------------------+
    | rx_port  | array   | an array of port object for caputure. This member exists if role is  |
    |          |         | "recieve". Note that there is only a port object in the array.       |
    +----------+---------+----------------------------------------------------------------------+
    | filename | string  | a path name of output file. This member exists if role is "write".   |
    +----------+---------+----------------------------------------------------------------------+

Note that there is only a port object in the array

port object:

.. _table_spp_ctl_spp_pcap_res_port:

.. table:: Port objects of getting spp_pcap.

    +---------+---------+---------------------------------------------------------------+
    | Name    | Type    | Description                                                   |
    |         |         |                                                               |
    +=========+=========+===============================================================+
    | port    | string  | port id. port id is the form {interface_type}:{interface_id}. |
    +---------+---------+---------------------------------------------------------------+

Response example
^^^^^^^^^^^^^^^^

.. code-block:: json

    {
      "client-id": 1,
      "status": "running",
      "core": [
        {
          "core": 2,
          "role": "receive",
          "rx_port": [
            {
            "port": "phy:0"
            }
          ]
        },
        {
          "core": 3,
          "role": "write",
          "filename": "/tmp/spp_pcap.20181108110600.ring0.1.2.pcap"
        }
      ]
    }

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    pcap {client_id}; status

PUT /v1/pcaps/{client_id}/capture
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start or Stop capturing.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_pcap_capture:

.. table:: Request params of capture of spp_pcap.

    +-----------+---------+---------------------------------+
    | Name      | Type    | Description                     |
    |           |         |                                 |
    +===========+=========+=================================+
    | client_id | integer | client id.                      |
    +-----------+---------+---------------------------------+

Request(body)
^^^^^^^^^^^^^

.. _table_spp_ctl_spp_pcap_capture_body:

.. table:: Request body params of capture of spp_pcap.

    +--------+--------+-------------------------------------+
    | Name   | Type   | Description                         |
    |        |        |                                     |
    +========+========+=====================================+
    | action | string | ``start`` or ``stop``.              |
    +--------+--------+-------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X PUT -H 'application/json' \
    -d '{"action": "start"}' \
    http://127.0.0.1:7777/v1/pcaps/1/capture

Response
^^^^^^^^

There is no body content for the response of a successful ``PUT`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

action is ``start``

.. code-block:: none

    pcap {client_id}; start

action is ``stop``

.. code-block:: none

    pcap {client_id}; stop


DELETE /v1/pcaps/{client_id}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Terminate ``spp_pcap`` process.

* Normal response codes: 204
* Error response codes: 400, 404

Request(path)
^^^^^^^^^^^^^

.. _table_spp_ctl_pcap_delete:

.. table:: Request parameter for terminating spp_pcap.

    +-----------+---------+-------------------------------------+
    | Name      | Type    | Description                         |
    |           |         |                                     |
    +===========+=========+=====================================+
    | client_id | integer | client id.                          |
    +-----------+---------+-------------------------------------+

Request example
^^^^^^^^^^^^^^^

.. code-block:: console

    curl -X DELETE -H 'application/json' \
    http://127.0.0.1:7777/v1/pcaps/1

Response example
^^^^^^^^^^^^^^^^

There is no body content for the response of a successful ``DELETE`` request.

Equivalent CLI command
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: none

    pcap {client_id}; exit
