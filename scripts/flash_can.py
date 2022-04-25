#!/usr/bin/env python3
# Script to upload software via Can Bootloader
#
# Copyright (C) 2022 Eric Callahan <arksine.code@gmail.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
from __future__ import annotations
import sys
import os
import asyncio
import socket
import struct
import logging
import errno
import argparse
import hashlib
import pathlib
from typing import Dict, List, Optional, Union

def output_line(msg: str) -> None:
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()

def output(msg: str) -> None:
    sys.stdout.write(msg)
    sys.stdout.flush()

logging.basicConfig(level=logging.INFO)
CAN_FMT = "<IB3x8s"
CAN_READER_LIMIT = 1024 * 1024

# Canboot Defs
CMD_HEADER = b"cbtl"
CMD_TRAILER = 0x03
BOOTLOADER_CMDS = {
    'CONNECT': 0x11,
    'SEND_BLOCK': 0x12,
    'SEND_EOF': 0x13,
    'REQUEST_BLOCK': 0x14,
    'COMPLETE': 0x15
}

ACK_CMD = bytearray(CMD_HEADER + b"\xa0")
ACK_BLOCK_RECD = bytearray(CMD_HEADER + b"\xa1")
NACK = bytearray(CMD_HEADER + b"\xf0")

# Klipper Admin Defs (for jumping to bootloader)
KLIPPER_ADMIN_ID = 0x3f0
KLIPPER_REBOOT_CMD = 0x02

# CAN Admin Defs
CANBUS_ID_ADMIN = 0x3f0
CANBUS_ID_ADMIN_RESP = 0x3f1
CANBUS_CMD_QUERY_UNASSIGNED = 0x00
CANBUS_CMD_SET_NODEID = 0x11
CANBUS_CMD_CLEAR_NODE_ID = 0x12
CANBUS_RESP_NEED_NODEID = 0x20
CANBUS_NODE_OFFSET = 0x200

class FlashCanError(Exception):
    pass

class CanFlasher:
    def __init__(
        self,
        node: CanNode,
        fw_file: pathlib.Path
    ) -> None:
        self.node = node
        self.fw_name = fw_file
        self.fw_sha = hashlib.sha1()
        self.file_size = 0
        self.block_size = 64
        self.block_count = 0

    async def connect_btl(self):
        output_line("Attempting to connect to bootloader")
        self.block_size = await self.send_command('CONNECT')
        if self.block_size not in [64, 128, 256, 512]:
            raise FlashCanError("Invalid Block Size: %d" % (self.block_size,))
        output_line("Connected, Block Size: %d bytes" % (self.block_size,))

    async def send_command(
        self,
        cmdname: str,
        ack: bytearray = ACK_CMD,
        arg: int = 0,
        tries: int = 5
    ) -> int:
        cmd = BOOTLOADER_CMDS[cmdname]
        out_cmd = bytearray(CMD_HEADER)
        out_cmd.append(cmd)
        out_cmd.append((arg >> 8) & 0xFF)
        out_cmd.append(arg & 0xFF)
        out_cmd.append(CMD_TRAILER)
        while tries:
            try:
                ret = await self.node.write_with_response(out_cmd, 8)
                data = bytearray(ret)
            except Exception:
                logging.exception("Can Read Error")
            else:
                if (
                    len(data) == 8 and
                    data[:5] == ack and
                    data[-1] == CMD_TRAILER
                ):
                    return (data[5] << 8) | data[6]
            tries -= 1
            await asyncio.sleep(.1)
        raise FlashCanError("Error sending command [%s] to Can Device"
                            % (cmdname))

    async def send_file(self):
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
                await self.send_command('SEND_BLOCK', arg=self.block_count)
                if len(buf) < self.block_size:
                    buf += b"\xFF" * (self.block_size - len(buf))
                self.fw_sha.update(buf)
                self.node.write(buf)
                ack = await self.node.readexactly(8)
                expect = bytearray(ACK_BLOCK_RECD)
                expect.append((self.block_count >> 8) & 0xFF)
                expect.append(self.block_count & 0xFF)
                expect.append(CMD_TRAILER)
                if ack != bytes(expect):
                    output_line("\nExpected resp: %s, Recd: %s" % (expect, ack))
                    raise FlashCanError("Did not receive ACK for sent block %d"
                                        % (self.block_count))
                self.block_count += 1
                uploaded = self.block_count * self.block_size
                pct = int(uploaded / float(self.file_size) * 100 + .5)
                if pct >= last_percent + 2:
                    last_percent += 2.
                    output("#")
            page_count = await self.send_command('SEND_EOF')
            output_line("]\n\nWrite complete: %d pages" % (page_count))

    async def verify_file(self):
        last_percent = 0
        output_line("Verifying (block count = %d)..." % (self.block_count,))
        output("\n[")
        ver_sha = hashlib.sha1()
        for i in range(self.block_count):
            tries = 3
            while tries:
                resp = await self.send_command("REQUEST_BLOCK", arg=i)
                if resp == i:
                    # command should ack with the requested block as
                    # parameter
                    try:
                        buf = await self.node.readexactly(
                            self.block_size, timeout=5.
                        )
                    except asyncio.TimeoutError:
                        if tries:
                            output_line("\nRead Timeout, Retrying...")
                    else:
                        if len(buf) == self.block_size:
                            break
                tries -= 1
                await asyncio.sleep(.1)
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

    async def finish(self):
        await self.send_command("COMPLETE")


class CanNode:
    def __init__(self, node_id: int, cansocket: CanSocket) -> None:
        self.node_id = node_id
        self._reader = asyncio.StreamReader(CAN_READER_LIMIT)
        self._cansocket = cansocket

    async def read(
        self, n: int = -1, timeout: Optional[float] = 2
    ) -> bytes:
        return await asyncio.wait_for(self._reader.read(n), timeout)

    async def readexactly(
        self, n: int, timeout: Optional[float] = 2
    ) -> bytes:
        return await asyncio.wait_for(self._reader.readexactly(n), timeout)

    def write(self, payload: Union[bytes, bytearray]) -> None:
        if isinstance(payload, bytearray):
            payload = bytes(payload)
        self._cansocket.send(self.node_id, payload)

    async def write_with_response(
        self,
        payload: Union[bytearray, bytes],
        resp_length: int,
        timeout: Optional[float] = 2.
    ) -> bytes:
        self.write(payload)
        return await self.readexactly(resp_length, timeout)

    def feed_data(self, data: bytes) -> None:
        self._reader.feed_data(data)

    def close(self) -> None:
        self._reader.feed_eof()

class CanSocket:
    def __init__(self, loop: asyncio.AbstractEventLoop):
        self._loop = loop
        self.cansock = socket.socket(socket.PF_CAN, socket.SOCK_RAW,
                                     socket.CAN_RAW)
        self.admin_node = CanNode(CANBUS_ID_ADMIN, self)
        self.nodes: Dict[int, CanNode] = {
            CANBUS_ID_ADMIN_RESP: self.admin_node
        }

        self.input_buffer = b""
        self.output_packets: List[bytes] = []
        self.input_busy = False
        self.output_busy = False
        self.closed = True

    def _handle_can_response(self) -> None:
        try:
            data = self.cansock.recv(4096)
        except socket.error as e:
            # If bad file descriptor allow connection to be
            # closed by the data check
            if e.errno == errno.EBADF:
                logging.exception("Can Socket Read Error, closing")
                data = b''
            else:
                return
        if not data:
            # socket closed
            self.close()
            return
        self.input_buffer += data
        if self.input_busy:
            return
        self.input_busy = True
        while len(self.input_buffer) >= 16:
            packet = self.input_buffer[:16]
            self._process_packet(packet)
            self.input_buffer = self.input_buffer[16:]
        self.input_busy = False

    def _process_packet(self, packet: bytes) -> None:
        can_id, length, data = struct.unpack(CAN_FMT, packet)
        can_id &= socket.CAN_EFF_MASK
        payload = data[:length]
        node = self.nodes.get(can_id)
        if node is not None:
            node.feed_data(payload)

    def send(self, can_id: int, payload: bytes = b"") -> None:
        if can_id > 0x7FF:
            can_id |= socket.CAN_EFF_FLAG
        if not payload:
            packet = struct.pack(CAN_FMT, can_id, 0, b"")
            self.output_packets.append(packet)
        else:
            while payload:
                length = min(len(payload), 8)
                pkt_data = payload[:length]
                payload = payload[length:]
                packet = struct.pack(
                    CAN_FMT, can_id, length, pkt_data)
                self.output_packets.append(packet)
        if self.output_busy:
            return
        self.output_busy = True
        asyncio.create_task(self._do_can_send())

    async def _do_can_send(self):
        while self.output_packets:
            packet = self.output_packets.pop(0)
            try:
                await self._loop.sock_sendall(self.cansock, packet)
            except socket.error:
                logging.info("Socket Write Error, closing")
                self.close()
                break
        self.output_busy = False

    def _jump_to_bootloader(self, uuid: int):
        # TODO: Send Klipper Admin command to jump to bootloader.
        # It will need to be implemented
        output_line("Sending bootloader jump command...")
        plist = [(uuid >> ((5 - i) * 8)) & 0xFF for i in range(6)]
        plist.insert(0, KLIPPER_REBOOT_CMD)
        self.send(KLIPPER_ADMIN_ID, bytes(plist))

    async def _query_uuids(self) -> List[int]:
        output_line("Checking for canboot nodes...")
        payload = bytes([CANBUS_CMD_QUERY_UNASSIGNED])
        self.admin_node.write(payload)
        curtime = self._loop.time()
        endtime = curtime + 2.
        self.uuids: List[int] = []
        while curtime < endtime:
            diff = endtime - curtime
            try:
                resp = await self.admin_node.readexactly(8, diff)
            except asyncio.TimeoutError:
                break
            finally:
                curtime = self._loop.time()
            if resp[0] != CANBUS_RESP_NEED_NODEID:
                continue
            app = "unknown"
            if resp[-1] == 1:
                app = "CanBoot"
            elif resp[-1] == 0:
                app = "Klipper"
            output_line(f"Detected UUID: {resp[1:].hex()}, Application: {app}")
            uuid = sum([v << ((5 - i) * 8) for i, v in enumerate(resp[1:7])])
            if uuid not in self.uuids and app == "CanBoot":
                self.uuids.append(uuid)
        return self.uuids

    def _reset_nodes(self) -> None:
        output_line("Resetting all bootloader node IDs...")
        payload = bytes([CANBUS_CMD_CLEAR_NODE_ID])
        self.admin_node.write(payload)

    def _set_node_id(self, uuid: int) -> CanNode:
        # Convert ID to a list
        plist = [(uuid >> ((5 - i) * 8)) & 0xFF for i in range(6)]
        plist.insert(0, CANBUS_CMD_SET_NODEID)
        node_id = len(self.nodes)
        plist.append(node_id)
        payload = bytes(plist)
        self.admin_node.write(payload)
        decoded_id = node_id * 2 + CANBUS_NODE_OFFSET
        node = CanNode(decoded_id, self)
        self.nodes[decoded_id + 1] = node
        return node

    async def run(self, intf: str, uuid: int, fw_path: pathlib.Path) -> None:
        if not fw_path.is_file():
            raise FlashCanError("Invalid firmware path '%s'" % (fw_path))
        try:
            self.cansock.bind((intf,))
        except Exception:
            raise FlashCanError("Unable to bind socket to can0")
        self.closed = False
        self.cansock.setblocking(False)
        self._loop.add_reader(
            self.cansock.fileno(), self._handle_can_response)
        self._jump_to_bootloader(uuid)
        await asyncio.sleep(.5)
        self._reset_nodes()
        await asyncio.sleep(.5)
        id_list = await self._query_uuids()
        if uuid not in id_list:
            raise FlashCanError(
                f"Unable to find node matching UUID: {uuid:06x}"
            )
        node = self._set_node_id(uuid)
        flasher = CanFlasher(node, fw_path)
        await asyncio.sleep(.5)
        await flasher.connect_btl()
        await flasher.send_file()
        await flasher.verify_file()
        await flasher.finish()

    async def run_query(self, intf: str):
        try:
            self.cansock.bind((intf,))
        except Exception:
            raise FlashCanError("Unable to bind socket to can0")
        self.closed = False
        self.cansock.setblocking(False)
        self._loop.add_reader(
            self.cansock.fileno(), self._handle_can_response)
        self._reset_nodes()
        await asyncio.sleep(.5)
        await self._query_uuids()

    def close(self):
        if self.closed:
            return
        self.closed = True
        for node in self.nodes.values():
            node.close()
        self._loop.remove_reader(self.cansock.fileno())
        self.cansock.close()

def main():
    parser = argparse.ArgumentParser(
        description="Can Bootloader Flash Utility")
    parser.add_argument(
        "-i", "--interface", default="can0", metavar='<can interface>',
        help="Can Interface"
    )
    parser.add_argument(
        "-f", "--firmware", metavar="<klipper.bin>",
        default="~/klipper/out/klipper.bin",
        help="Path to Klipper firmware file")
    parser.add_argument(
        "-u", "--uuid", metavar="<uuid>", default=None,
        help="Can device uuid"
    )
    parser.add_argument(
        "-q", "--query", action="store_true",
        help="Query Bootloader Device IDs"
    )

    args = parser.parse_args()
    intf = args.interface
    fpath = pathlib.Path(args.firmware).expanduser().resolve()
    loop = asyncio.get_event_loop()
    try:
        cansock = CanSocket(loop)
        if args.query:
            loop.run_until_complete(cansock.run_query(intf))
        else:
            if args.uuid is None:
                raise FlashCanError(
                    "The 'uuid' option must be specified to flash a device"
                )
            uuid = int(args.uuid, 16)
            loop.run_until_complete(cansock.run(intf, uuid, fpath))
    except Exception as e:
        logging.exception("Can Flash Error")
        sys.exit(-1)
    finally:
        if cansock is not None:
            cansock.close()
    if args.query:
        output_line("Query Complete")
    else:
        output_line("CAN Flash Success")


if __name__ == '__main__':
    main()
