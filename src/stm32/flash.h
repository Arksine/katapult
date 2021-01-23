#ifndef __STM32_FLASH_H
#define __STM32_FLASH_H

#include <stdint.h>

void flash_complete(void);
void flash_write_page(uint16_t page_index, uint16_t *data);
void flash_read_block(uint16_t block_index, uint32_t *buffer);

#endif