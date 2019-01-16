# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Nippon Telegraph and Telephone Corporation

import eventlet
eventlet.monkey_patch()

import argparse
import errno
import logging
import socket
import subprocess

import spp_proc
import spp_webapi


LOG = logging.getLogger(__name__)


MSG_SIZE = 4096


class Controller(object):

    def __init__(self, host, pri_port, sec_port, api_port):
        self.web_server = spp_webapi.WebServer(self, host, api_port)
        self.procs = {}
        self.ip_addr = host
        self.init_connection(pri_port, sec_port)

    def start(self):
        self.web_server.start()

    def init_connection(self, pri_port, sec_port):
        self.pri_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.pri_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.pri_sock.bind((self.ip_addr, pri_port))
        self.pri_sock.listen(1)
        self.primary_listen_thread = eventlet.greenthread.spawn(
            self.accept_primary)

        self.sec_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sec_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sec_sock.bind((self.ip_addr, sec_port))
        self.sec_sock.listen(1)
        self.secondary_listen_thread = eventlet.greenthread.spawn(
            self.accept_secondary)

    def accept_primary(self):
        while True:
            conn, _ = self.pri_sock.accept()
            proc = self.procs.get(spp_proc.ID_PRIMARY)
            if proc is not None:
                LOG.warning("spp_primary reconnect !")
                with proc.sem:
                    try:
                        proc.conn.close()
                    except Exception:
                        pass
                    proc.conn = conn
                # NOTE: when spp_primary restart, all secondarys must be
                # restarted. this is out of controle of spp-ctl.
            else:
                LOG.info("primary connected.")
                self.procs[spp_proc.ID_PRIMARY] = spp_proc.PrimaryProc(conn)

    def accept_secondary(self):
        while True:
            conn, _ = self.sec_sock.accept()
            LOG.debug("sec accepted: get process id")
            proc = self._get_proc(conn)
            if proc is None:
                LOG.error("get process id failed")
                conn.close()
                continue
            old_proc = self.procs.get(proc.id)
            if old_proc:
                LOG.warning("%s(%d) reconnect !", old_proc.type, old_proc.id)
                if old_proc.type != proc.type:
                    LOG.warning("type changed ! new type: %s", proc.type)
                with old_proc.sem:
                    try:
                        old_proc.conn.close()
                    except Exception:
                        pass
            else:
                LOG.info("%s(%d) connected.", proc.type, proc.id)
            self.procs[proc.id] = proc

    @staticmethod
    def _continue_recv(conn):
        try:
            # must set non-blocking to recieve remining data not to happen
            # blocking here.
            # NOTE: usually MSG_DONTWAIT flag is used for this purpose but
            # this flag is not supported under eventlet.
            conn.setblocking(False)
            data = b""
            while True:
                try:
                    rcv_data = conn.recv(MSG_SIZE)
                    data += rcv_data
                    if len(rcv_data) < MSG_SIZE:
                        break
                except socket.error as e:
                    if e.args[0] == errno.EAGAIN:
                        # OK, no data remining. this happens when recieve data
                        # length is just multiple of MSG_SIZE.
                        break
                    raise e
            return data
        finally:
            conn.setblocking(True)

    @staticmethod
    def _send_command(conn, command):
        conn.sendall(command.encode())
        data = conn.recv(MSG_SIZE)
        if data and len(data) == MSG_SIZE:
            # could not receive data at once. recieve remining data.
            data += self._continue_recv(conn)
        if data:
            data = data.decode()
        return data

    def _get_proc(self, conn):
        # it is a bit ad hoc. send "_get_clinet_id" command and try to
        # decode reply for each proc type. if success, that is the type.
        data = self._send_command(conn, "_get_client_id")
        for proc in [spp_proc.VfProc, spp_proc.NfvProc, spp_proc.MirrorProc,
                     spp_proc.PcapProc]:
            sec_id = proc._decode_client_id(data)
            if sec_id is not None:
                return proc(sec_id, conn)

    def get_processes(self):
        procs = []
        for proc in self.procs.values():
            p = {"type": proc.type}
            if proc.id != spp_proc.ID_PRIMARY:
                p["client-id"] = proc.id
            procs.append(p)
        return procs

    def do_exit(self, proc_type, proc_id):
        removed_id = None  # remove proc info of ID from self.procs
        for proc in self.procs.values():
            if proc.type == proc_type and proc.id == proc_id:
                removed_id = proc.id
                break
        if removed_id is not None:
            del self.procs[removed_id]


def main():
    parser = argparse.ArgumentParser(description="SPP Controller")
    parser.add_argument("-b", '--bind-addr', type=str, default='localhost',
                        help="bind address, default=localhost")
    parser.add_argument("-p", dest='pri_port', type=int, default=5555,
                        action='store', help="primary port, default=5555")
    parser.add_argument("-s", dest='sec_port', type=int, default=6666,
                        action='store', help="secondary port, default=6666")
    parser.add_argument("-a", dest='api_port', type=int, default=7777,
                        action='store', help="web api port, default=7777")
    args = parser.parse_args()

    logging.basicConfig(level=logging.DEBUG)

    controller = Controller(args.bind_addr, args.pri_port, args.sec_port,
                            args.api_port)
    controller.start()
