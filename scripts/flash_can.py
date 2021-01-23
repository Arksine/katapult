#!/usr/bin/env python2
# Script to upload software via Can Bootloader
#
# Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import sys
import os
import time
import argparse
import hashlib
import traceback
import serial

def output_line(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()

def output(msg):
    sys.stdout.write(msg)
    sys.stdout.flush()


SERIAL_BAUD = 250000
CMD_HEADER = b"cbtl"
CMD_TRAILER = b"\x03"
BOOTLOADER_CMDS = {
    'CONNECT': b"\x01",
    'SEND_BLOCK': b"\x02",
    'SEND_EOF': b"\x03",
    'REQUEST_BLOCK': b"\x04",
    'COMPLETE': b"\x05"
}
ACK_CMD = bytearray(CMD_HEADER + b"\x10")
ACK_BLOCK_RECD = bytearray(CMD_HEADER + b"\x11")
NACK = bytearray(CMD_HEADER + b"\x20")

class FlashCanError(Exception):
    pass

class FlashCan:
    def __init__(self, dev_name, fw_file):
        if not os.path.exists(dev_name):
            raise FlashCanError("Cannot find device '%s'" % (dev_name))
        if not os.path.isfile(fw_file):
            raise FlashCanError("Invalid firmware path '%s'" % (fw_file))
        # TODO:  We need to enter the bootloader first
        self.ser = serial.Serial(dev_name, SERIAL_BAUD, timeout=.1)
        self.fw_name = fw_file
        self.fw_sha = hashlib.sha1()
        self.file_size = 0
        self.block_size = 64
        self.block_count = 0

    def connect_btl(self):
        output_line("Attempting to connect to bootloader")
        self.block_size = self.send_command('CONNECT')
        if self.block_size not in [64, 128, 256, 512]:
            raise FlashCanError("Invalid Block Size: %d" % (self.block_size,))
        output_line("Connected, Block Size: %d bytes" % (self.block_size,))

    def serial_read(self, count):
        ret = b""
        tries = 10
        while tries:
            data = self.ser.read(count)
            count -= len(data)
            ret += data
            if not count:
                break
            tries -= 1
        return ret

    def send_command(self, cmdname, ack=ACK_CMD, arg=0, tries=5):
        cmd = BOOTLOADER_CMDS[cmdname]
        out_cmd = bytearray(CMD_HEADER + cmd)
        out_cmd.append((arg >> 8) & 0xFF)
        out_cmd.append(arg & 0xFF)
        out_cmd.append(CMD_TRAILER)
        while tries:
            try:
                self.ser.write(out_cmd)
                data = bytearray(self.serial_read(8))
            except Exception:
                traceback.print_exc()
            else:
                if len(data) == 8 and data.startswith(ack) and \
                        data.endswith(CMD_TRAILER):
                    return (data[5] << 8) | data[6]
            tries -= 1
            time.sleep(.1)
        raise FlashCanError("Error sending command [%s] to Can Device"
                            % (cmdname))

    def send_file(self):
        last_percent = 0
        output_line("Flashing '%s'..." % (self.fw_name))
        output("\n[")
        with open(self.fw_name, 'rb') as f:
            f.seek(0, os.SEEK_END)
            self.file_size = f.tell()
            f.seek(0)
            while True:
                buf = f.read(self.block_size)
                if not buf:
                    break
                self.send_command('SEND_BLOCK', arg=self.block_count)
                if len(buf) < self.block_size:
                    buf += b"\xFF" * (self.block_size - len(buf))
                self.fw_sha.update(buf)
                self.ser.write(buf)
                ack = self.serial_read(8)
                expect = bytearray(ACK_BLOCK_RECD)
                expect.append((self.block_count >> 8) & 0xFF)
                expect.append(self.block_count & 0xFF)
                expect.append(CMD_TRAILER)
                if ack != expect:
                    output_line("\nExpected resp: %s, Recd: %s" % (expect, ack))
                    raise FlashCanError("Did not receive ACK for sent block %d"
                                        % (self.block_count))
                self.block_count += 1
                uploaded = self.block_count * self.block_size
                pct = int(uploaded / float(self.file_size) * 100 + .5)
                if pct >= last_percent + 2:
                    last_percent += 2.
                    output("#")
            page_count = self.send_command('SEND_EOF')
            output_line("]\n\nWrite complete: %d pages" % (page_count))

    def verify_file(self):
        last_percent = 0
        output_line("Verifying (block count = %d)..." % (self.block_count,))
        output("\n[")
        ver_sha = hashlib.sha1()
        for i in range(self.block_count):
            tries = 3
            while tries:
                resp = self.send_command("REQUEST_BLOCK", arg=i)
                if resp == i:
                    # command should ack with the requested block as
                    # parameter
                    buf = self.serial_read(self.block_size)
                    if len(buf) == self.block_size:
                        break
                tries -= 1
                time.sleep(.1)
            else:
                output_line("Error")
                raise FlashCanError("Block Request Error, block: %d" % (i,))
            ver_sha.update(buf)
            pct = int(i * self.block_size / float(self.file_size) * 100 + .5)
            if pct >= last_percent + 2:
                last_percent += 2
                output("#")
        ver_hex = ver_sha.hexdigest().upper()
        fw_hex = self.fw_sha.hexdigest().upper()
        if ver_hex != fw_hex:
            raise FlashCanError("Checksum mismatch: Expected %s, Received %s"
                                % (fw_hex, ver_hex))
        output_line("]\n\nVerification Complete: SHA = %s" % (ver_hex))

    def finish(self):
        self.send_command("COMPLETE")

    def close(self):
        self.ser.close()

def main():
    parser = argparse.ArgumentParser(
        description="Can Bootloader Flash Utility")
    parser.add_argument('device', metavar="<device>",
                        help="Can device location")
    parser.add_argument('fwpath', metavar="<klipper.bin>",
                        help="Path to firmware file")
    args = parser.parse_args()
    fcan = None
    dev = args.device
    fname = os.path.abspath(os.path.expanduser(args.fwpath))
    try:
        fcan = FlashCan(dev, fname)
        time.sleep(1.)
        fcan.connect_btl()
        fcan.send_file()
        fcan.verify_file()
        fcan.finish()
    except Exception as e:
        output("Can Flash Error: ")
        output_line(str(e))
        sys.exit(-1)
    finally:
        if fcan is not None:
            fcan.close()
    output_line("CAN Flash Success")


if __name__ == '__main__':
    main()
