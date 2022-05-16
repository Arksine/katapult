// Flash (IAP) functionality for STM32
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memset
#include "autoconf.h" // CONFIG_MACH_STM32F103
#include "board/io.h" // writew
#include "flash.h" // flash_write_block
#include "internal.h" // FLASH

#if CONFIG_MACH_STM32F4
#define FLASH_KEY1 (0x45670123UL)
#define FLASH_KEY2 (0xCDEF89ABUL)

// Return the flash sector index for the page at the given address
static uint32_t
stm32f4_sector_index(uint32_t addr)
{
    if (addr < 0x08010000)
        return (addr - 0x08000000) / (16 * 1024);
    else if (addr < 0x08020000)
        return 4;
    else
        return 5 + (addr - 0x08020000) / (128 * 1024);
}
#endif

// Return the flash page size at the given address
static uint32_t
flash_get_page_size(uint32_t addr)
{
    if (CONFIG_MACH_STM32F4) {
        if (addr < 0x08010000)
            return 16 * 1024;
        else if (addr < 0x08020000)
            return 64 * 1024;
        else
            return 128 * 1024;
    } else if (CONFIG_MACH_STM32F042) {
        return 1024;
    } else if (CONFIG_MACH_STM32F103) {
        // Check for a 1K page size on the stm32f103
        uint16_t *flash_size = (void*)FLASHSIZE_BASE;
        if (*flash_size < 256)
            return 1024;
    }
    return 2 * 1024;
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

// Wait for flash hardware to report ready
static void
wait_flash(void)
{
    while (FLASH->SR & FLASH_SR_BSY)
        ;
}

// Issue low-level flash hardware unlock sequence
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

// Place low-level flash hardware into a locked state
static void
lock_flash(void)
{
    FLASH->CR = FLASH_CR_LOCK;
}

// Issue a low-level flash hardware erase request for a flash page
static void
erase_page(uint32_t page_address)
{
#if CONFIG_MACH_STM32F4
    FLASH->CR = (FLASH_CR_PSIZE_1 | FLASH_CR_STRT | FLASH_CR_SER
                 | ((stm32f4_sector_index(page_address) & 0xF) << 3));
#else
    FLASH->CR = FLASH_CR_PER;
    FLASH->AR = page_address;
    FLASH->CR = FLASH_CR_PER | FLASH_CR_STRT;
#endif
    wait_flash();
}

// Write out a "block" of data to the low-level flash hardware
static void
write_block(uint32_t block_address, uint32_t *data)
{
#if CONFIG_MACH_STM32F4
    uint32_t *page = (void*)block_address;
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    for (int i = 0; i < CONFIG_BLOCK_SIZE / 4; i++) {
        writel(&page[i], data[i]);
        wait_flash();
    }
#else
    uint16_t *page = (void*)block_address, *data16 = (void*)data;
    FLASH->CR = FLASH_CR_PG;
    for (int i = 0; i < CONFIG_BLOCK_SIZE / 2; i++) {
        writew(&page[i], data16[i]);
        wait_flash();
    }
#endif
}

static uint32_t write_count;

// Main block write interface
int
flash_write_block(uint32_t block_address, uint32_t *data)
{
    if (block_address & (CONFIG_BLOCK_SIZE - 1))
        // Not a block aligned address
        return -1;
    uint32_t flash_page_size = flash_get_page_size(block_address);
    uint32_t page_address = ALIGN_DOWN(block_address, flash_page_size);

    // Check if erase is needed
    int need_erase = 0;
    if (page_address == block_address) {
        if (check_erased(block_address, flash_page_size)) {
            // Page already erased
        } else if (memcmp(data, (void*)block_address, CONFIG_BLOCK_SIZE) == 0
                   && check_erased(block_address + CONFIG_BLOCK_SIZE
                                   , flash_page_size - CONFIG_BLOCK_SIZE)) {
            // Retransmitted request - just ignore
            return 0;
        } else {
            need_erase = 1;
        }
    } else {
        if (!check_erased(block_address, CONFIG_BLOCK_SIZE)) {
            if (memcmp(data, (void*)block_address, CONFIG_BLOCK_SIZE) == 0)
                // Retransmitted request - just ignore
                return 0;
            // Block not erased - out of order request?
            return -2;
        }
    }

    // make sure flash is unlocked
    unlock_flash();

    // Erase page
    if (need_erase)
        erase_page(page_address);

    // Write block
    write_block(block_address, data);

    lock_flash();

    if (memcmp(data, (void*)block_address, CONFIG_BLOCK_SIZE) != 0)
        // Failed to write to flash?!
        return -3;

    write_count++;
    return 0;
}

// Main flash complete notification interface
int
flash_complete(void)
{
    return write_count;
}
