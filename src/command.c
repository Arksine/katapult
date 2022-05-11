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

uint_fast8_t
command_encode_and_frame(uint8_t *buf, const struct command_encoder *ce
                         , va_list args)
{
    memcpy(buf, ce->data, ce->max_size);
    return ce->max_size;
}

static void
command_respond(uint32_t *data, uint32_t cmdid, uint32_t data_len)
{
    // First four bytes: 2 byte header, ack_type, data length
    data[0] = cpu_to_le32((data_len - 2) << 24 | cmdid << 16 | 0x8801);
    // calculate the CRC
    uint16_t crc = crc16_ccitt((uint8_t *)data + 2, (data_len - 2) * 4 + 2);
    data[data_len - 1] = cpu_to_le32(0x0399 << 16 | crc);

    struct command_encoder ce = { .data = data, .max_size = data_len * 4 };
    console_sendf(&ce, (va_list){});
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

// Dispatch all the commands found in a message block
void
command_dispatch(uint8_t *buf, uint_fast8_t msglen)
{
    uint32_t data[DIV_ROUND_UP(MESSAGE_MAX, 4)];
    memcpy(data, buf, msglen);
    uint32_t cmd = (le32_to_cpu(data[0]) >> 16) & 0xff;
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
            if (CONFIG_CANSERIAL) {
                command_get_canbus_id(data);
                break;
            }
            // NO BREAK
        default:
            // Unknown command or gabage data, NACK it
            command_respond_command_error();
    }
}

enum { CF_NEED_SYNC=1<<0, CF_NEED_VALID=1<<1 };

// Find the next complete message block
int_fast8_t
command_find_block(uint8_t *buf, uint_fast8_t buf_len, uint_fast8_t *pop_count)
{
    static uint8_t sync_state;
    if (buf_len && sync_state & CF_NEED_SYNC)
        goto need_sync;
    if (buf_len < MESSAGE_MIN)
        goto need_more_data;
    if (buf[MESSAGE_POS_STX1] != MESSAGE_STX1
        || buf[MESSAGE_POS_STX2] != MESSAGE_STX2)
        goto error;
    uint_fast8_t msglen = buf[MESSAGE_POS_LEN] * 4 + 8;
    if (msglen < MESSAGE_MIN || msglen > MESSAGE_MAX)
        goto error;
    if (buf_len < msglen)
        goto need_more_data;
    if (buf[msglen-MESSAGE_TRAILER_SYNC2] != MESSAGE_SYNC2
        || buf[msglen-MESSAGE_TRAILER_SYNC] != MESSAGE_SYNC)
        goto error;
    uint16_t msgcrc = (buf[msglen-MESSAGE_TRAILER_CRC]
                       | (buf[msglen-MESSAGE_TRAILER_CRC+1] << 8));
    uint16_t crc = crc16_ccitt(buf+2, msglen-MESSAGE_TRAILER_SIZE-2);
    if (crc != msgcrc)
        goto error;
    sync_state &= ~CF_NEED_VALID;
    *pop_count = msglen;
    return 1;

need_more_data:
    *pop_count = 0;
    return 0;
error:
    sync_state |= CF_NEED_SYNC;
need_sync: ;
    // Discard bytes until next SYNC found
    uint8_t *next_sync = memchr(buf, MESSAGE_STX1, buf_len);
    if (next_sync) {
        sync_state &= ~CF_NEED_SYNC;
        *pop_count = next_sync - buf;
    } else {
        *pop_count = buf_len;
    }
    if (sync_state & CF_NEED_VALID)
        return -1;
    sync_state |= CF_NEED_VALID;
    command_respond_nack();
    return -1;
}

// Compat wrapper for klipper low-level code
void
command_send_ack(void)
{
}

// Find a message block and then dispatch all the commands in it
int_fast8_t
command_find_and_dispatch(uint8_t *buf, uint_fast8_t buf_len
                          , uint_fast8_t *pop_count)
{
    int_fast8_t ret = command_find_block(buf, buf_len, pop_count);
    if (ret > 0) {
        command_dispatch(buf, *pop_count);
        command_send_ack();
    }
    return ret;
}
