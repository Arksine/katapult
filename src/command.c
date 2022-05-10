// Command processing
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "board/misc.h" // crc16_ccitt
#include "byteorder.h" // cpu_to_le32
#include "command.h" // send_ack

// Input Tracking
#define CMD_BUF_SIZE (CONFIG_BLOCK_SIZE + 64)
static uint8_t cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_pos = 0;

static void
command_respond(uint32_t *data, uint32_t cmdid, uint32_t data_len)
{
    // First four bytes: 2 byte header, ack_type, data length
    data[0] = cpu_to_le32((data_len - 2) << 24 | cmdid << 16 | 0x8801);
    // calculate the CRC
    uint16_t crc = crc16_ccitt((uint8_t *)data + 2, (data_len - 2) * 4 + 2);
    data[data_len - 1] = cpu_to_le32(0x0399 << 16 | crc);
    console_process_tx((uint8_t *)data, data_len * 4);
}

void
command_respond_ack(uint32_t acked_cmd, uint32_t *out, uint32_t out_len)
{
    out[1] = cpu_to_le32(acked_cmd);
    command_respond(out, RESPONSE_ACK, out_len);
}

void
command_respond_command_error(void)
{
    uint32_t out[2];
    command_respond(out, RESPONSE_COMMAND_ERROR, ARRAY_SIZE(out));
}

static void
command_respond_nack(void)
{
    uint32_t out[2];
    command_respond(out, RESPONSE_NACK, ARRAY_SIZE(out));
}

int
command_get_arg_count(uint32_t *data)
{
    return le32_to_cpu(data[0]) >> 24;
}

static void
process_command(uint8_t cmd, uint32_t *data, uint8_t data_len)
{
    switch (cmd) {
        case CMD_CONNECT:
            command_connect(data);
            break;
        case CMD_RX_BLOCK:
            command_write_block(data);
            break;
        case CMD_RX_EOF:
            command_eof(data);
            break;
        case CMD_REQ_BLOCK:
            command_read_block(data);
            break;
        case CMD_COMPLETE:
            command_complete(data);
            break;
        case CMD_GET_CANBUS_ID:
            command_get_canbus_id(data);
            break;
        default:
            // Unknown command or gabage data, NACK it
            command_respond_command_error();
    }
}

static void
decode_command(void)
{
    uint8_t remaining = cmd_pos;
    uint8_t *tmpbuf = cmd_buf;
    while (remaining) {
        if (tmpbuf[0] == 0x01) {
            // potential match
            if (remaining >= PROTO_SIZE) {
                uint16_t header = tmpbuf[0] << 8 | tmpbuf[1];
                uint8_t cmd = tmpbuf[2];
                uint8_t length = tmpbuf[3];
                uint16_t full_length = PROTO_SIZE * 2 + length * 4;
                if (header == CMD_HEADER) {
                    if (full_length > CMD_BUF_SIZE) {
                        // packet too large, nack it and move on
                        command_respond_nack();
                    } else if (remaining >= full_length) {
                        remaining -= full_length;
                        uint16_t fpos = full_length - 4;
                        uint16_t trailer = tmpbuf[fpos + 2] << 8 | tmpbuf[fpos + 3];
                        if (trailer != CMD_TRAILER) {
                            command_respond_nack();
                        } else {
                            uint16_t crc = le16_to_cpu(*(uint16_t *)(&tmpbuf[fpos]));
                            uint16_t calc_crc = crc16_ccitt(&tmpbuf[2], full_length - 6);
                            if (crc != calc_crc) {
                                command_respond_nack();
                            } else {
                                // valid command, process
                                process_command(cmd, (uint32_t *)tmpbuf, length);
                            }
                        }
                        if (!remaining)
                            break;
                    } else {
                        // Header is valid, haven't received full packet
                        break;
                    }
                }
            } else {
                // Not enough data, check again after the next read
                break;
            }
        }
        remaining--;
        tmpbuf++;
    }
    if (remaining) {
        // move the buffer
        uint8_t rpos = cmd_pos - remaining;
        memmove(&cmd_buf[0], &cmd_buf[rpos], remaining);
    }
    cmd_pos = remaining;
}

void
console_process_rx(uint8_t *data, uint32_t len)
{
    // read into the command buffer
    if (cmd_pos >= CMD_BUF_SIZE)
        return;
    else if (cmd_pos + len > CMD_BUF_SIZE)
        len = CMD_BUF_SIZE - cmd_pos;
    memcpy(&cmd_buf[cmd_pos], data, len);
    cmd_pos += len;
    if (cmd_pos > PROTO_SIZE)
        decode_command();
}
