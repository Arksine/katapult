#!/usr/bin/python3
# Python 3 port of "CanSerial"
#
# Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import asyncio
import socket
import os
import sys
import pty
import termios
import fcntl
import struct
import time
import logging
import errno

CAN_FMT = "<IB3x8s"
CANBUS_ID_UUID = 0x321
CANBUS_ID_SET = 0x322
CANBUS_ID_UUID_RESP = 0x323
CANBUS_UUID_LEN = 6
CANBUS_PORT_OFFSET = 0x180
UUID_CONFIG_FILE = "/var/tmp/canuuids.cfg"
UPDATE_REQUEST_TIME = 5.

class CanSerialError(Exception):
    pass

class PeriodicCallback:
    def __init__(self, period, callback, *args):
        try:
            self.aio_loop = asyncio.get_running_loop()
        except RuntimeError:
            self.aio_loop = asyncio.get_event_loop()
        self.running = False
        self.cb = callback
        self.period = period
        self.args = args
        self.timer_hdl = None

    def _create_cb_task(self):
        asyncio.create_task(self._cb_wrapper())

    async def _cb_wrapper(self):
        ret = self.cb(*self.args)
        if asyncio.iscoroutine(ret):
            await ret
        if self.running:
            self.timer_hdl = self.aio_loop.call_later(
                self.period, self._create_cb_task)

    def start(self):
        self.running = True
        self.timer_hdl = self.aio_loop.call_later(
            self.period, self._create_cb_task)

    def stop(self):
        self.running = False
        if self.timer_hdl is not None:
            self.timer_hdl.cancel()
            self.timer_hdl = None

class CanDeviceLink:
    def __init__(self, canserial, uuid_bytes, can_id):
        self.canserial = canserial
        self.aio_loop = canserial.aio_loop
        self.uuid_bytes = uuid_bytes
        self.uuid_str = "".join([f"{b:02x}" for b in self.uuid_bytes])
        self.can_id = can_id
        self.last_recd_time = 0
        self.ping_tries = 0
        self.pty_fd = None
        self._create_pty()
        # Send ID Back after creation
        self.respond_id_request()

    def _create_pty(self):
        ptyname = f"/tmp/ttyCAN0_{self.uuid_str}"
        try:
            os.unlink(ptyname)
        except Exception:
            pass
        mfd, sfd = pty.openpty()
        fname = os.ttyname(sfd)
        os.chmod(fname, 0o660)
        os.symlink(fname, ptyname)
        attrs = termios.tcgetattr(mfd)
        attrs[3] = attrs[3] & ~termios.ECHO
        termios.tcsetattr(mfd, termios.TCSADRAIN, attrs)
        fl = fcntl.fcntl(mfd, fcntl.F_GETFL) | os.O_NONBLOCK
        fcntl.fcntl(mfd, fcntl.F_SETFL, fl)
        self.aio_loop.add_reader(mfd, self._handle_pty_data)
        self.pty_fd = mfd

    def _handle_pty_data(self):
        try:
            data = os.read(self.pty_fd, 4096)
        except Exception:
            logging.exception("Error reading pty")
            return
        self.canserial.can_send(self.can_id, data)

    def ping(self):
        time_diff = self.aio_loop.time() - self.last_recd_time
        if time_diff < UPDATE_REQUEST_TIME + 1.:
            # No need to ping as we are receiving data
            # from the Can Device
            return
        if self.ping_tries > 3:
            logging.info(f"Connection Timed Out: {self.uuid_str}")
            self.close()
            return
        self.ping_tries += 1
        self.canserial.can_send(self.can_id)

    def handle_can_data(self, data):
        # TODO: we may need to make sure something is connected
        # to the other end of the pty.  We shouldn't write to
        # it otherwise.

        self.last_recd_time = self.aio_loop.time()
        if not data:
            # This is pong
            self.ping_tries = 0
            return
        try:
            os.write(self.pty_fd, data)
        except Exception:
            logging.exception("Error writing to pty")

    def respond_id_request(self):
        self.last_recd_time = self.aio_loop.time()
        can_id_bytes = struct.pack("<H", self.can_id)
        response = can_id_bytes + self.uuid_bytes
        self.canserial.can_send(CANBUS_ID_SET, response)

    def close(self):
        self.canserial.remove_device(self.can_id)
        self.aio_loop.remove_reader(self.pty_fd)
        try:
            os.close(self.pty_fd)
        except Exception:
            pass

class CanSerial:
    def __init__(self):
        self.aio_loop = asyncio.get_event_loop()
        self.cansock = socket.socket(socket.PF_CAN, socket.SOCK_RAW,
                                     socket.CAN_RAW)
        self.device_links = {}
        self.can_response_cbs = {CANBUS_ID_UUID_RESP: self._handle_set_id}
        self.can_status_cb = PeriodicCallback(UPDATE_REQUEST_TIME,
                                              self._request_can_status)
        self.input_buffer = b""
        self.output_packets = []
        self.input_busy = False
        self.output_busy = False
        with open(UUID_CONFIG_FILE, 'r') as f:
            data = f.read()
        lines = [line.strip() for line in data.split('\n')
                 if line.strip() and line.strip()[0] != "#"]
        self.mapped_can_devs = {}
        for line in lines:
            port, uuid = line.split()
            can_id = 2 * int(port) + CANBUS_PORT_OFFSET
            self.mapped_can_devs[uuid] = can_id

    def remove_device(self, can_id):
        self.can_response_cbs.pop(can_id + 1)
        self.device_links.pop(can_id)

    def _handle_can_response(self):
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

    def _process_packet(self, packet):
        can_id, length, data = struct.unpack(CAN_FMT, packet)
        can_id &= socket.CAN_EFF_MASK
        payload = data[:length]
        canfunc = self.can_response_cbs.get(can_id)
        if canfunc is None:
            logging.info(f"No response callback for CAN ID {can_id:3X}'")
            return
        canfunc(payload)

    def _handle_set_id(self, uuid_bytes):
        if len(uuid_bytes) != CANBUS_UUID_LEN:
            logging.info(f"Invalid length for UUID: {uuid_bytes}")
            return
        uuid = ":".join([f"{b:02X}" for b in uuid_bytes])
        can_id = None
        if uuid in self.mapped_can_devs:
            can_id = self.mapped_can_devs[uuid]
            if can_id in self.device_links:
                self.device_links[can_id].respond_id_request()
                return
        else:
            can_id = self._assign_can_port(uuid)
        new_dev = CanDeviceLink(self, uuid_bytes, can_id)
        self.device_links[can_id] = new_dev
        self.can_response_cbs[can_id + 1] = new_dev.handle_can_data

    def _assign_can_port(self, uuid):
        port = len(self.mapped_can_devs) + 1
        with open(UUID_CONFIG_FILE, "a") as f:
            f.write(f"{port} {uuid}\n")
        can_id = 2 * port + CANBUS_PORT_OFFSET
        self.mapped_can_devs[uuid] = can_id
        return can_id

    def _request_can_status(self):
        # See Check for new devices
        self.can_send(CANBUS_ID_UUID)
        # Ping Each device
        for dev in list(self.device_links.values()):
            dev.ping()

    def can_send(self, can_id, payload=b""):
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
                await self.aio_loop.sock_sendall(self.cansock, packet)
            except socket.error:
                logging.info("Socket Write Error, closing")
                self.close()
                break
        self.output_busy = False

    def start(self):
        try:
            self.cansock.bind(("can0",))
        except Exception:
            raise CanSerialError("Unable to bind socket to can0")
        self.cansock.setblocking(False)
        self.aio_loop.add_reader(
            self.cansock.fileno(), self._handle_can_response)
        self.can_status_cb.start()
        self.aio_loop.run_forever()

    def close(self):
        self.can_status_cb.stop()
        self.aio_loop.remove_reader(self.cansock.fileno())
        for dev in list(self.device_links.values()):
            dev.close()
        self.aio_loop.stop()

def main():
    while True:
        try:
            canserial = CanSerial()
            canserial.start()
        except Exception as e:
            logging.exception(str(e))
            time.sleep(5.)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    # Initialize UUID file if it doesn't exist
    if not os.path.exists(UUID_CONFIG_FILE):
        with open(UUID_CONFIG_FILE, "w") as f:
            f.write("# [port] [UUID]\n")
    main()
