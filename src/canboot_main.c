// Canboot main event loop
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h>         // memmove
#include "autoconf.h"       // CONFIG_*
#include "board/misc.h"     // delay_ms
#include "board/canbus.h"   // canbus_send
#include "board/flash.h"    // write_page
#include "board/gpio.h"     // gpio_in_setup
#include "canboot_main.h"   // canboot_main
#include "ctr.h"            // DECL_CTR
#include "led.h"            // check_blink_time
#include "byteorder.h"      // cpu_to_le32


#define PROTO_SIZE      4
#define CMD_BUF_SIZE    CONFIG_BLOCK_SIZE + 64
#define MAX_OBUF_SIZE   CONFIG_BLOCK_SIZE + 16
#define CMD_CONNECT     0x11
#define CMD_RX_BLOCK    0x12
#define CMD_RX_EOF      0x13
#define CMD_REQ_BLOCK   0x14
#define CMD_COMPLETE    0x15
#define ACK_COMMAND     0xa0

// Command Format:
// <2 byte header> <1 byte cmd> <1 byte data word count> <data> <2 byte crc> <2 byte trailer>
#define CMD_HEADER      0x0188
#define CMD_TRAILER     0x9903

#define WAIT_BLINK_TIME 1000000
#define XFER_BLINK_TIME 20000

#define REQUEST_SIG    0x5984E3FA6CA1589B // Random request sig

static uint8_t nack[8] = {0x01, 0x88, 0xF1, 0x00, 0x68, 0x95, 0x99, 0x03};
static uint8_t page_buffer[CONFIG_MAX_FLASH_PAGE_SIZE];
// Input Tracking
static uint8_t cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_pos = 0;
// Page Tracking
static uint16_t last_page_written = 0;
static uint8_t page_pending = 0;
static uint32_t blink_time = WAIT_BLINK_TIME;
static uint8_t complete = 0;

static void
send_ack(uint32_t* data, uint8_t payload_len)
{
    // First four bytes: 2 byte header, ack_type, data length
    data[0] = cpu_to_le32(payload_len << 24 | ACK_COMMAND << 16 | 0x8801);
    // calculate the CRC
    uint16_t crc = crc16_ccitt((uint8_t *)data + 2, payload_len * 4 + 2);
    data[payload_len + 1] = cpu_to_le32(0x0399 << 16 | crc);
    canboot_sendf((uint8_t *)data, (payload_len + 2) * 4);
}

static void
write_page(uint16_t page)
{
    flash_write_page(page, (uint16_t*)page_buffer);
    memset(page_buffer, 0xFF, sizeof(page_buffer));
    last_page_written = page;
    page_pending = 0;
}

static void
process_read_block(uint32_t* data, uint8_t data_len) {
    uint32_t block_index = le32_to_cpu(data[0]);
    uint8_t word_len = CONFIG_BLOCK_SIZE / 4 + 2;
    uint32_t out[MAX_OBUF_SIZE];
    out[1] = cpu_to_le32(CMD_REQ_BLOCK);
    out[2] = cpu_to_le32(block_index);
    flash_read_block(block_index, &out[3]);
    send_ack(out, word_len);
}

static void
process_write_block(uint32_t* data, uint8_t data_len) {
    if (data_len != (CONFIG_BLOCK_SIZE / 4) + 1) {
        canboot_sendf(nack, 8);
        return;
    }
    uint32_t block_index = le32_to_cpu(data[0]);
    uint32_t byte_addr = block_index * CONFIG_BLOCK_SIZE;
    uint32_t flash_page_size = flash_get_page_size();
    uint32_t page_pos = byte_addr % flash_page_size;
    uint16_t page_index = byte_addr / flash_page_size;
    memcpy(&page_buffer[page_pos], (uint8_t *)&data[1], CONFIG_BLOCK_SIZE);
    page_pending = 1;
    page_pos += CONFIG_BLOCK_SIZE;
    if (page_pos == flash_page_size)
        write_page(page_index);
    uint32_t out[4];
    out[1] = cpu_to_le32(CMD_RX_BLOCK);
    out[2] = cpu_to_le32(block_index);
    send_ack(out, 2);
}

static void
process_eof(void)
{
    if (page_pending)
        write_page(last_page_written + 1);
    flash_complete();
    uint32_t out[4];
    out[1] = cpu_to_le32(CMD_RX_EOF);
    out[2] = cpu_to_le32(last_page_written + 1);
    send_ack(out, 2);
}

static void
process_complete(void)
{
    uint32_t out[3];
    out[1] = cpu_to_le32(CMD_COMPLETE);
    send_ack(out, 1);
    complete = 1;
}

static void
process_connnect(void)
{   uint32_t out[4];
    out[1] = cpu_to_le32(CMD_CONNECT);
    out[2] = cpu_to_le32(CONFIG_BLOCK_SIZE);
    send_ack(out, 2);

}

static inline void
process_command(uint8_t cmd, uint32_t* data, uint8_t data_len)
{
    switch (cmd) {
        case CMD_CONNECT:
            process_connnect();
            break;
        case CMD_RX_BLOCK:
            blink_time = XFER_BLINK_TIME;
            process_write_block(data, data_len);
            break;
        case CMD_RX_EOF:
            blink_time = WAIT_BLINK_TIME;
            process_eof();
            break;
        case CMD_REQ_BLOCK:
            blink_time = XFER_BLINK_TIME;
            process_read_block(data, data_len);
            break;
        case CMD_COMPLETE:
            process_complete();
            break;
        default:
            // Unknown command or gabage data, NACK it
            canboot_sendf(nack, 8);
    }
}

static inline void
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
                        canboot_sendf(nack, 8);
                    } else if (remaining >= full_length) {
                        remaining -= full_length;
                        uint16_t fpos = full_length - 4;
                        uint16_t trailer = tmpbuf[fpos + 2] << 8 | tmpbuf[fpos + 3];
                        if (trailer != CMD_TRAILER) {
                            canboot_sendf(nack, 8);
                        } else {
                            uint16_t crc = le16_to_cpu(*(uint16_t *)(&tmpbuf[fpos]));
                            uint16_t calc_crc = crc16_ccitt(&tmpbuf[2], full_length - 6);
                            if (crc != calc_crc) {
                                canboot_sendf(nack, 8);
                            } else {
                                // valid command, process
                                process_command(cmd, (uint32_t *)&tmpbuf[4], length);
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
canboot_process_rx(uint8_t *data, uint32_t len)
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

static inline uint8_t
check_application_code(void)
{
    // Read the first block of memory, if it
    // is all 0xFF then no application has been flashed
    flash_read_block(0, (uint32_t*)page_buffer);
    for (uint8_t i = 0; i < CONFIG_BLOCK_SIZE; i++) {
        if (page_buffer[i] != 0xFF)
            return 1;
    }
    return 0;
}

// Generated by buildcommands.py
DECL_CTR("DECL_BUTTON " __stringify(CONFIG_BUTTON_PIN));
extern int32_t button_gpio, button_high, button_pullup;

// Check for a bootloader request via double tap of reset button
static int
check_button_pressed(void)
{
    if (!CONFIG_ENABLE_BUTTON)
        return 0;
    struct gpio_in button = gpio_in_setup(button_gpio, button_pullup);
    udelay(10);
    return gpio_in_read(button) == button_high;
}

// Check for a bootloader request via double tap of reset button
static void
check_double_reset(void)
{
    if (!CONFIG_ENABLE_DOUBLE_RESET)
        return;
    // set request signature and delay for two seconds.  This enters the bootloader if
    // the reset button is double clicked
    set_bootup_code(REQUEST_SIG);
    udelay(500000);
    set_bootup_code(0);
    // No reset, read the key back out to clear it
}


/****************************************************************
 * Startup
 ****************************************************************/
static void
enter_bootloader(void)
{
    can_init();
    led_init();

    for (;;) {
        canbus_rx_task();
        canbus_tx_task();
        check_blink_time(blink_time);
        if (complete && canbus_tx_clear())
            // wait until we are complete and the ack has returned
            break;
    }

    // Flash Complete, system reset
    udelay(100000);
    canbus_reboot();
}

// Main loop of program
void
canboot_main(void)
{
    // Enter the bootloader in the following conditions:
    // - The request signature is set in memory (request from app)
    // - No application code is present
    uint64_t bootup_code = get_bootup_code();
    if (bootup_code == REQUEST_SIG || !check_application_code()
        || check_button_pressed()) {
        set_bootup_code(0);
        enter_bootloader();
    }
    check_double_reset();

    // jump to app
    jump_to_application();
}
