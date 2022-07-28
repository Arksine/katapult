// Flash (IAP) functionality for LPC176x
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "generic/irq.h" // irq_disable
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "flash.h" // flash_write_page
#include "compiler.h" // ALIGN_DOWN

#define IAP_LOCATION        0x1fff1ff1
#define IAP_CMD_PREPARE     50
#define IAP_CMD_WRITE       51
#define IAP_CMD_ERASE       52
#define IAP_FREQ            (CONFIG_CLOCK_FREQ / 1000)
#define IAP_BUF_MIN_SIZE        256
typedef void (*IAP)(uint32_t *, uint32_t *);

static uint8_t iap_buf[IAP_BUF_MIN_SIZE] __aligned(4);
static uint32_t next_address;
static uint32_t page_write_count;

// Return the flash sector index for the page at the given address
static uint32_t
flash_get_sector_index(uint32_t addr)
{
    if (addr < 0x00010000)
        return addr / (4 * 1024);
    else
        return 16 + (addr - 0x00010000) / (32 * 1024);
}


// Return the flash page size at the given address
static uint32_t
flash_get_sector_size(uint32_t addr)
{
    if (addr < 0x00010000)
        return 4 * 1024;
    else
        return 32 * 1024;
}

// Check if the data at the given address has been erased (all 0xff)
static int
check_erased(uint32_t addr, uint32_t count)
{
    uint32_t *p = (void*)addr, *e = (void*)addr + count / 4;
    while (p < e)
        if (*p++ != 0xffffffff)
            return 0;
    return 1;
}

static int
call_iap(uint32_t* command)
{
    uint32_t iap_resp[5];
    IAP iap_entry = (IAP)IAP_LOCATION;
    irq_disable();
    iap_entry(command, iap_resp);
    irq_enable();
    return iap_resp[0];
}

static int
unlock_flash(uint32_t sector)
{
    uint32_t iap_cmd[5] = {IAP_CMD_PREPARE, sector, sector, IAP_FREQ, 0};
    return call_iap(iap_cmd);
}

static int
erase_sector(uint32_t sector)
{
    uint32_t iap_cmd[5] = {IAP_CMD_ERASE, sector, sector, IAP_FREQ, 0};
    return call_iap(iap_cmd);
}

static int
write_flash(uint32_t flash_address, uint32_t* data, uint32_t len)
{
    uint32_t iap_cmd[5] = {
        IAP_CMD_WRITE, flash_address, (uint32_t)data, len, IAP_FREQ
    };
    return call_iap(iap_cmd);
}

static int
write_buffer(uint32_t flash_address, uint32_t* data, uint32_t len)
{
    uint32_t flash_sector_size = flash_get_sector_size(flash_address);
    uint32_t sector = flash_get_sector_index(flash_address);
    uint32_t page_address = ALIGN_DOWN(flash_address, flash_sector_size);
    if (page_address == flash_address) {
        if (check_erased(flash_address, flash_sector_size)){
            // sector already erased
        }
        else if (memcmp(data, (void*)flash_address, len) == 0 &&
                 check_erased(flash_address + len, flash_sector_size - len))
        {
            // retransmit of this block
            return 0;
        }
        else {
            // sector needs to be erased
            unlock_flash(sector);
            if (erase_sector(sector) != 0)
                return -3;
        }
        page_write_count += 1;
    } else {
        if (!check_erased(flash_address, len)) {
            if (memcmp(data, (void*)flash_address, len) == 0)
                return 0;
            return -2;
        }
    }
    unlock_flash(sector);
    if (write_flash(flash_address, data, len) != 0)
        return -4;
    return 0;
}

int
flash_write_block(uint32_t block_address, uint32_t *data)
{
    if (block_address & (CONFIG_BLOCK_SIZE - 1))
        // Not a block aligned address
        return -1;
    if (CONFIG_BLOCK_SIZE < IAP_BUF_MIN_SIZE) {
        if (block_address != next_address) {
            if ((block_address | next_address) & (IAP_BUF_MIN_SIZE - 1))
                // out of order request
                return -2;
            next_address = block_address;
        }
        uint32_t buf_idx = block_address & (IAP_BUF_MIN_SIZE - 1);
        memcpy(&iap_buf[buf_idx], data, CONFIG_BLOCK_SIZE);
        if (buf_idx == IAP_BUF_MIN_SIZE - CONFIG_BLOCK_SIZE) {
            int ret = write_buffer(
                block_address - buf_idx, (uint32_t*)iap_buf, IAP_BUF_MIN_SIZE
            );
            if (ret < 0)
                return ret;
        }
        next_address += CONFIG_BLOCK_SIZE;
    } else
        return write_buffer(block_address, data, CONFIG_BLOCK_SIZE);
    return 0;
}

int
flash_complete(void)
{
    if (CONFIG_BLOCK_SIZE < IAP_BUF_MIN_SIZE) {
        uint32_t buf_idx = next_address & (IAP_BUF_MIN_SIZE - 1);
        if (buf_idx) {
            memset(&iap_buf[buf_idx], 0xFF, (IAP_BUF_MIN_SIZE - buf_idx));
            int ret = write_buffer(
                next_address - buf_idx, (uint32_t*)iap_buf, IAP_BUF_MIN_SIZE
            );
            if (ret < 0)
                return ret;
        }
    }
    return page_write_count;
}
