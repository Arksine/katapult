// Command handlers for flash requests
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memmove
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "board/flash.h" // flash_write_page
#include "byteorder.h" // cpu_to_le32
#include "command.h" // command_respond_ack
#include "flashcmd.h" // flashcmd_is_in_transfer

static uint8_t page_buffer[CONFIG_MAX_FLASH_PAGE_SIZE];
// Page Tracking
static uint32_t last_page_address = 0;
static uint8_t page_pending = 0;
static uint8_t is_in_transfer;

int
flashcmd_is_in_transfer(void)
{
    return is_in_transfer;
}

static void
write_page(uint32_t page_address)
{
    flash_write_page(page_address, (uint16_t*)page_buffer);
    memset(page_buffer, 0xFF, sizeof(page_buffer));
    last_page_address = page_address;
    page_pending = 0;
}

void
command_read_block(uint32_t *data)
{
    is_in_transfer = 1;
    uint32_t block_address = le32_to_cpu(data[1]);
    uint32_t out[CONFIG_BLOCK_SIZE / 4 + 2 + 2];
    out[2] = cpu_to_le32(block_address);
    flash_read_block(block_address, &out[3]);
    command_respond_ack(CMD_REQ_BLOCK, out, ARRAY_SIZE(out));
}

void
command_write_block(uint32_t *data)
{
    is_in_transfer = 1;
    if (command_get_arg_count(data) != (CONFIG_BLOCK_SIZE / 4) + 1) {
        command_respond_command_error();
        return;
    }
    uint32_t block_address = le32_to_cpu(data[1]);
    if (block_address < CONFIG_APPLICATION_START) {
        command_respond_command_error();
        return;
    }
    uint32_t flash_page_size = flash_get_page_size();
    uint32_t page_pos = block_address % flash_page_size;
    memcpy(&page_buffer[page_pos], (uint8_t *)&data[2], CONFIG_BLOCK_SIZE);
    page_pending = 1;
    if (page_pos + CONFIG_BLOCK_SIZE == flash_page_size)
        write_page(block_address - page_pos);
    uint32_t out[4];
    out[2] = cpu_to_le32(block_address);
    command_respond_ack(CMD_RX_BLOCK, out, ARRAY_SIZE(out));
}

void
command_eof(uint32_t *data)
{
    is_in_transfer = 0;
    uint32_t flash_page_size = flash_get_page_size();
    if (page_pending) {
        write_page(last_page_address + flash_page_size);
    }
    flash_complete();
    uint32_t out[4];
    out[2] = cpu_to_le32(
        ((last_page_address - CONFIG_APPLICATION_START)
        / flash_page_size) + 1);
    command_respond_ack(CMD_RX_EOF, out, ARRAY_SIZE(out));
}
