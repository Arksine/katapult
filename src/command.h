#ifndef __COMMAND_H
#define __COMMAND_H

#include <stdarg.h> // va_list
#include <stddef.h>
#include <stdint.h> // uint32_t
#include "ctr.h" // DECL_CTR

// Declare a constant exported to the host
#define DECL_CONSTANT(NAME, VALUE)                              \
    DECL_CTR_INT("DECL_CONSTANT " NAME, 1, CTR_INT(VALUE))
#define DECL_CONSTANT_STR(NAME, VALUE)                  \
    DECL_CTR("DECL_CONSTANT_STR " NAME " " VALUE)

// Declare an enumeration
#define DECL_ENUMERATION(ENUM, NAME, VALUE)                             \
    DECL_CTR_INT("DECL_ENUMERATION " ENUM " " NAME, 1, CTR_INT(VALUE))
#define DECL_ENUMERATION_RANGE(ENUM, NAME, VALUE, COUNT)        \
    DECL_CTR_INT("DECL_ENUMERATION_RANGE " ENUM " " NAME,       \
                 2, CTR_INT(VALUE), CTR_INT(COUNT))

// Shutdown wrappers for Klipper compatibility
#define shutdown(msg)     do { } while (1)
#define try_shutdown(msg) do { } while (0)

#define PROTO_VERSION   0x00010000      // Version 1.0.0
#define CMD_CONNECT       0x11
#define CMD_RX_BLOCK      0x12
#define CMD_RX_EOF        0x13
#define CMD_REQ_BLOCK     0x14
#define CMD_COMPLETE      0x15
#define CMD_GET_CANBUS_ID 0x16
#define RESPONSE_ACK           0xa0
#define RESPONSE_NACK          0xf1
#define RESPONSE_COMMAND_ERROR 0xf2

// Command Format:
// <2 byte header> <1 byte cmd> <1 byte data word count> <data> <2 byte crc> <2 byte trailer>
#define MESSAGE_MIN 8
#define MESSAGE_MAX 128
#define MESSAGE_HEADER_SIZE  4
#define MESSAGE_TRAILER_SIZE 4
#define MESSAGE_POS_STX1 0
#define MESSAGE_POS_STX2 1
#define MESSAGE_POS_LEN  3
#define MESSAGE_TRAILER_CRC   4
#define MESSAGE_TRAILER_SYNC2 2
#define MESSAGE_TRAILER_SYNC  1
#define MESSAGE_STX1  0x01
#define MESSAGE_STX2  0x88
#define MESSAGE_SYNC2 0x99
#define MESSAGE_SYNC  0x03

// command handlers
void command_connect(uint32_t *data);
void command_read_block(uint32_t *data);
void command_write_block(uint32_t *data);
void command_eof(uint32_t *data);
void command_complete(uint32_t *data);
void command_get_canbus_id(uint32_t *data);

// command.c
void command_respond_ack(uint32_t acked_cmd, uint32_t *out, uint32_t out_len);
void command_respond_command_error(void);
int command_get_arg_count(uint32_t *data);

struct command_encoder {
    uint32_t *data;
    uint_fast8_t max_size;
};
uint_fast8_t command_encode_and_frame(
    uint8_t *buf, const struct command_encoder *ce, va_list args);
int_fast8_t command_find_block(uint8_t *buf, uint_fast8_t buf_len
                               , uint_fast8_t *pop_count);
void command_dispatch(uint8_t *buf, uint_fast8_t msglen);
void command_send_ack(void);
int_fast8_t command_find_and_dispatch(uint8_t *buf, uint_fast8_t buf_len
                                      , uint_fast8_t *pop_count);

#endif // command.h
