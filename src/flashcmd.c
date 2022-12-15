// Command handlers for flash requests
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memmove
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "board/flash.h" // flash_write_block
#include "board/misc.h" // application_jump
#include "byteorder.h" // cpu_to_le32
#include "canboot.h" // application_jump
#include "command.h" // command_respond_ack
#include "flashcmd.h" // flashcmd_is_in_transfer
#include "sched.h" // DECL_TASK

// Handler for "connect" commands
void
command_connect(uint32_t *data)
{
    uint32_t mcuwords = DIV_ROUND_UP(strlen(CONFIG_MCU), 4);
    uint32_t out[6 + mcuwords];
    memset(out, 0, (6 + mcuwords) * 4);
    out[2] = cpu_to_le32(PROTO_VERSION);
    out[3] = cpu_to_le32(CONFIG_LAUNCH_APP_ADDRESS);
    out[4] = cpu_to_le32(CONFIG_BLOCK_SIZE);
    memcpy(&out[5], CONFIG_MCU, strlen(CONFIG_MCU));
    command_respond_ack(CMD_CONNECT, out, ARRAY_SIZE(out));
}


/****************************************************************
 * Command "complete" handling
 ****************************************************************/

static uint8_t complete;
static uint32_t complete_endtime;

void
command_complete(uint32_t *data)
{
    uint32_t out[3];
    command_respond_ack(CMD_COMPLETE, out, ARRAY_SIZE(out));
    complete = 1;
    complete_endtime = timer_read_time() + timer_from_us(100000);
}

void
complete_task(void)
{
    if (complete && timer_is_before(complete_endtime, timer_read_time()))
        application_jump();
}
DECL_TASK(complete_task);


/****************************************************************
 * Flash commands
 ****************************************************************/

static uint8_t is_in_transfer;

int
flashcmd_is_in_transfer(void)
{
    return is_in_transfer;
}

void
command_read_block(uint32_t *data)
{
    is_in_transfer = 1;
    uint32_t block_address = le32_to_cpu(data[1]);
    uint32_t out[CONFIG_BLOCK_SIZE / 4 + 2 + 2];
    out[2] = cpu_to_le32(block_address);
    application_read_flash(block_address, &out[3]);
    command_respond_ack(CMD_REQ_BLOCK, out, ARRAY_SIZE(out));
}

void
command_write_block(uint32_t *data)
{
    is_in_transfer = 1;
    if (command_get_arg_count(data) != (CONFIG_BLOCK_SIZE / 4) + 1)
        goto fail;
    uint32_t block_address = le32_to_cpu(data[1]);
    if (block_address < CONFIG_LAUNCH_APP_ADDRESS)
        goto fail;
    int ret = flash_write_block(block_address, &data[2]);
    if (ret < 0)
        goto fail;
    uint32_t out[4];
    out[2] = cpu_to_le32(block_address);
    command_respond_ack(CMD_RX_BLOCK, out, ARRAY_SIZE(out));
    return;
fail:
    command_respond_command_error();
}

void
command_eof(uint32_t *data)
{
    is_in_transfer = 0;
    int ret = flash_complete();
    if (ret < 0) {
        command_respond_command_error();
        return;
    }
    uint32_t out[4];
    out[2] = cpu_to_le32(ret);
    command_respond_ack(CMD_RX_EOF, out, ARRAY_SIZE(out));
}
