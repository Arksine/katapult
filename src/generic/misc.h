#ifndef __GENERIC_MISC_H
#define __GENERIC_MISC_H

#include <stdarg.h> // va_list
#include <stdint.h> // uint8_t

struct command_encoder;
void console_sendf(const struct command_encoder *ce, va_list args);
void *console_receive_buffer(void);

uint64_t get_bootup_code(void);
void set_bootup_code(uint64_t code);
void jump_to_application(void);

void timer_init(void);
void udelay(uint32_t usecs);

uint32_t timer_from_us(uint32_t us);
uint8_t timer_is_before(uint32_t time1, uint32_t time2);
uint32_t timer_read_time(void);
void timer_kick(void);

void *dynmem_start(void);
void *dynmem_end(void);

uint16_t crc16_ccitt(uint8_t *buf, uint_fast8_t len);

#endif // misc.h
