// Flash (IAP) functionality for STM32F1
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "flash.h"
#include "autoconf.h"
#include "internal.h"

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
flash_write_page(uint16_t page_index, uint16_t *data)
{
    // A page_index of 0 is the first page of the application area
    uint16_t* page_addr = (uint16_t*)(CONFIG_APPLICATION_START +
        (page_index * CONFIG_FLASH_PAGE_SIZE));

    // make sure flash is unlocked
    if (FLASH->CR & FLASH_CR_LOCK)
        unlock_flash();

    // Erase page
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = (uint32_t)page_addr;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    do {
        FLASH->CR &= ~FLASH_CR_PER;
    } while (FLASH->CR & FLASH_CR_PER);

    // Write page
    FLASH->CR |= FLASH_CR_PG;
    for (uint16_t i = 0; i < CONFIG_FLASH_PAGE_SIZE / 2; i++)
    {
        page_addr[i] = data[i];
        while (FLASH->SR & FLASH_SR_BSY);
    }
    do {
        FLASH->CR &= ~FLASH_CR_PG;
    } while (FLASH->CR & FLASH_CR_PG);
    FLASH->CR |= FLASH_CR_LOCK;
}

void
flash_read_block(uint16_t block_index, uint32_t *buffer)
{
    // A page_index of 0 is the first page of the application area
    uint32_t* block_addr = (uint32_t*)(CONFIG_APPLICATION_START +
        (block_index * CONFIG_BLOCK_SIZE));

    for (uint8_t i = 0; i < CONFIG_BLOCK_SIZE / 4; i++)
        buffer[i] = block_addr[i];
}