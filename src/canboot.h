#ifndef __CANBOOT_H
#define __CANBOOT_H

#include <stdint.h> // uint32_t

#define CANBOOT_SIGNATURE 0x21746f6f426e6143 // CanBoot!
#define REQUEST_CANBOOT 0x5984E3FA6CA1589B
#define REQUEST_START_APP 0x7b06ec45a9a8243d

uint64_t get_bootup_code(void);
void set_bootup_code(uint64_t code);
void application_read_flash(uint32_t address, uint32_t *dest);
int application_check_valid(void);
void application_jump(void);

void udelay(uint32_t usecs);
void timer_setup(void);

#endif // canboot.h
