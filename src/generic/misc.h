#ifndef __GENERIC_MISC_H
#define __GENERIC_MISC_H

#include <stdarg.h> // va_list
#include <stdint.h> // uint8_t
#include "autoconf.h" // CONFIG_MACH_STM32F0

uint64_t get_bootup_code(void);
void set_bootup_code(uint64_t code);
void jump_to_application(void);

// Timer Functions
#if CONFIG_MACH_STM32F0
void timer_init(void);
#endif
uint32_t timer_from_us(uint32_t us);
uint8_t timer_is_before(uint32_t time1, uint32_t time2);
uint32_t timer_read_time(void);
void udelay(uint32_t usecs);

#endif // misc.h
