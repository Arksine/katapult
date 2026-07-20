#ifndef __STM32_FLASH_H
#define __STM32_FLASH_H

#include <stdint.h>

int flash_write_block(uint32_t block_address, uint32_t *data);
int flash_complete(void);

/* Flash complete flag — reserved page immediately below the application
 * start (CONFIG_STM32_RESERVE_FLASH_FLAG, see armcm_link.lds.S).
 * Present (magic written) = last flash completed successfully.
 * Absent (erased, 0xFF)   = flash incomplete or never run; bootloader stays indefinitely.
 */
void flash_flag_erase(void);
void flash_flag_write(void);
int flash_flag_check(void);

#endif
