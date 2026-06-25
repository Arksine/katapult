#ifndef __STM32_FLASH_H
#define __STM32_FLASH_H

#include <stdint.h>

int flash_write_block(uint32_t block_address, uint32_t *data);
int flash_complete(void);

/* Flash complete flag — 2KB page at 0x0801D800 (between app and config storage).
 * Present (magic written) = last flash completed successfully.
 * Absent (erased, 0xFF)   = flash incomplete or never run; bootloader stays indefinitely.
 */
void flash_flag_erase(void);
void flash_flag_write(void);
int flash_flag_check(void);

#endif
