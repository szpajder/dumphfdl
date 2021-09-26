#!/usr/bin/env python
#SPDX-License-Identifier: GPL-3.0-or-later
#
# Receive logs from multiple sources on ZMQ server socket and write them into a common, optionally rotated file.
#
# (c) Tomasz Lemiech <szpajder@gmail.com>
#
import argparse
import re
import signal
import sys
import time
import zmq

do_exit = False

def sighandler(signum, stack_frame):
    print(f"Got signal {signum}")
    do_exit = True
    raise KeyboardInterrupt

def setup_signals():
    signal.signal(signal.SIGINT, sighandler)
    signal.signal(signal.SIGTERM, sighandler)
    signal.signal(signal.SIGQUIT, sighandler)

def out_file_open(self):
    if self.rotate is None:
        filename = self.filename_prefix
    else:
        self.current_tm = time.localtime(time.time())
        if self.rotate == 'daily':
            fmt = '_%Y%m%d'
        elif self.rotate == 'hourly':
            fmt = '_%Y%m%d_%H'
        timesuffix = time.strftime(fmt, self.current_tm)
        filename = self.filename_prefix + timesuffix + self.extension
    self.fh = open(filename, mode='a', buffering=1)


def out_file_init(self):
    if self.output_file == '-':
        self.fh = sys.stdout
        self.rotate = None
        return True
    else:
        m = re.match("^(.+)(\.[^.]+)$", self.output_file)
        if m:
            self.filename_prefix = m.group(1)
            self.extension = m.group(2)
        else:
            self.filename_prefix = self.output_file
            self.extension = ''
        out_file_open(self)


def out_file_rotate(self):
    now = time.localtime(time.time())
    if (self.rotate == 'daily' and now.tm_mday != self.current_tm.tm_mday) or (self.rotate == 'hourly' and now.tm_hour != self.current_tm.tm_hour):
        self.fh.close()
        out_file_open(self)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Receive logs from multiple sources on ZMQ server socket and write them into a common, optionally rotated file')
    parser.add_argument('--listen', required=True, help='ZMQ endpoint to listen on (example: tcp://*:4000)')
    parser.add_argument('--output-file', default='-', help='Full path to the file where the output should be written (default: stdout)')
    parser.add_argument('--rotate', choices=['daily','hourly'], help='Rotate the output file on top of the day or hour, respectively (default: do not rotate)')
    self = parser.parse_args()

    try:
        out_file_init(self)
    except Exception as e:
        print(f"Could not open output file: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        socket.bind(self.listen)
        socket.setsockopt_string(zmq.SUBSCRIBE, '')
    except Exception as e:
        print(f"Could not create ZMQ listener: {e}", file=sys.stderr)
        sys.exit(1)

    setup_signals()

    while not do_exit:
        try:
            string = socket.recv_string()
        except KeyboardInterrupt:
            break
        try:
            if self.rotate is not None:
                out_file_rotate(self)
        except Exception as e:
            print(f"Could not rotate output file: {e}", file=sys.stderr)
            sys.exit(1)
        self.fh.write(string)

    print('Exiting', file=sys.stderr)

