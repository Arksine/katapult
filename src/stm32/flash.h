#ifndef __STM32_FLASH_H
#define __STM32_FLASH_H

#include <stdint.h>

uint32_t flash_get_page_size(void);
void flash_complete(void);
void flash_write_page(uint32_t page_address, void *data);
void flash_read_block(uint32_t block_address, uint32_t *buffer);

#endif
