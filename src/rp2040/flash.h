// Flash (IAP) functionality for RP2040
// This file may be distributed under the terms of the GNU GPLv3 license.
#ifndef __RP2040_FLASH_H
#define __RP2040_FLASH_H

#include <stdint.h>

int flash_write_block(uint32_t block_address, uint32_t *data);
int flash_complete(void);

#endif
