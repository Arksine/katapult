#!/usr/bin/env python3
# Script to upload software via Katapult
#
# Copyright (C) 2022 Eric Callahan <arksine.code@gmail.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
from __future__ import annotations
import sys
import os
import termios
import fcntl
import zlib
import json
import asyncio
import socket
import struct
import logging
import errno
import argparse
import hashlib
import pathlib
import shutil
import shlex
import contextlib
from typing import Dict, List, Optional, Union, Any
HAS_SERIAL = True
try:
    from serial import Serial, SerialException
except ModuleNotFoundError:
    HAS_SERIAL = False
    SerialException = Exception

def output_line(msg: str) -> None:
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()

def output(msg: str) -> None:
    sys.stdout.write(msg)
    sys.stdout.flush()

# Standard crc16 ccitt, take from msgproto.py in Klipper
def crc16_ccitt(buf: Union[bytes, bytearray]) -> int:
    crc = 0xffff
    for data in buf:
        data ^= crc & 0xff
        data ^= (data & 0x0f) << 4
        crc = ((data << 8) | (crc >> 8)) ^ (data >> 4) ^ (data << 3)
    return crc & 0xFFFF


logging.basicConfig(level=logging.INFO)
CAN_FMT = "<IB3x8s"
CAN_READER_LIMIT = 1024 * 1024

# Katapult Defs
CMD_HEADER = b'\x01\x88'
CMD_TRAILER = b'\x99\x03'
BOOTLOADER_CMDS = {
    'CONNECT': 0x11,
    'SEND_BLOCK': 0x12,
    'SEND_EOF': 0x13,
    'REQUEST_BLOCK': 0x14,
    'COMPLETE': 0x15,
    'GET_CANBUS_ID': 0x16,
}

ACK_SUCCESS = 0xa0
NACK = 0xf1
ACK_ERROR = 0xf2
ACK_BUSY = 0xf3

# Klipper Admin Defs (for jumping to bootloader)
KLIPPER_ADMIN_ID = 0x3f0
KLIPPER_SET_NODE_CMD = 0x01
KLIPPER_REBOOT_CMD = 0x02

# CAN Admin Defs
CANBUS_ID_ADMIN = 0x3f0
CANBUS_ID_ADMIN_RESP = 0x3f1
CANBUS_CMD_QUERY_UNASSIGNED = 0x00
CANBUS_CMD_SET_NODEID = 0x11
CANBUS_CMD_CLEAR_NODE_ID = 0x12
CANBUS_RESP_NEED_NODEID = 0x20
CANBUS_NODEID_OFFSET = 128

# USB IDs
KATAPULT_USB_ID = "1d50:6177"
KLIPPER_USB_ID = "1d50:614e"
SERIAL_BL_REQ = b"~ \x1c Request Serial Bootloader!! ~"

class FlashError(Exception):
    pass

def get_usb_info(usb_path: pathlib.Path) -> Dict[str, Any]:
    usb_info: Dict[str, Any] = {}
    id_path = usb_path.joinpath("idVendor")
    prod_path = usb_path.joinpath("idProduct")
    mfr_path = usb_path.joinpath("manufacturer")
    prod_path = usb_path.joinpath("product")
    if id_path.is_file() and prod_path.is_file():
        vid = id_path.read_text().strip().lower()
        pid = prod_path.read_text().strip().lower()
        usb_info["usb_id"] = f"{vid}:{pid}"
    usb_info["manufacturer"] = "unknown"
    usb_info["product"] = "unknown"
    if mfr_path.is_file():
        usb_info["manufacturer"] = mfr_path.read_text().strip().lower()
    if prod_path.is_file():
        usb_info["product"] = prod_path.read_text().strip().lower()
    return usb_info

def get_usb_path(device: pathlib.Path) -> Optional[pathlib.Path]:
    device_path = device.resolve()
    if not device_path.exists():
        return None
    sys_dev_path = pathlib.Path("/sys/class/tty").joinpath(device_path.name)
    if not sys_dev_path.exists():
        return None
    sys_dev_path = sys_dev_path.resolve()
    for usb_path in sys_dev_path.parents:
        dnum_file = usb_path.joinpath("devnum")
        bnum_file = usb_path.joinpath("busnum")
        if dnum_file.is_file() and bnum_file.is_file():
            return usb_path
    return None

def get_stable_usb_symlink(device: pathlib.Path) -> pathlib.Path:
    device_path = device.resolve()
    ser_by_path_dir = pathlib.Path("/dev/serial/by-path")
    try:
        if ser_by_path_dir.exists():
            for item in ser_by_path_dir.iterdir():
                if item.samefile(device_path):
                    return item
    except OSError:
        pass
    return device_path

class CanFlasher:
    def __init__(
        self,
        node: CanNode,
        fw_file: pathlib.Path
    ) -> None:
        self.node = node
        self.firmware_path = fw_file
        self.fw_sha = hashlib.sha1()
        self.primed = False
        self.file_size = 0
        self.block_size = 64
        self.block_count = 0
        self.app_start_addr = 0
        self.klipper_dict: Optional[Dict[str, Any]] = None
        self._check_binary()

    def _check_binary(self) -> None:
        """
        Extract klipper.dict from binary
        """
        fw_name = self.firmware_path.name.lower()
        if fw_name != "klipper.bin":
            return
        bin_data = self.firmware_path.read_bytes()
        klipper_dict: Dict[str, Any] = {}
        for idx in range(len(bin_data)):
            try:
                uncmp_data = zlib.decompress(bin_data[idx:])
                klipper_dict = json.loads(uncmp_data)
            except (zlib.error, json.JSONDecodeError):
                continue
            if klipper_dict.get("app") == "Klipper":
                break
        if klipper_dict:
            self.klipper_dict = klipper_dict
            ver = klipper_dict.get("version", "")
            bin_mcu = self.klipper_dict.get("config", {}).get("MCU", "")
            output_line(
                f"Detected Klipper binary version {ver}, MCU: {bin_mcu}"
            )

    def _build_command(self, cmd: int, payload: bytes) -> bytearray:
        word_cnt = (len(payload) // 4) & 0xFF
        out_cmd = bytearray(CMD_HEADER)
        out_cmd.append(cmd)
        out_cmd.append(word_cnt)
        if payload:
            out_cmd.extend(payload)
        crc = crc16_ccitt(out_cmd[2:])
        out_cmd.extend(struct.pack("<H", crc))
        out_cmd.extend(CMD_TRAILER)
        return out_cmd

    def prime(self) -> None:
        # Prime with an invalid command.  This will generate an error
        # and force double buffered USB devices to respond after the
        # first command is sent.
        msg = self._build_command(0x90, b"")
        self.node.write(msg)
        self.primed = True

    async def connect_btl(self) -> None:
        output_line("Attempting to connect to bootloader")
        ret = await self.send_command('CONNECT')
        pinfo = ret[:12]
        mcu_info = ret[12:]
        ver_bytes: bytes
        ver_bytes, start_addr, self.block_size = struct.unpack("<4sII", pinfo)
        self.app_start_addr = start_addr
        self.software_version = "?"
        self.proto_version = tuple([v for v in reversed(ver_bytes[:3])])
        proto_version_str = ".".join([str(v) for v in self.proto_version])
        if self.block_size not in [64, 128, 256, 512]:
            raise FlashError("Invalid Block Size: %d" % (self.block_size,))
        mcu_info.rstrip(b"\x00")
        if self.proto_version >= (1, 1, 0):
            mcu_bytes, sv_bytes = mcu_info.split(b"\x00", maxsplit=1)
            mcu_type = mcu_bytes.decode()
            self.software_version = sv_bytes.decode()
        else:
            mcu_type = mcu_info.decode()
        output_line(
            f"Katapult Connected\n"
            f"Software Version: {self.software_version}\n"
            f"Protocol Version: {proto_version_str}\n"
            f"Block Size: {self.block_size} bytes\n"
            f"Application Start: 0x{self.app_start_addr:4X}\n"
            f"MCU type: {mcu_type}"
        )
        if self.klipper_dict is not None:
            bin_mcu = self.klipper_dict.get("config", {}).get("MCU", "")
            if bin_mcu and bin_mcu != mcu_type:
                output_line(
                    "WARNING: MCU returned by Katapult does not match MCU"
                    "stored in klipper.bin.\n"
                    f"Katapult MCU: {mcu_type}\n"
                    f"Klipper Binary MCU: {bin_mcu}"
                )

    async def verify_canbus_uuid(self, uuid):
        output_line("Verifying canbus connection")
        ret = await self.send_command('GET_CANBUS_ID')
        mcu_uuid = sum([v << ((5 - i) * 8) for i, v in enumerate(ret[:6])])
        if mcu_uuid != uuid:
            raise FlashError("UUID mismatch (%s vs %s)" % (uuid, mcu_uuid))

    async def send_command(
        self,
        cmdname: str,
        payload: bytes = b"",
        tries: int = 5
    ) -> bytearray:
        cmd = BOOTLOADER_CMDS[cmdname]
        out_cmd = self._build_command(cmd, payload)
        last_err = Exception()
        while tries:
            data = bytearray()
            recd_len = 0
            try:
                self.node.write(out_cmd)
                read_done = False
                while not read_done:
                    ret = await self.node.readuntil(CMD_TRAILER)
                    data.extend(ret)
                    while len(data) > 7:
                        if data[:2] != CMD_HEADER:
                            data = data[1:]
                            continue
                        recd_len = data[3] * 4
                        read_done = len(data) == recd_len + 8
                        break
                    if self.primed and read_done:
                        recd_len = 0
                        data.clear()
                        self.primed = False
                        read_done = False
            except asyncio.CancelledError:
                raise
            except asyncio.TimeoutError:
                logging.info(
                    f"Response for command {cmdname} timed out, "
                    f"{tries - 1} tries remaining"
                )
            except Exception as e:
                if type(e) is type(last_err) and e.args == last_err.args:
                    last_err = e
                    logging.exception("Device Read Error")
            else:
                trailer = data[-2:]
                recd_crc, = struct.unpack("<H", data[-4:-2])
                calc_crc = crc16_ccitt(data[2:-4])
                recd_ack = data[2]
                cmd_response = 0
                if recd_len:
                    cmd_response, = struct.unpack("<I", data[4:8])
                if trailer != CMD_TRAILER:
                    logging.info(
                        f"Command '{cmdname}': Invalid Trailer Received "
                        f"0x{trailer.hex()}"
                    )
                elif recd_crc != calc_crc:
                    logging.info(
                        f"Command '{cmdname}': Frame CRC Mismatch, expected: "
                        f"{calc_crc}, received {recd_crc}"
                    )
                elif recd_ack == ACK_ERROR:
                    logging.info(f"Command '{cmdname}': Received Error Response")
                elif recd_ack == ACK_BUSY:
                    logging.info(f"Command '{cmdname}': Received busy signal")
                    await asyncio.sleep(1.5)
                elif recd_ack != ACK_SUCCESS:
                    logging.info(f"Command '{cmdname}': Received NACK")
                elif cmd_response != cmd:
                    logging.info(
                        f"Command '{cmdname}': Acknowledged wrong command, "
                        f"expected: {cmd:2x}, received: {cmd_response:2x}"
                    )
                else:
                    # Validation passed, return payload sans command
                    if recd_len <= 4:
                        return bytearray()
                    return data[8:recd_len + 4]
            tries -= 1
            # clear the read buffer
            try:
                ret = await self.node.read(1024, timeout=.25)
            except asyncio.TimeoutError:
                pass
            else:
                logging.info(f"Read Buffer Contents: {ret!r}")
            await asyncio.sleep(.5)
        raise FlashError("Error sending command [%s] to Device" % (cmdname))

    async def send_file(self):
        last_percent = 0
        output_line("Flashing '%s'..." % (self.firmware_path))
        output("\n[")
        with open(self.firmware_path, 'rb') as f:
            f.seek(0, os.SEEK_END)
            self.file_size = f.tell()
            f.seek(0)
            flash_address = self.app_start_addr
            recd_addr = 0
            while True:
                buf = f.read(self.block_size)
                if not buf:
                    break
                if len(buf) < self.block_size:
                    buf += b"\xFF" * (self.block_size - len(buf))
                self.fw_sha.update(buf)
                prefix = struct.pack("<I", flash_address)
                for _ in range(3):
                    resp = await self.send_command('SEND_BLOCK', prefix + buf)
                    recd_addr, = struct.unpack("<I", resp)
                    if recd_addr == flash_address:
                        break
                    logging.info(
                        f"Block write mismatch: expected: {flash_address:4X}, "
                        f"received: {recd_addr:4X}"
                    )
                    await asyncio.sleep(.1)
                else:
                    raise FlashError(
                        f"Flash write failed, block address 0x{recd_addr:4X}"
                    )
                flash_address += self.block_size
                self.block_count += 1
                uploaded = self.block_count * self.block_size
                pct = int(uploaded / float(self.file_size) * 100 + .5)
                if pct >= last_percent + 2:
                    last_percent += 2.
                    output("#")
            resp = await self.send_command('SEND_EOF')
            page_count, = struct.unpack("<I", resp)
            output_line("]\n\nWrite complete: %d pages" % (page_count))

    async def verify_file(self):
        last_percent = 0
        output_line("Verifying (block count = %d)..." % (self.block_count,))
        output("\n[")
        ver_sha = hashlib.sha1()
        for i in range(self.block_count):
            flash_address = i * self.block_size + self.app_start_addr
            for _ in range(3):
                payload = struct.pack("<I", flash_address)
                resp = await self.send_command("REQUEST_BLOCK", payload)
                recd_addr, = struct.unpack("<I", resp[:4])
                if recd_addr == flash_address:
                    break
                logging.info(
                    f"Block read mismatch: expected: 0x{flash_address:4X}, "
                    f"received: 0x{recd_addr}"
                )
                await asyncio.sleep(.1)
            else:
                output_line("Error")
                raise FlashError("Block Request Error, block: %d" % (i,))
            ver_sha.update(resp[4:])
            pct = int(i * self.block_size / float(self.file_size) * 100 + .5)
            if pct >= last_percent + 2:
                last_percent += 2
                output("#")
        ver_hex = ver_sha.hexdigest().upper()
        fw_hex = self.fw_sha.hexdigest().upper()
        if ver_hex != fw_hex:
            raise FlashError("Checksum mismatch: Expected %s, Received %s"
                                % (fw_hex, ver_hex))
        output_line("]\n\nVerification Complete: SHA = %s" % (ver_hex))

    async def finish(self):
        await self.send_command("COMPLETE")


class CanNode:
    def __init__(self, node_id: int, cansocket: CanSocket | SerialSocket) -> None:
        self.node_id = node_id
        self._reader = asyncio.StreamReader(CAN_READER_LIMIT)
        self._cansocket = cansocket

    async def read(
        self, n: int = -1, timeout: Optional[float] = 2.
    ) -> bytes:
        return await asyncio.wait_for(self._reader.read(n), timeout)

    async def readexactly(
        self, n: int, timeout: Optional[float] = 2.
    ) -> bytes:
        return await asyncio.wait_for(self._reader.readexactly(n), timeout)

    async def readuntil(
        self, sep: bytes = b"\x03", timeout: Optional[float] = 2.
    ) -> bytes:
        return await asyncio.wait_for(self._reader.readuntil(sep), timeout)

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
        output_line("Sending bootloader jump command...")
        plist = [(uuid >> ((5 - i) * 8)) & 0xFF for i in range(6)]
        plist.insert(0, KLIPPER_REBOOT_CMD)
        self.send(KLIPPER_ADMIN_ID, bytes(plist))

    async def _query_uuids(self) -> List[int]:
        output_line("Checking for Katapult nodes...")
        payload = bytes([CANBUS_CMD_QUERY_UNASSIGNED])
        self.admin_node.write(payload)
        curtime = self._loop.time()
        endtime = curtime + 2.
        self.uuids: List[int] = []
        while curtime < endtime:
            timeout = max(.1, endtime - curtime)
            try:
                resp = await self.admin_node.read(8, timeout)
            except asyncio.TimeoutError:
                continue
            finally:
                curtime = self._loop.time()
            if len(resp) < 7 or resp[0] != CANBUS_RESP_NEED_NODEID:
                continue
            app_names = {
                KLIPPER_SET_NODE_CMD: "Klipper",
                CANBUS_CMD_SET_NODEID: "Katapult"
            }
            app = "Unknown"
            if len(resp) > 7:
                app = app_names.get(resp[7], "Unknown")
            data = resp[1:7]
            output_line(f"Detected UUID: {data.hex()}, Application: {app}")
            uuid = sum([v << ((5 - i) * 8) for i, v in enumerate(data)])
            if uuid not in self.uuids and app == "Katapult":
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
        node_id = len(self.nodes) + CANBUS_NODEID_OFFSET
        plist.append(node_id)
        payload = bytes(plist)
        self.admin_node.write(payload)
        decoded_id = node_id * 2 + 0x100
        node = CanNode(decoded_id, self)
        self.nodes[decoded_id + 1] = node
        return node

    async def run(
        self, intf: str, uuid: int, fw_path: pathlib.Path, req_only: bool
    ) -> None:
        if not req_only and not fw_path.is_file():
            raise FlashError("Invalid firmware path '%s'" % (fw_path))
        try:
            self.cansock.bind((intf,))
        except Exception:
            raise FlashError("Unable to bind socket to can0")
        self.closed = False
        self.cansock.setblocking(False)
        self._loop.add_reader(
            self.cansock.fileno(), self._handle_can_response)
        self._jump_to_bootloader(uuid)
        await asyncio.sleep(.5)
        if req_only:
            output_line("Bootloader request command sent")
            return
        self._reset_nodes()
        await asyncio.sleep(1.0)
        node = self._set_node_id(uuid)
        flasher = CanFlasher(node, fw_path)
        await asyncio.sleep(.5)
        try:
            await flasher.connect_btl()
            await flasher.verify_canbus_uuid(uuid)
            await flasher.send_file()
            await flasher.verify_file()
        finally:
            # always attempt to send the complete command. If
            # there is an error it will exit the bootloader
            # unless comms were broken
            await flasher.finish()

    async def run_query(self, intf: str):
        try:
            self.cansock.bind((intf,))
        except Exception:
            raise FlashError("Unable to bind socket to can0")
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

class SerialSocket:
    def __init__(self, loop: asyncio.AbstractEventLoop):
        self._loop = loop
        self.serial: Optional[Serial] = None
        self.node = CanNode(0, self)

    def _handle_response(self) -> None:
        assert self.serial is not None
        try:
            data = self.serial.read(4096)
        except SerialException:
            logging.exception("Error on serial read")
            self.close()
        else:
            self.node.feed_data(data)

    def send(self, can_id: int, payload: bytes = b"") -> None:
        assert self.serial is not None
        try:
            self.serial.write(payload)
        except SerialException:
            logging.exception("Error on serial write")
            self.close()

    async def _lookup_proc_name(self, process_id: str) -> str:
        has_sysctl = shutil.which("systemctl") is not None
        if has_sysctl:
            cmd = shlex.split(f"systemctl status {process_id}")
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            stdout, _ = await proc.communicate()
            resp = stdout.strip().decode(errors="ignore")
            if resp:
                unit = resp.split(maxsplit=2)
                if len(unit) == 3:
                    return f"Systemd Unit Name: {unit[1]}"
        cmdline_file = pathlib.Path(f"/proc/{process_id}/cmdline")
        if cmdline_file.exists():
            res = cmdline_file.read_text().replace("\x00", " ").strip()
            return f"Command Line: {res}"
        exe_file = pathlib.Path(f"/proc/{process_id}/exe")
        if exe_file.exists():
            return f"Executable: {exe_file.resolve()})"
        return "Name Unknown"

    async def validate_device(self, dev_strpath: str) -> None:
        dev_path = pathlib.Path(dev_strpath)
        if not dev_path.exists():
            raise FlashError(f"No Serial Device found at {dev_path}")
        try:
            dev_st = dev_path.stat()
        except PermissionError as e:
            raise FlashError(f"No permission to access device {dev_path}") from e
        dev_id = (dev_st.st_dev, dev_st.st_ino)
        for fd_dir in pathlib.Path("/proc").glob("*/fd"):
            pid = fd_dir.parent.name
            if not pid.isdigit():
                continue
            with contextlib.suppress(OSError):
                for item in fd_dir.iterdir():
                    try:
                        item_st = item.stat()
                    except OSError:
                        continue
                    item_id = (item_st.st_dev, item_st.st_ino)
                    if item_id == dev_id:
                        proc_name = await self._lookup_proc_name(pid)
                        output_line(
                            f"Serial device {dev_path} in use by another program.\n"
                            f"Process ID: {pid}\n"
                            f"Process {proc_name}"
                        )
                        raise FlashError(f"Serial device {dev_path} in use")

    async def _request_usb_bootloader(self, device: pathlib.Path) -> pathlib.Path:
        output_line(f"Requesting USB bootloader for {device}...")
        usb_dev_path = get_usb_path(device)
        if usb_dev_path is None:
            output_line(f"Device path {device} is not a usb device")
            return device
        stable_path = get_stable_usb_symlink(device)
        fd: Optional[int] = None
        with contextlib.suppress(OSError):
            fd = os.open(str(device), os.O_RDWR)
            fcntl.ioctl(
                fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR)
            )
            t = termios.tcgetattr(fd)
            t[4] = t[5] = termios.B1200
            termios.tcsetattr(fd, termios.TCSANOW, t)
            fcntl.ioctl(
                fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR)
            )
        if fd is not None:
            os.close(fd)
        output("Waiting for USB Reconnect.")
        for _ in range(8):
            await asyncio.sleep(.5)
            output(".")
            usb_info = get_usb_info(usb_dev_path)
            mfr = usb_info.get("manufacturer")
            if mfr == "katapult":
                output_line("Katapult detected")
                await asyncio.sleep(1.0)
                break
        else:
            output_line("timed out")
        return stable_path

    async def _request_serial_bootloader(self, device: str, baud: int) -> None:
        output_line(f"Requesting serial bootloader for device {device}...")
        self.serial = self._open_device(device, baud)
        self.send(0, SERIAL_BL_REQ)
        await asyncio.sleep(1.)
        if self.serial is not None:
            self.close()

    def _open_device(self, device: str, baud: int) -> Serial:
        try:
            serial_dev = Serial(                            # type: ignore
                baudrate=baud, timeout=0, exclusive=True
            )
            serial_dev.port = device
            serial_dev.open()
        except (OSError, IOError, SerialException) as e:
            raise FlashError("Unable to open serial port: %s" % (e,))
        return serial_dev

    def _has_double_buffering(self, product: str) -> bool:
        if product.startswith("stm32"):
            variant = product[5:7]
            return variant not in ("f2", "f4", "h7")
        return False

    async def run(
        self, intf: str, baud: int, fw_path: pathlib.Path, req_only: bool
    ) -> None:
        if not fw_path.is_file():
            raise FlashError("Invalid firmware path '%s'" % (fw_path))
        await self.validate_device(intf)
        intf_path = pathlib.Path(intf)
        usb_dev_path = get_usb_path(intf_path)
        dev_info: Dict[str, Any] = {}
        if usb_dev_path is not None:
            dev_info = get_usb_info(usb_dev_path)
        usb_id = dev_info.get("usb_id")
        usb_mfr = dev_info.get("manufacturer")
        usb_prod: str = dev_info.get("product", "unknown")
        if usb_mfr == "klipper" or usb_id == KLIPPER_USB_ID:
            # Request usb bootloader, wait for katapult
            output_line("Detected USB device running Klipper")
            new_intf = await self._request_usb_bootloader(intf_path)
            intf = str(new_intf)
            if req_only:
                return
        elif usb_mfr == "katapult" or usb_id == KATAPULT_USB_ID:
            output_line("Detected USB device running Katapult")
            if req_only:
                return
        elif req_only:
            # Request serial bootloader and exit
            await self._request_serial_bootloader(intf, baud)
            return
        else:
            usb_prod = ""
        self.serial = self._open_device(intf, baud)
        self._loop.add_reader(self.serial.fileno(), self._handle_response)
        flasher = CanFlasher(self.node, fw_path)
        try:
            if self._has_double_buffering(usb_prod):
                # Prime the USB Connection with a dummy command.  This is
                # necessary to get STM32 devices with usbfs double buffering
                # to respond immediately to the connect command.
                flasher.prime()
            await flasher.connect_btl()
            await flasher.send_file()
            await flasher.verify_file()
        finally:
            # always attempt to send the complete command. If
            # there is an error it will exit the bootloader
            # unless comms were broken
            await flasher.finish()

    def close(self):
        if self.serial is None:
            return
        self._loop.remove_reader(self.serial.fileno())
        self.serial.close()
        self.serial = None

async def main(args: argparse.Namespace) -> int:
    if not args.verbose:
        logging.getLogger().setLevel(logging.ERROR)
    intf = args.interface
    fpath = pathlib.Path(args.firmware).expanduser().resolve()
    loop = asyncio.get_running_loop()
    iscan = args.device is None
    req_only = args.request_bootloader
    sock: CanSocket | SerialSocket | None = None
    try:
        if iscan:
            sock = CanSocket(loop)
            if args.query:
                await sock.run_query(intf)
            else:
                if args.uuid is None:
                    raise FlashError(
                        "The 'uuid' option must be specified to flash a device"
                    )
                output_line(f"Flashing CAN UUID {args.uuid} on interface {intf}")
                uuid = int(args.uuid, 16)
                await sock.run(intf, uuid, fpath, req_only)
        else:
            if not HAS_SERIAL:
                ser_inst_cmd = "pip3 install serial"
                if shutil.which("apt") is not None:
                    ser_inst_cmd = "sudo apt install python3-serial"
                raise FlashError(
                    "The pyserial python package was not found.  To install "
                    "run the following command in a terminal: \n\n"
                    f"   {ser_inst_cmd}\n\n"
                )
            if args.device is None:
                raise FlashError(
                    "The 'device' option must be specified to flash a device"
                )
            output_line(f"Flashing Serial Device {args.device}, baud {args.baud}")
            sock = SerialSocket(loop)
            await sock.run(args.device, args.baud, fpath, req_only)
    except Exception:
        logging.exception("Flash Error")
        return 1
    finally:
        if sock is not None:
            sock.close()
    if args.query:
        output_line("Query Complete")
    else:
        output_line("Flash Success")
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Katapult Flash Tool")
    parser.add_argument(
        "-d", "--device", metavar='<serial device>',
        help="Serial Device"
    )
    parser.add_argument(
        "-b", "--baud", default=250000, metavar='<baud rate>',
        help="Serial baud rate"
    )
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
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Enable verbose responses"
    )
    parser.add_argument(
        "-r", "--request-bootloader", action="store_true",
        help="Requests the bootloader and exits"
    )

    args = parser.parse_args()
    exit(asyncio.run(main(args)))
