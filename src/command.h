#ifndef __COMMAND_H
#define __COMMAND_H

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

#define PROTO_VERSION   0x00010000      // Version 1.0.0
#define PROTO_SIZE      4
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
#define CMD_HEADER      0x0188
#define CMD_TRAILER     0x9903

// command handlers
void command_connect(uint32_t *data);
void command_read_block(uint32_t *data);
void command_write_block(uint32_t *data);
void command_eof(uint32_t *data);
void command_complete(uint32_t *data);
void command_get_canbus_id(uint32_t *data);

// board specific code
void console_process_tx(uint8_t *data, uint32_t size);

// command.c
void command_respond_ack(uint32_t acked_cmd, uint32_t *out, uint32_t out_len);
void command_respond_command_error(void);
int command_get_arg_count(uint32_t *data);
void console_process_rx(uint8_t *data, uint32_t len);

#endif // command.h
