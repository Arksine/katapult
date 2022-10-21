// Flash (IAP) functionality for RP2040
// This file may be distributed under the terms of the GNU GPLv3 license.
#include "flash.h"

#include <string.h> // memcpy
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "generic/irq.h"
#include "hw_flash.h" // flash_write_page

#define MAX(a, b) ((a) > (b))?(a):(b)
#define PAGE_SIZE (MAX(CONFIG_BLOCK_SIZE, 256))
#define SECTOR_SIZE 4096

// buffer to consolidate multiple block_size requests into one page size write
static uint32_t buffer_not_empty; // true if buffer hold some data
static uint32_t buffer_start_address; // buffer data should be written at this flash address
static uint8_t buffer[PAGE_SIZE] __aligned(4); // buffer data itseft

static uint32_t page_write_count;

static void
flush_buffer(void)
{
    if (!buffer_not_empty) {
       return;
    }
    if ((buffer_start_address % SECTOR_SIZE) == 0) {
        flash_range_erase(buffer_start_address - CONFIG_FLASH_START, SECTOR_SIZE);
    }
    flash_range_program(buffer_start_address - CONFIG_FLASH_START, buffer, PAGE_SIZE);
    page_write_count += 1;
    buffer_not_empty = 0;
}

static void
ensure_buffer(uint32_t address)
{
    if (buffer_not_empty) {
        if ((address >= buffer_start_address) &&
            (address + CONFIG_BLOCK_SIZE <= buffer_start_address + PAGE_SIZE)) {
            // current buffer have space for the new data
            return;
        } else {
           // flush existing data
           flush_buffer();
        }
    }
    //prepare buffer
    buffer_not_empty = 1;
    // address should be multiple of PAGE_SIZE
    buffer_start_address = (address / PAGE_SIZE) * PAGE_SIZE;
    memset(buffer, 0xFF, PAGE_SIZE);
}

static int
check_valid_flash_address(uint32_t address)
{
    if ((address % CONFIG_BLOCK_SIZE) != 0) {
        return -1;
    }
    if (address < CONFIG_FLASH_START) {
       return -2;
    }
    if (address + CONFIG_BLOCK_SIZE > CONFIG_FLASH_START + CONFIG_FLASH_SIZE) {
       return -3;
    }
    return 0;
}

int
flash_write_block(uint32_t block_address, uint32_t *data)
{
    int ret = check_valid_flash_address(block_address);
    if (ret < 0) {
       return ret;
    }
    ensure_buffer(block_address);
    memcpy(&buffer[block_address - buffer_start_address], data, CONFIG_BLOCK_SIZE);
    return 0;
}

int
flash_complete(void)
{
    flush_buffer();
    return page_write_count;
}
