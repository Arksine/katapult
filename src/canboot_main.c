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
#include "canboot_main.h"   // canboot_main


#define COMMAND_SIZE    8
#define CMD_BUF_SIZE    16
#define CMD_CONNECT     0x01
#define CMD_RX_BLOCK    0x02
#define CMD_RX_EOF      0x03
#define CMD_REQ_BLOCK   0x04
#define CMD_COMPLETE    0x05
#define ACK_COMMAND     0x10
#define ACK_BLOCK_RX    0x11
#define NACK            0x20

// Command Format:
// <4 byte header> <1 byte cmd> <2 byte arg> <1 byte trailer>
#define CMD_HEADER      0x6362746C  // "cbtl"
#define CMD_TRAILER     0x03        // ETX

#define WAIT_BLINK_TIME 1000000
#define XFER_BLINK_TIME 10000

static uint8_t page_buffer[CONFIG_FLASH_PAGE_SIZE];
static uint8_t cmd_buf[CMD_BUF_SIZE];
static uint8_t cmd_pos = 0;
static uint16_t page_count = 0;
static uint16_t page_pos = 0;
static uint32_t last_blink_time = 0;
static uint16_t cmd_arg = 0;
enum { CMD_PENDING, RX_BLOCK, RX_DONE, TX_BLOCK, COMPLETE };
static uint8_t current_state = CMD_PENDING;


static void
send_ack(uint8_t ack, uint16_t arg)
{
    uint8_t ack_buf[8];
    ack_buf[0] = (CMD_HEADER >> 24) & 0xFF;
    ack_buf[1] = (CMD_HEADER >> 16) & 0xFF;
    ack_buf[2] = (CMD_HEADER >> 8) & 0xFF;
    ack_buf[3] = CMD_HEADER & 0xFF;
    ack_buf[4] = ack;
    ack_buf[5] = (arg >> 8) & 0xFF;
    ack_buf[6] = arg & 0xFF;
    ack_buf[7] = CMD_TRAILER;
    canboot_sendf(ack_buf, COMMAND_SIZE);
}

static inline void
process_command(void)
{
    if (cmd_pos < COMMAND_SIZE)
        return;

    uint8_t cmd = 0xFF;
    uint8_t remaining = cmd_pos;
    uint8_t *tmpbuf = cmd_buf;
    uint32_t header;
    while (remaining) {
        if (tmpbuf[0] == 0x63) {
            // potential match
            if (remaining >= COMMAND_SIZE) {
                header = (tmpbuf[0] << 24) | (tmpbuf[1] << 16) | (tmpbuf[2] << 8)
                    | (tmpbuf[3]);
                if (header == CMD_HEADER && cmd_buf[7] == CMD_TRAILER) {
                    cmd = tmpbuf[4];
                    cmd_arg = (tmpbuf[5] << 8) | tmpbuf[6];
                    remaining -= COMMAND_SIZE;
                    break;
                }
            } else
                // A potential command, check it after the next read
                break;
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
    switch (cmd) {
        case CMD_CONNECT:
            send_ack(ACK_COMMAND, CONFIG_BLOCK_SIZE);
            // TODO: reinit page?
            break;
        case CMD_RX_BLOCK:
            send_ack(ACK_COMMAND, cmd_arg);
            current_state = RX_BLOCK;
            break;
        case CMD_RX_EOF:
            // This will be ACK'ed after the final
            // page is written
            current_state = RX_DONE;
            break;
        case CMD_REQ_BLOCK:
            send_ack(ACK_COMMAND, cmd_arg);
            current_state = TX_BLOCK;
            break;
        case CMD_COMPLETE:
            send_ack(ACK_COMMAND, 1);
            current_state = COMPLETE;
            break;
        default:
            // Unknown command or gabage data, NACK it
            send_ack(NACK, 0);
    }
}

static void
write_page(uint16_t page)
{
    flash_write_page(page, (uint16_t*)page_buffer);
    memset(page_buffer, 0xFF, CONFIG_FLASH_PAGE_SIZE);
    page_pos = 0;
}

static void
process_page(void) {
    static uint16_t last_page_pos = 0;
    uint8_t need_ack = 0;
    if (page_pos == last_page_pos) {
        return;
    }
    if (page_pos == CONFIG_FLASH_PAGE_SIZE)
        write_page(page_count++);
    if (page_pos % CONFIG_BLOCK_SIZE == 0) {
        current_state = CMD_PENDING;
        need_ack = 1;
    }
    last_page_pos = page_pos;
    if (need_ack)
        send_ack(ACK_BLOCK_RX, cmd_arg);
}

static void
check_blink_time(uint32_t usec)
{
    uint32_t curtime = timer_read_time();
    uint32_t endtime = last_blink_time + timer_from_us(usec);
    if (timer_is_before(endtime, curtime)) {
        led_toggle();
        last_blink_time = timer_read_time();
    }
}

static inline void
process_state(void)
{
    switch (current_state) {
        case CMD_PENDING:
            check_blink_time(WAIT_BLINK_TIME);
            process_command();
            break;
        case RX_BLOCK:
            check_blink_time(XFER_BLINK_TIME);
            process_page();
            break;
        case RX_DONE:
            if (page_pos)
                write_page(page_count++);
            flash_complete();
            current_state = CMD_PENDING;
            send_ack(ACK_COMMAND, page_count);
            break;
        case TX_BLOCK:
            // TODO: rather than tx the block, we can do the
            // CRC check here?  CRC16 or SHA1?
            check_blink_time(XFER_BLINK_TIME);
            flash_read_block(cmd_arg, (uint32_t*)page_buffer);
            canboot_sendf(page_buffer, CONFIG_BLOCK_SIZE);
            current_state = CMD_PENDING;
            break;
    }
}

void
canboot_process_rx(uint32_t id, uint32_t len, uint8_t *data)
{
    switch (current_state) {
        case CMD_PENDING:
            // read into the command buffer
            if (cmd_pos >= CMD_BUF_SIZE)
                return;
            else if (cmd_pos + len > CMD_BUF_SIZE)
                len = CMD_BUF_SIZE - cmd_pos;
            memcpy(&cmd_buf[cmd_pos], data, len);
            cmd_pos += len;
            break;
        case RX_BLOCK:
            // read into into the page buffer
            if (page_pos >= CONFIG_FLASH_PAGE_SIZE)
                return;
            else if (page_pos > CONFIG_FLASH_PAGE_SIZE)
                len = CONFIG_FLASH_PAGE_SIZE - cmd_pos;
            memcpy(&page_buffer[page_pos], data, len);
            page_pos += len;
            break;
        default:
            return;
    }
}


/****************************************************************
 * Startup
 ****************************************************************/
static void
enter_bootloader(void)
{
    can_init();

    // TODO: this is temporary.  It lets us know
    // that the bootloader has been entered.  We can
    // also toggle this as a means to visualize transfers.
    // We will want to set it up in the menuconfig
    led_init();
    // The short delay is simply to ensure that the Debug Timer is
    // enabled
    udelay(10);
    last_blink_time = timer_read_time();

    for (;;) {
        canbus_rx_task();
        canbus_tx_task();
        process_state();
        if (current_state == COMPLETE && canbus_tx_clear())
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
    uint16_t mkey = read_magic_key();
    // Attempt to enter the bootloader.  This function should not
    // return
    if (mkey == CONFIG_MAGIC_KEY)
        enter_bootloader();

    // set magic key and delay for one second.  This enters the bootloader if
    // the reset button is double clicked
    set_magic_key();
    udelay(1500000);
    // No reset, read the key back out to clear it
    read_magic_key();

    // jump to app
    jump_to_application();
}
