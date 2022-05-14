// Flash (IAP) functionality for STM32F1
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "autoconf.h" // CONFIG_MACH_STM32F103
#include "flash.h" // flash_write_page
#include "internal.h" // FLASH

#define STM32F4_MIN_SECTOR_SIZE 16384
#define STM32F4_MAX_SECTOR_SIZE 131072
#if CONFIG_MACH_STM32F4
#define FLASH_KEY1 (0x45670123UL)
#define FLASH_KEY2 (0xCDEF89ABUL)
#endif

uint32_t
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
unlock_flash(void)
{
    // Unlock Flash Erase
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    while (FLASH->SR & FLASH_SR_BSY);
}

void
flash_complete(void)
{
    // Lock flash when done
    FLASH->CR |= FLASH_CR_LOCK;
}

void
flash_write_stm32f4xx(uint32_t page_address, uint32_t *data)
{
#if CONFIG_MACH_STM32F4
    uint32_t flash_page_size = flash_get_page_size();
    uint32_t* page = (uint32_t*)(page_address);

    if (FLASH->CR & FLASH_CR_LOCK)
        unlock_flash();

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
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR &= ~FLASH_CR_SNB;
    if (need_erase) {
        FLASH->CR |= FLASH_CR_PSIZE_1;
        FLASH->CR |= FLASH_CR_SER | ((sector_index & 0xF) << 3);
        FLASH->CR |= FLASH_CR_STRT;
        while (FLASH->SR & FLASH_SR_BSY);
        FLASH->CR &= ~FLASH_CR_SNB;
        do {
            FLASH->CR &= ~FLASH_CR_SER;
        } while(FLASH->CR & FLASH_CR_SER);
    }

    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;
    for (uint16_t i = 0; i < flash_page_size / 4; i++)
    {
        page[i] = data[i];
        while (FLASH->SR & FLASH_SR_BSY);
    }
    do {
        FLASH->CR &= ~FLASH_CR_PG;
    } while(FLASH->CR & FLASH_CR_PG);

    FLASH->CR |= FLASH_CR_LOCK;
#endif
}

void
flash_write_stm32f1xx(uint32_t page_address, uint16_t *data)
{
#if CONFIG_MACH_STM32F0 || CONFIG_MACH_STM32F1
    uint32_t flash_page_size = flash_get_page_size();
    uint16_t* page = (uint16_t*)(page_address);

    // make sure flash is unlocked
    if (FLASH->CR & FLASH_CR_LOCK)
        unlock_flash();

    // Erase page
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = (uint32_t)page;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    do {
        FLASH->CR &= ~FLASH_CR_PER;
    } while (FLASH->CR & FLASH_CR_PER);

    // Write page
    FLASH->CR |= FLASH_CR_PG;
    for (uint16_t i = 0; i < flash_page_size / 2; i++)
    {
        page[i] = data[i];
        while (FLASH->SR & FLASH_SR_BSY);
    }
    do {
        FLASH->CR &= ~FLASH_CR_PG;
    } while (FLASH->CR & FLASH_CR_PG);

    FLASH->CR |= FLASH_CR_LOCK;
#endif
}

void
flash_write_page(uint32_t page_address, void *data)
{
    if (CONFIG_MACH_STM32F4) {
        flash_write_stm32f4xx(page_address, (uint32_t *)data);
    } else {
        flash_write_stm32f1xx(page_address, (uint16_t *)data);
    }
}

void
flash_read_block(uint32_t block_address, uint32_t *buffer)
{
    memcpy(buffer, (void*)block_address, CONFIG_BLOCK_SIZE);
}
