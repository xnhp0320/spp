#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Nippon Telegraph and Telephone Corporation

import requests


class SppCtlClient(object):

    rest_common_error_codes = [400, 404, 500]

    def __init__(self, ip_addr='localhost', port=7777, api_ver='v1'):
        self.base_url = 'http://%s:%d/%s' % (ip_addr, port, api_ver)
        self.ip_addr = ip_addr
        self.port = port

    def request_handler(func):
        """Request handler for spp-ctl.

        Decorator for handling a http request of 'requests' library.
        It receives one of the methods 'get', 'put', 'post' or 'delete'
        as 'func' argment.
        """

        def wrapper(self, *args, **kwargs):
            try:
                res = func(self, *args, **kwargs)

                # TODO(yasufum) revise print message to more appropriate
                # for spp.py.
                if res.status_code == 400:
                    print('Syntax or lexical error, or SPP returns ' +
                          'error for the request.')
                elif res.status_code == 404:
                    print('URL is not supported, or no SPP process ' +
                          'of client-id in a URL.')
                elif res.status_code == 500:
                    print('System error occured in spp-ctl.')

                return res
            except requests.exceptions.ConnectionError:
                print('Error: Failed to connect to spp-ctl.')
                return None
        return wrapper

    @request_handler
    def get(self, req):
        url = '%s/%s' % (self.base_url, req)
        return requests.get(url)

    @request_handler
    def put(self, req, params):
        url = '%s/%s' % (self.base_url, req)
        return requests.put(url, json=params)

    @request_handler
    def post(self, req, params):
        url = '%s/%s' % (self.base_url, req)
        return requests.post(url, json=params)

    @request_handler
    def delete(self, req):
        url = '%s/%s' % (self.base_url, req)
        return requests.delete(url)

    def is_server_running(self):
        if self.get('processes') is None:
            return False
        else:
            return True
