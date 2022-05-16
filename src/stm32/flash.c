// Flash (IAP) functionality for STM32F1
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memset
#include "autoconf.h" // CONFIG_MACH_STM32F103
#include "board/io.h" // writew
#include "flash.h" // flash_write_block
#include "internal.h" // FLASH

#define STM32F4_MIN_SECTOR_SIZE 16384
#define STM32F4_MAX_SECTOR_SIZE 131072
#if CONFIG_MACH_STM32F4
#define FLASH_KEY1 (0x45670123UL)
#define FLASH_KEY2 (0xCDEF89ABUL)
#endif

static uint32_t
flash_get_page_size(void)
{
    if (CONFIG_MACH_STM32F103) {
        // Check for a 1K page size on the stm32f103
        uint16_t *flash_size = (void*)FLASHSIZE_BASE;
        if (*flash_size < 256)
            return 0x400;
    }
    return CONFIG_MAX_FLASH_PAGE_SIZE;
}

static void
wait_flash(void)
{
    while (FLASH->SR & FLASH_SR_BSY)
        ;
}

static void
unlock_flash(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        // Unlock Flash Erase
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    wait_flash();
}

static void
lock_flash(void)
{
    FLASH->CR = FLASH_CR_LOCK;
}

static void
flash_write_stm32f4xx(uint32_t page_address, uint32_t *data)
{
#if CONFIG_MACH_STM32F4
    uint32_t flash_page_size = flash_get_page_size();
    uint32_t* page = (uint32_t*)(page_address);

    uint8_t need_erase = 0;
    uint32_t offset_addr = page_address - CONFIG_FLASH_START;
    uint32_t sector_index = offset_addr / STM32F4_MAX_SECTOR_SIZE;
    if (sector_index < 1) {
        need_erase = ((page_address % STM32F4_MIN_SECTOR_SIZE) == 0);
        sector_index = offset_addr / STM32F4_MIN_SECTOR_SIZE;
        if (sector_index > 3) {
            need_erase &= (sector_index == 4);
            sector_index = 4;
        }
    } else {
        need_erase = ((page_address % STM32F4_MAX_SECTOR_SIZE) == 0);
        sector_index += 4;
    }

    // make sure flash is unlocked
    unlock_flash();

    // Erase page
    if (need_erase) {
        FLASH->CR = (FLASH_CR_PSIZE_1 | FLASH_CR_STRT | FLASH_CR_SER
                     | ((sector_index & 0xF) << 3));
        wait_flash();
    }

    // Write page
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    for (int i = 0; i < flash_page_size / 4; i++) {
        writel(&page[i], data[i]);
        wait_flash();
    }

    lock_flash();
#endif
}

static void
flash_write_stm32f1xx(uint32_t page_address, uint16_t *data)
{
#if CONFIG_MACH_STM32F0 || CONFIG_MACH_STM32F1
    uint32_t flash_page_size = flash_get_page_size();
    uint16_t* page = (uint16_t*)(page_address);

    // make sure flash is unlocked
    unlock_flash();

    // Erase page
    FLASH->CR = FLASH_CR_PER;
    FLASH->AR = page_address;
    FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
    wait_flash();

    // Write page
    FLASH->CR = FLASH_CR_PG;
    for (int i = 0; i < flash_page_size / 2; i++) {
        writew(&page[i], data[i]);
        wait_flash();
    }

    lock_flash();
#endif
}

static void
flash_write_page(uint32_t page_address, void *data)
{
    if (CONFIG_MACH_STM32F4) {
        flash_write_stm32f4xx(page_address, (uint32_t *)data);
    } else {
        flash_write_stm32f1xx(page_address, (uint16_t *)data);
    }
}

static uint8_t page_buffer[CONFIG_MAX_FLASH_PAGE_SIZE] __aligned(4);
// Page Tracking
static uint32_t last_page_address = 0;
static uint8_t page_pending = 0;

static void
write_page(uint32_t page_address)
{
    flash_write_page(page_address, page_buffer);
    memset(page_buffer, 0xFF, sizeof(page_buffer));
    last_page_address = page_address;
    page_pending = 0;
}

int
flash_write_block(uint32_t block_address, uint32_t *data)
{
    uint32_t flash_page_size = flash_get_page_size();
    uint32_t page_pos = block_address % flash_page_size;
    memcpy(&page_buffer[page_pos], (uint8_t *)&data[2], CONFIG_BLOCK_SIZE);
    page_pending = 1;
    if (page_pos + CONFIG_BLOCK_SIZE == flash_page_size)
        write_page(block_address - page_pos);
    return 0;
}

int
flash_complete(void)
{
    uint32_t flash_page_size = flash_get_page_size();
    if (page_pending) {
        write_page(last_page_address + flash_page_size);
    }
    return ((last_page_address - CONFIG_APPLICATION_START)
            / flash_page_size) + 1;
}
