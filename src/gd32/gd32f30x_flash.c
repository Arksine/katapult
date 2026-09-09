// Flash programming support for GD32F30x.
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.
//
// The GD32F303 FMC is similar to STM32F1, but this implementation deliberately
// uses the GD32 vendor register names so Katapult does not depend on STM32
// device headers.
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h>
#include "autoconf.h"
#include "board/io.h"
#include "flash.h"
#include "internal.h"

#if CONFIG_MACH_GD32F303XB
#define FLASH_PAGE_SIZE 1024U
#else
#define FLASH_PAGE_SIZE 2048U
#endif
#define FMC_ERROR_FLAGS (FMC_STAT0_PGERR | FMC_STAT0_WPERR)

static uint32_t page_write_count;

static int
wait_flash(void)
{
    while (FMC_STAT0 & FMC_STAT0_BUSY)
        ;
    uint32_t status = FMC_STAT0;
    // Status flags are cleared by writing one.
    FMC_STAT0 = FMC_ERROR_FLAGS | FMC_STAT0_ENDF;
    return status & FMC_ERROR_FLAGS ? -1 : 0;
}

static int
unlock_flash(void)
{
    if (wait_flash())
        return -1;
    if (FMC_CTL0 & FMC_CTL0_LK) {
        FMC_KEY0 = UNLOCK_KEY0;
        FMC_KEY0 = UNLOCK_KEY1;
    }
    return (FMC_CTL0 & FMC_CTL0_LK) ? -1 : 0;
}

static void
lock_flash(void)
{
    FMC_CTL0 |= FMC_CTL0_LK;
}

static int
check_erased(uint32_t address, uint32_t size)
{
    uint32_t *p = (void *)address;
    while (size) {
        if (*p++ != 0xffffffffU)
            return 0;
        size -= sizeof(*p);
    }
    return 1;
}

static int
erase_page(uint32_t address)
{
    FMC_CTL0 = FMC_CTL0_PER;
    FMC_ADDR0 = address;
    FMC_CTL0 = FMC_CTL0_PER | FMC_CTL0_START;
    int ret = wait_flash();
    FMC_CTL0 = 0;
    return ret;
}

static int
write_block(uint32_t address, uint32_t *data)
{
    uint16_t *dest = (void *)address;
    uint16_t *src = (void *)data;
    FMC_CTL0 = FMC_CTL0_PG;
    for (uint32_t i = 0; i < CONFIG_BLOCK_SIZE / 2; i++) {
        writew(&dest[i], src[i]);
        if (wait_flash()) {
            FMC_CTL0 = 0;
            return -1;
        }
    }
    FMC_CTL0 = 0;
    return 0;
}

int
flash_write_block(uint32_t address, uint32_t *data)
{
    if (address & (CONFIG_BLOCK_SIZE - 1))
        return -1;

    uint32_t page = address & ~(FLASH_PAGE_SIZE - 1);
    int need_erase = 0;
    if (page == address) {
        if (!check_erased(address, FLASH_PAGE_SIZE)) {
            if (memcmp(data, (void *)address, CONFIG_BLOCK_SIZE) == 0
                && check_erased(address + CONFIG_BLOCK_SIZE,
                                FLASH_PAGE_SIZE - CONFIG_BLOCK_SIZE))
                return 0;
            need_erase = 1;
        }
        page_write_count++;
    } else if (!check_erased(address, CONFIG_BLOCK_SIZE)) {
        if (memcmp(data, (void *)address, CONFIG_BLOCK_SIZE) == 0)
            return 0;
        return -2;
    }

    if (unlock_flash())
        return -3;
    int ret = need_erase ? erase_page(page) : 0;
    if (!ret)
        ret = write_block(address, data);
    lock_flash();
    if (ret || memcmp(data, (void *)address, CONFIG_BLOCK_SIZE) != 0)
        return -3;
    return 0;
}

int
flash_complete(void)
{
    return page_write_count;
}
