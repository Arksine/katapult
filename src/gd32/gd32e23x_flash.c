// Flash programming support for GD32E23x
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h>
#include "autoconf.h"
#include "board/io.h"
#include "gd32e23x_flash.h"
#include "gd32e23x_internal.h"

#define FLASH_PAGE_SIZE 1024U

static void wait_flash(void)
{
    while (FMC_STAT & FMC_STAT_BUSY)
        ;
    FMC_STAT = FMC_STAT_PGERR | FMC_STAT_PGAERR | FMC_STAT_WPERR | FMC_STAT_ENDF;
}

static void unlock_flash(void)
{
    wait_flash();
    if (FMC_CTL & FMC_CTL_LK) {
        FMC_KEY = UNLOCK_KEY0;
        FMC_KEY = UNLOCK_KEY1;
    }
}

static void lock_flash(void) { FMC_CTL |= FMC_CTL_LK; }

static int check_erased(uint32_t address, uint32_t size)
{
    uint32_t *p = (void*)address;
    while (size) {
        if (*p++ != 0xffffffffU)
            return 0;
        size -= 4;
    }
    return 1;
}

static void erase_page(uint32_t address)
{
    wait_flash();
    FMC_CTL = FMC_CTL_PER;
    FMC_ADDR = address;
    FMC_CTL = FMC_CTL_PER | FMC_CTL_START;
    wait_flash();
    FMC_CTL = 0;
}

static void write_block(uint32_t address, uint32_t *data)
{
    FMC_WS |= FMC_WS_PGW;
    FMC_CTL = FMC_CTL_PG;
    uint32_t *dest = (void*)address;
    for (uint32_t i = 0; i < CONFIG_BLOCK_SIZE / 4; i++) {
        writel(&dest[i], data[i]);
        wait_flash();
    }
    FMC_CTL = 0;
    FMC_WS &= ~FMC_WS_PGW;
}

static uint32_t page_write_count;

int flash_write_block(uint32_t address, uint32_t *data)
{
    if (address & (CONFIG_BLOCK_SIZE - 1))
        return -1;
    uint32_t page = address & ~(FLASH_PAGE_SIZE - 1);
    int erase = 0;
    if (page == address) {
        if (!check_erased(address, FLASH_PAGE_SIZE)) {
            if (memcmp(data, (void*)address, CONFIG_BLOCK_SIZE) == 0
                && check_erased(address + CONFIG_BLOCK_SIZE,
                                FLASH_PAGE_SIZE - CONFIG_BLOCK_SIZE))
                return 0;
            erase = 1;
        }
        page_write_count++;
    } else if (!check_erased(address, CONFIG_BLOCK_SIZE)) {
        if (memcmp(data, (void*)address, CONFIG_BLOCK_SIZE) == 0)
            return 0;
        return -2;
    }
    unlock_flash();
    if (erase)
        erase_page(page);
    write_block(address, data);
    lock_flash();
    return memcmp(data, (void*)address, CONFIG_BLOCK_SIZE) ? -3 : 0;
}

int flash_complete(void) { return page_write_count; }
