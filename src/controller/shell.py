# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2015-2016 Intel Corporation

from __future__ import absolute_import

import cmd
from .commands import bye
from .commands import pri
from .commands import nfv
from .commands import server
from .commands import topo
from .commands import vf
from .commands import mirror
from .commands import pcap
import os
import re
import readline
from .shell_lib import common
from . import spp_common
from .spp_common import logger
import subprocess


class Shell(cmd.Cmd, object):
    """SPP command prompt."""

    recorded_file = None
    hist_file = os.path.expanduser('~/.spp_history')
    HIST_EXCEPT = ['bye', 'exit', 'history', 'redo']

    intro = 'Welcome to the spp.   Type help or ? to list commands.\n'
    prompt = 'spp > '

    PLUGIN_DIR = 'plugins'
    topo_size = '60%'

    # setup history file
    if os.path.exists(hist_file):
        readline.read_history_file(hist_file)
    else:
        readline.write_history_file(hist_file)

    def __init__(self, spp_cli_objs, use_cache=False):
        cmd.Cmd.__init__(self)
        self.spp_ctl_server = server.SppCtlServer(spp_cli_objs)
        self.spp_ctl_cli = spp_cli_objs[0]
        self.use_cache = use_cache
        self.init_spp_procs()
        self.spp_topo = topo.SppTopo(self.spp_ctl_cli, {}, self.topo_size)

    def init_spp_procs(self):
        """Initialize delegators of SPP processes.

        Delegators accept a command and sent it to SPP proesses. This method
        is also called from `precmd()` method to update it to the latest
        status.
        """

        self.primary = pri.SppPrimary(self.spp_ctl_cli)

        self.secondaries = {}
        self.secondaries['nfv'] = {}
        for sec_id in self.get_sec_ids('nfv'):
            self.secondaries['nfv'][sec_id] = nfv.SppNfv(
                    self.spp_ctl_cli, sec_id)

        self.secondaries['vf'] = {}
        for sec_id in self.get_sec_ids('vf'):
            self.secondaries['vf'][sec_id] = vf.SppVf(
                    self.spp_ctl_cli, sec_id)

        self.secondaries['mirror'] = {}
        for sec_id in self.get_sec_ids('mirror'):
            self.secondaries['mirror'][sec_id] = mirror.SppMirror(
                    self.spp_ctl_cli, sec_id)

        self.spp_pcaps = {}
        for sec_id in self.get_sec_ids('pcap'):
            self.spp_pcaps[sec_id] = pcap.SppPcap(self.spp_ctl_cli, sec_id)

    def default(self, line):
        """Define defualt behaviour.

        If user input is commend styled, controller simply echo
        as a comment.
        """

        if common.is_comment_line(line):
            print("%s" % line.strip())

        else:
            super(Shell, self).default(line)

    def emptyline(self):
        """Do nothin for empty input.

        It override Cmd.emptyline() which runs previous input as default
        to do nothing.
        """
        pass

    def get_sec_ids(self, ptype):
        """Return a list of IDs of running secondary processes.

        'ptype' is 'nfv', 'vf' or 'mirror' or 'pcap'.
        """

        ids = []
        res = self.spp_ctl_cli.get('processes')
        if res is not None:
            if res.status_code == 200:
                for ent in res.json():
                    if ent['type'] == ptype:
                        ids.append(ent['client-id'])
        return ids

    def clean_hist_file(self):
        """Remove useless entries in history file."""

        entries = []

        try:
            for line in open(self.hist_file):
                line_s = line.strip()
                if not (line_s.split(' ')[0] in self.HIST_EXCEPT):
                    entries.append(line_s)
            f = open(self.hist_file, "w+")
            contents = '\n'.join(entries)
            contents += '\n'
            f.write(contents)
            f.close()
        except IOError:
            print('Error: Cannot open history file "%s"' % self.hist_file)

    def print_status(self):
        """Display information about connected clients."""

        print('- spp-ctl:')
        print('  - address: %s:%s' % (self.spp_ctl_cli.ip_addr,
                                      self.spp_ctl_cli.port))
        res = self.spp_ctl_cli.get('processes')
        if res is not None:
            if res.status_code == 200:
                proc_objs = res.json()
                pri_obj = None
                sec_obj = {}
                sec_obj['nfv'] = []
                sec_obj['vf'] = []
                sec_obj['mirror'] = []
                sec_obj['pcap'] = []

                for proc_obj in proc_objs:
                    if proc_obj['type'] == 'primary':
                        pri_obj = proc_obj
                    else:
                        sec_obj[proc_obj['type']].append(proc_obj)

                print('- primary:')
                if pri_obj is not None:
                    print('  - status: running')
                else:
                    print('  - status: not running')

                print('- secondary:')
                print('  - processes:')
                cnt = 1
                for pt in ['nfv', 'vf', 'mirror', 'pcap']:
                    for obj in sec_obj[pt]:
                        print('    %d: %s:%s' %
                              (cnt, obj['type'], obj['client-id']))
                        cnt += 1
            elif res.status_code in self.spp_ctl_cli.rest_common_error_codes:
                pass
            else:
                print('Error: unknown response.')

    def is_patched_ids_valid(self, id1, id2, delim=':'):
        """Check if port IDs are valid

        Supported format is port ID of integer or resource UID such as
        'phy:0' or 'ring:1'. Default delimiter ':' can be overwritten
        by giving 'delim' option.
        """

        if str.isdigit(id1) and str.isdigit(id2):
            return True
        else:
            ptn = r"\w+\%s\d+" % delim  # Match "phy:0" or "ring:1" or so
            if re.match(ptn, id1) and re.match(ptn, id2):
                pt1 = id1.split(delim)[0]
                pt2 = id2.split(delim)[0]
                if (pt1 in spp_common.PORT_TYPES) \
                        and (pt2 in spp_common.PORT_TYPES):
                            return True
        return False

    def check_sec_cmds(self, cmds):
        """Validate secondary commands before sending"""

        # TODO(yasufum) change to return True or False, or None
        # instead of 0 or 1

        level1 = ['status', 'exit', 'forward', 'stop']
        level2 = ['add', 'patch', 'del']
        patch_args = ['reset']
        add_del_args = ['ring', 'vhost', 'pcap', 'nullpmd']
        cmdlist = cmds.split(' ')
        valid = 0

        length = len(cmdlist)
        if length == 1:
            if cmdlist[0] in level1:
                valid = 1

        elif length == 2:
            if cmdlist[0] == 'patch':
                if cmdlist[1] in patch_args:
                    valid = 1

            elif cmdlist[0] == 'add' or cmdlist[0] == 'del':
                p_type, p_id = cmdlist[1].split(':')
                if p_type in add_del_args:
                    if str.isdigit(p_id):
                        valid = 1

        elif length == 3:
            if cmdlist[0] in level2:
                if cmdlist[0] == 'patch':
                    if self.is_patched_ids_valid(cmdlist[1], cmdlist[2]):
                        valid = 1

        return valid

    def clean_cmd(self, cmdstr):
        """remove unwanted spaces to avoid invalid command error"""

        tmparg = re.sub(r'\s+', " ", cmdstr)
        res = re.sub(r'\s?;\s?', ";", tmparg)
        return res

    def precmd(self, line):
        """Called before running a command

        It is called for checking a contents of command line.
        """

        if self.use_cache is False:
            self.init_spp_procs()

        if self.recorded_file:
            if not (
                    ('playback' in line) or
                    ('bye' in line) or
                    ('exit' in line)):
                self.recorded_file.write("%s\n" % line)
        return line

    def close(self):
        """Close record file"""

        if self.recorded_file:
            print("closing file")
            self.recorded_file.close()
            self.recorded_file = None

    def do_server(self, commands):
        """Switch SPP REST API server.

        spp > server  # or 'server list'
          1: 192.168.1.101:7777 *
          2: 192.168.1.102:7777

        spp > server 2
        Switch spp-ctl to "2: 192.168.1.102:7777".
        """

        self.spp_ctl_server.run(commands)
        self.spp_ctl_cli = self.spp_ctl_server.get_current_server()

    def complete_server(self, text, line, begidx, endidx):
        """Completion for server command."""

        res = self.spp_ctl_server.complete(text, line, begidx, endidx)
        return res

    def do_status(self, _):
        """Display status info of SPP processes

        spp > status
        """

        self.print_status()

    def do_pri(self, command):
        """Send a command to primary process.

        Spp primary takes sub commands.

        spp > pri;status
        spp > pri;clear
        """

        # Remove unwanted spaces and first char ';'
        command = self.clean_cmd(command)[1:]

        if logger is not None:
            logger.info("Receive pri command: '%s'" % command)

        self.primary.run(command)

    def complete_pri(self, text, line, begidx, endidx):
        """Completion for primary process commands."""

        return self.primary.complete(text, line, begidx, endidx)

    def do_nfv(self, cmd):
        """Send a command to spp_nfv specified with ID.

        Spp_nfv is specified with secondary ID and takes sub commands.

        spp > nfv 1; status
        spp > nfv 1; add ring:0
        spp > nfv 1; patch phy:0 ring:0

        You can refer all of sub commands by pressing TAB after
        'nfv 1;'.

        spp > nfv 1;  # press TAB
        add     del     exit    forward patch   status  stop
        """

        # remove unwanted spaces to avoid invalid command error
        tmparg = self.clean_cmd(cmd)
        cmds = tmparg.split(';')
        if len(cmds) < 2:
            print("Required an ID and ';' before the command.")
        elif str.isdigit(cmds[0]):
            self.secondaries['nfv'][int(cmds[0])].run(cmds[1])
        else:
            print('Invalid command: %s' % tmparg)

    def complete_nfv(self, text, line, begidx, endidx):
        """Completion for nfv command"""

        line = self.clean_cmd(line)

        tokens = line.split(';')
        if len(tokens) == 1:
            # Add SppNfv of sec_id if it is not exist
            sec_ids = self.get_sec_ids('nfv')
            for idx in sec_ids:
                if self.secondaries['nfv'][idx] is None:
                    self.secondaries['nfv'][idx] = nfv.SppNfv(
                            self.spp_ctl_cli, idx)

            if len(line.split()) == 1:
                res = [str(i)+';' for i in sec_ids]
            else:
                if not (';' in line):
                    res = [str(i)+';'
                           for i in sec_ids
                           if (str(i)+';').startswith(text)]
            return res
        elif len(tokens) == 2:
            first_tokens = tokens[0].split(' ')  # 'nfv 1' => ['nfv', '1']
            if len(first_tokens) == 2:
                idx = int(first_tokens[1])

                # Add SppVf of sec_id if it is not exist
                if self.secondaries['nfv'][idx] is None:
                    self.secondaries['nfv'][idx] = nfv.SppNfv(
                            self.spp_ctl_cli, idx)

                res = self.secondaries['nfv'][idx].complete(
                        self.get_sec_ids('nfv'), text, line, begidx, endidx)

                # logger.info(res)
                return res

    def do_vf(self, cmd):
        """Send a command to spp_vf.

        SPP VF is a secondary process for pseudo SR-IOV features. This
        command has four sub commands.
          * status
          * component
          * port
          * classifier_table

        Each of sub commands other than 'status' takes several parameters
        for detailed operations. Notice that 'start' for launching a worker
        is replaced with 'stop' for terminating. 'add' is also replaced with
        'del' for deleting.

        Examples:

        # (1) show status of worker threads and resources
        spp > vf 1; status

        # (2) launch or terminate a worker thread with arbitrary name
        #   NAME: arbitrary name used as identifier
        #   CORE_ID: one of unused cores referred from status
        #   ROLE: role of workers, 'forward', 'merge' or 'classifier_mac'
        spp > vf 1; component start NAME CORE_ID ROLE
        spp > vf 1; component stop NAME CORE_ID ROLE

        # (3) add or delete a port to worker of NAME
        #   RES_UID: resource UID such as 'ring:0' or 'vhost:1'
        #   DIR: 'rx' or 'tx'
        spp > vf 1; port add RES_UID DIR NAME
        spp > vf 1; port del RES_UID DIR NAME

        # (4) add or delete a port with vlan ID to worker of NAME
        #   VID: vlan ID
        #   PCP: priority code point defined in IEEE 802.1p
        spp > vf 1; port add RES_UID DIR NAME add_vlantag VID PCP
        spp > vf 1; port del RES_UID DIR NAME add_vlantag VID PCP

        # (5) add a port of deleting vlan tag
        spp > vf 1; port add RES_UID DIR NAME del_vlantag

        # (6) add or delete an entry of MAC address and resource to classify
        spp > vf 1; classifier_table add mac MAC_ADDR RES_UID
        spp > vf 1; classifier_table del mac MAC_ADDR RES_UID

        # (7) add or delete an entry of MAC address and resource with vlan ID
        spp > vf 1; classifier_table add vlan VID MAC_ADDR RES_UID
        spp > vf 1; classifier_table del vlan VID MAC_ADDR RES_UID
        """

        # remove unwanted spaces to avoid invalid command error
        tmparg = self.clean_cmd(cmd)
        cmds = tmparg.split(';')
        if len(cmds) < 2:
            print("Required an ID and ';' before the command.")
        elif str.isdigit(cmds[0]):
            self.secondaries['vf'][int(cmds[0])].run(cmds[1])
        else:
            print('Invalid command: %s' % tmparg)

    def complete_vf(self, text, line, begidx, endidx):
        """Completion for vf command"""

        line = self.clean_cmd(line)

        tokens = line.split(';')
        if len(tokens) == 1:
            # Add SppVf of sec_id if it is not exist
            sec_ids = self.get_sec_ids('vf')
            for idx in sec_ids:
                if self.secondaries['vf'][idx] is None:
                    self.secondaries['vf'][idx] = vf.SppVf(
                            self.spp_ctl_cli, idx)

            if len(line.split()) == 1:
                res = [str(i)+';' for i in sec_ids]
            else:
                if not (';' in line):
                    res = [str(i)+';'
                           for i in sec_ids
                           if (str(i)+';').startswith(text)]
            return res
        elif len(tokens) == 2:
            first_tokens = tokens[0].split(' ')  # 'vf 1' => ['vf', '1']
            if len(first_tokens) == 2:
                idx = int(first_tokens[1])

                # Add SppVf of sec_id if it is not exist
                if self.secondaries['vf'][idx] is None:
                    self.secondaries['vf'][idx] = vf.SppVf(
                            self.spp_ctl_cli, idx)

                return self.secondaries['vf'][idx].complete(
                        self.get_sec_ids('vf'), text, line, begidx, endidx)

    def do_mirror(self, cmd):
        """Send a command to spp_mirror.

        spp_mirror is a secondary process for duplicating incoming
        packets to be used as similar to TaaS in OpenStack. This
        command has four sub commands.
          * status
          * component
          * port

        Each of sub commands other than 'status' takes several parameters
        for detailed operations. Notice that 'start' for launching a worker
        is replaced with 'stop' for terminating. 'add' is also replaced with
        'del' for deleting.

        Examples:

        # (1) show status of worker threads and resources
        spp > mirror 1; status

        # (2) launch or terminate a worker thread with arbitrary name
        #   NAME: arbitrary name used as identifier
        #   CORE_ID: one of unused cores referred from status
        spp > mirror 1; component start NAME CORE_ID mirror
        spp > mirror 1; component stop NAME CORE_ID mirror

        # (3) add or delete a port to worker of NAME
        #   RES_UID: resource UID such as 'ring:0' or 'vhost:1'
        #   DIR: 'rx' or 'tx'
        spp > mirror 1; port add RES_UID DIR NAME
        spp > mirror 1; port del RES_UID DIR NAME
        """

        # remove unwanted spaces to avoid invalid command error
        tmparg = self.clean_cmd(cmd)
        cmds = tmparg.split(';')
        if len(cmds) < 2:
            print("Required an ID and ';' before the command.")
        elif str.isdigit(cmds[0]):
            self.secondaries['mirror'][int(cmds[0])].run(cmds[1])
        else:
            print('Invalid command: %s' % tmparg)

    def complete_mirror(self, text, line, begidx, endidx):
        """Completion for mirror command"""

        line = self.clean_cmd(line)

        tokens = line.split(';')
        if len(tokens) == 1:
            # Add SppMirror of sec_id if it is not exist
            sec_ids = self.get_sec_ids('mirror')
            for idx in sec_ids:
                if self.secondaries['mirror'][idx] is None:
                    self.secondaries['mirror'][idx] = mirror.SppMirror(
                            self.spp_ctl_cli, idx)

            if len(line.split()) == 1:
                res = [str(i)+';' for i in sec_ids]
            else:
                if not (';' in line):
                    res = [str(i)+';'
                           for i in sec_ids
                           if (str(i)+';').startswith(text)]
            return res
        elif len(tokens) == 2:
            # Split tokens like as from 'mirror 1' to ['mirror', '1']
            first_tokens = tokens[0].split(' ')
            if len(first_tokens) == 2:
                idx = int(first_tokens[1])

                # Add SppMirror of sec_id if it is not exist
                if self.secondaries['mirror'][idx] is None:
                    self.secondaries['mirror'][idx] = mirror.SppMirror(
                            self.spp_ctl_cli, idx)

                return self.secondaries['mirror'][idx].complete(
                        self.get_sec_ids('mirror'), text, line, begidx, endidx)

    def do_pcap(self, cmd):
        """Send a command to spp_pcap.

        spp_pcap is a secondary process for duplicating incoming
        packets to be used as similar to TaaS in OpenStack. This
        command has four sub commands.
          * status
          * start
          * stop
          * exit

        Each of sub commands other than 'status' takes several parameters
        for detailed operations. Notice that 'start' for launching a worker
        is replaced with 'stop' for terminating. 'exit' for spp_pcap
        terminating.

        Examples:

        # (1) show status of worker threads and resources
        spp > pcap 1; status

        # (2) launch capture thread
        spp > pcap 1; start

        # (3) terminate capture thread
        spp > pcap 1; stop

        # (4) terminate spp_pcap secondaryd
        spp > pcap 1; exit
        """

        # remove unwanted spaces to avoid invalid command error
        tmparg = self.clean_cmd(cmd)
        cmds = tmparg.split(';')
        if len(cmds) < 2:
            print("Required an ID and ';' before the command.")
        elif str.isdigit(cmds[0]):
            self.spp_pcaps[int(cmds[0])].run(cmds[1])
        else:
            print('Invalid command: %s' % tmparg)

    def complete_pcap(self, text, line, begidx, endidx):
        """Completion for pcap command"""

        line = self.clean_cmd(line)

        tokens = line.split(';')
        if len(tokens) == 1:
            # Add SppPcap of sec_id if it is not exist
            sec_ids = self.get_sec_ids('pcap')
            for idx in sec_ids:
                if self.spp_pcaps[idx] is None:
                    self.spp_pcaps[idx] = pcap.SppPcap(self.spp_ctl_cli, idx)

            if len(line.split()) == 1:
                res = [str(i)+';' for i in sec_ids]
            else:
                if not (';' in line):
                    res = [str(i)+';'
                           for i in sec_ids
                           if (str(i)+';').startswith(text)]
            return res
        elif len(tokens) == 2:
            # Split tokens like as from 'pcap 1' to ['pcap', '1']
            first_tokens = tokens[0].split(' ')
            if len(first_tokens) == 2:
                idx = int(first_tokens[1])

                # Add SppPcap of sec_id if it is not exist
                if self.spp_pcaps[idx] is None:
                    self.spp_pcaps[idx] = pcap.SppPcap(self.spp_ctl_cli, idx)

                return self.spp_pcaps[idx].complete(
                        self.get_sec_ids('pcap'), text, line, begidx, endidx)

    def do_record(self, fname):
        """Save commands as a recipe file.

        Save all of commands to a specified file as a recipe. This file
        is reloaded with 'playback' command later. You can also edit
        the recipe by hand to customize.

        spp > record path/to/recipe_file
        """

        if fname == '':
            print("Record file is required!")
        else:
            self.recorded_file = open(fname, 'w')

    def complete_record(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_playback(self, fname):
        """Setup a network configuration from recipe file.

        Recipe is a file describing a series of SPP command to setup
        a network configuration.

        spp > playback path/to/recipe_file
        """

        if fname == '':
            print("Record file is required!")
        else:
            self.close()
            try:
                with open(fname) as recorded_file:
                    lines = []
                    for line in recorded_file:
                        if not common.is_comment_line(line):
                            lines.append("# %s" % line)
                        lines.append(line)
                    self.cmdqueue.extend(lines)
            except IOError:
                message = "Error: File does not exist."
                print(message)

    def complete_playback(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_pwd(self, args):
        """Show corrent directory.

        It behaves as UNIX's pwd command.

        spp > pwd
        """

        print(os.getcwd())

    def do_ls(self, args):
        """Show a list of specified directory.

        It behaves as UNIX's ls command.

        spp > ls path/to/dir
        """

        if args == '' or os.path.isdir(args):
            c = 'ls -F %s' % args
            subprocess.call(c, shell=True)
        else:
            print("No such a directory.")

    def complete_ls(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_cd(self, args):
        """Change current directory.

        spp > cd path/to/dir
        """

        if os.path.isdir(args):
            os.chdir(args)
            print(os.getcwd())
        else:
            print("No such a directory.")

    def complete_cd(self, text, line, begidx, endidx):
        return common.compl_common(text, line, 'directory')

    def do_mkdir(self, args):
        """Create a new directory.

        It behaves as 'mkdir -p' which means that you can create sub
        directories at once.

        spp > mkdir path/to/dir
        """

        c = 'mkdir -p %s' % args
        subprocess.call(c, shell=True)

    def complete_mkdir(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_bye(self, args):
        """Terminate SPP processes and controller.

        There are three usages for terminating processes.
        It terminates logging if you activated recording.

        (1) Terminate secondary processes
        spp > bye sec

        (2) Terminate primary and secondary processes
        spp > bye all

        (3) Terminate SPP controller (not for primary and secondary)
        spp > bye
        """

        cmds = args.split(' ')
        if cmds[0] == '':  # terminate SPP CLI itself
            self.do_exit('')
            return True
        else:  # terminate other SPP processes
            spp_bye = bye.SppBye()
            spp_bye.run(args, self.primary, self.secondaries)

    def complete_bye(self, text, line, begidx, endidx):
        """Completion for bye commands"""

        spp_bye = bye.SppBye()
        return spp_bye.complete(text, line, begidx, endidx)

    def do_cat(self, arg):
        """View contents of a file.

        spp > cat file
        """
        if os.path.isfile(arg):
            c = 'cat %s' % arg
            subprocess.call(c, shell=True)
        else:
            print("No such a directory.")

    def do_redo(self, args):
        """Execute command of index of history.

        spp > redo 5  # exec 5th command in the history
        """

        idx = int(args)
        cmdline = None
        cnt = 1
        try:
            for line in open(self.hist_file):
                if cnt == idx:
                    cmdline = line.strip()
                    break
                cnt += 1

            if cmdline.find('pri;') > -1:
                cmdline = cmdline.replace(';', ' ;')
                print(cmdline)
            cmd_ary = cmdline.split(' ')
            cmd = cmd_ary.pop(0)
            cmd_options = ' '.join(cmd_ary)
            eval('self.do_%s(cmd_options)' % cmd)
        except IOError:
            print('Error: Cannot open history file "%s"' % self.hist_file)

    def do_history(self, arg):
        """Show command history.

        spp > history
          1  ls
          2  cat file.txt
          ...

        Command history is recorded in a file named '.spp_history'.
        It does not add some command which are no meaning for history.
        'bye', 'exit', 'history', 'redo'
        """

        # flush all of history to the hist_file.
        readline.write_history_file(self.hist_file)

        # remove commands defined in `self.HIST_EXCEPT`
        self.clean_hist_file()

        try:
            f = open(self.hist_file)

            # setup output format
            nof_lines = len(f.readlines())
            f.seek(0)
            lines_digit = len(str(nof_lines))
            hist_format = '  %' + str(lines_digit) + 'd  %s'

            cnt = 1
            for line in f:
                line_s = line.strip()
                print(hist_format % (cnt, line_s))
                cnt += 1
            f.close()
        except IOError:
            print('Error: Cannot open history file "%s"' % self.hist_file)

    def complete_cat(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_less(self, arg):
        """View contents of a file.

        spp > less file
        """
        if os.path.isfile(arg):
            c = 'less %s' % arg
            subprocess.call(c, shell=True)
        else:
            print("No such a directory.")

    def complete_less(self, text, line, begidx, endidx):
        return common.compl_common(text, line)

    def do_exit(self, args):
        """Terminate SPP controller process.

        It is an alias of bye command to terminate controller.

        spp > exit
        Thank you for using Soft Patch Panel
        """

        self.close()
        print('Thank you for using Soft Patch Panel')
        return True

    def do_inspect(self, args):
        """Print attributes of Shell for debugging.

        This command is intended to be used by developers to show the
        inside of the object of Shell class.

        spp > inspect
        {'cmdqueue': [],
         'completekey': 'tab',
         'completion_matches': ['inspect'],
         'lastcmd': 'inspect',
         'old_completer': None,
         'stdin': <open file '<stdin>', mode 'r' at 0x7fe96bddf0c0>,
         'stdout': <open file '<stdout>', mode 'w' at 0x7fe96bddf150>}

        """

        from pprint import pprint
        if args == '':
            pprint(vars(self))

    def terms_topo_subgraph(self):
        """Define terms of topo_subgraph command."""

        return ['add', 'del']

    def do_topo_subgraph(self, args):
        """Edit subgarph for topo command.

        Subgraph is a group of object defined in dot language. For topo
        command, it is used for grouping resources of each of VM or
        container to topology be more understandable.

        (1) Add subgraph labeled 'vm1'.
        spp > topo_subgraph add vm1 vhost:1;vhost:2

        (2) Delete subgraph 'vm1'.
        spp > topo_subgraph del vm1

        (3) Show subgraphs by running topo_subgraph without args.
        spp > topo_subgraph
        label: vm1	subgraph: "vhost:1;vhost:2"
        """

        # logger.info("Topo initialized with sec IDs %s" % sec_ids)

        # delimiter of node in dot file
        delim_node = '_'

        args_cleaned = re.sub(r"\s+", ' ', args).strip()
        # Show subgraphs if given no argments
        if (args_cleaned == ''):
            if len(self.spp_topo.subgraphs) == 0:
                print("No subgraph.")
            else:
                for label, subg in self.spp_topo.subgraphs.items():
                    print('label: %s\tsubgraph: "%s"' % (label, subg))
        else:  # add or del
            tokens = args_cleaned.split(' ')
            # Add subgraph
            if tokens[0] == 'add':
                if len(tokens) == 3:
                    label = tokens[1]
                    subg = tokens[2]
                    if ',' in subg:
                        subg = re.sub(r'%s' % delim_node, ':', subg)
                        subg = re.sub(r",", ";", subg)

                    # TODO(yasufum) add validation for subgraph
                    self.spp_topo.subgraphs[label] = subg
                    print("Add subgraph '%s'" % label)
                else:
                    print("Invalid syntax '%s'!" % args_cleaned)
            # Delete subgraph
            elif ((tokens[0] == 'del') or
                    (tokens[0] == 'delete') or
                    (tokens[0] == 'remove')):
                del(self.spp_topo.subgraphs[tokens[1]])
                print("Delete subgraph '%s'" % tokens[1])

            else:
                print("Ivalid subcommand '%s'!" % tokens[0])

    def complete_topo_subgraph(self, text, line, begidx, endidx):
        terms = self.terms_topo_subgraph()

        tokens = re.sub(r"\s+", ' ', line).strip().split(' ')
        if text == '':
            if len(tokens) == 1:
                return terms
            elif len(tokens) == 2 and tokens[1] == 'del':
                return self.spp_topo.subgraphs.keys()
        elif text != '':
            completions = []
            if len(tokens) == 3 and tokens[1] == 'del':
                for t in self.spp_topo.subgraphs.keys():
                    if t.startswith(tokens[2]):
                        completions.append(t)
            elif len(tokens) == 2:
                for t in terms:
                    if t.startswith(text):
                        completions.append(t)
            return completions
        else:
            pass

    def do_topo_resize(self, args):
        """Change the size of the image of topo command.

        You can specify the size by percentage or ratio.

        spp > topo resize 60%  # percentage
        spp > topo resize 0.6  # ratio

        """

        self.spp_topo.resize_graph(args)

    def do_topo(self, args):
        """Output network topology.

        Support four types of output.
        * terminal (but very few terminals supporting to display images)
        * browser (websocket server is required)
        * image file (jpg, png, bmp)
        * text (dot, js or json, yml or yaml)

        spp > topo term  # terminal
        spp > topo http  # browser
        spp > topo network_conf.jpg  # image
        spp > topo network_conf.dot  # text
        spp > topo network_conf.js# text
        """

        self.spp_topo.run(args, self.get_sec_ids('nfv'))

    def complete_topo(self, text, line, begidx, endidx):

        return self.spp_topo.complete(text, line, begidx, endidx)

    def do_load_cmd(self, args):
        """Load command plugin.

        Path of plugin file is 'spp/src/controller/plugins'.

        spp > load_cmd hello
        """

        args = re.sub(',', ' ', args)
        args = re.sub(r'\s+', ' ', args)
        list_args = args.split(' ')

        libdir = self.PLUGIN_DIR
        mod_name = list_args[0]
        method_name = 'do_%s' % mod_name
        exec('from .%s import %s' % (libdir, mod_name))
        do_cmd = '%s.%s' % (mod_name, method_name)
        exec('Shell.%s = %s' % (method_name, do_cmd))

        print("Module '%s' loaded." % mod_name)

    def complete_load_cmd(self, text, line, begidx, endidx):
        """Complete command plugins

        Search under PLUGIN_DIR with compl_common() method.
        This method is intended to be used for searching current
        directory, but not in this case. If text is not '',
        compl_common() does not work correctly and do filtering
        for the result by self.
        """

        curdir = os.path.dirname(__file__)
        res = common.compl_common(
            '', '%s/%s' % (curdir, self.PLUGIN_DIR), 'py')

        completions = []
        for t in res:
            if text == '':
                if t[:2] != '__':
                    completions.append(t[:-3])
            else:
                if t[:len(text)] == text:
                    completions.append(t[:-3])
        return completions
