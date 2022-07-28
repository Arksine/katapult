#ifndef __GENERIC_MISC_H
#define __GENERIC_MISC_H

#include <stdarg.h> // va_list
#include <stdint.h> // uint8_t

struct command_encoder;
void console_sendf(const struct command_encoder *ce, va_list args);
void *console_receive_buffer(void);

#define CANBOOT_SIGNATURE 0x21746f6f426e6143 // CanBoot!
#define REQUEST_CANBOOT 0x5984E3FA6CA1589B
#define REQUEST_START_APP 0x7b06ec45a9a8243d

uint64_t get_bootup_code(void);
void set_bootup_code(uint64_t code);
void application_read_flash(uint32_t address, uint32_t *dest);
int application_check_valid(void);
void application_jump(void);

void try_request_canboot(void);

void timer_setup(void);

uint32_t timer_from_us(uint32_t us);
uint8_t timer_is_before(uint32_t time1, uint32_t time2);
uint32_t timer_read_time(void);
void timer_kick(void);

void *dynmem_start(void);
void *dynmem_end(void);

uint16_t crc16_ccitt(uint8_t *buf, uint_fast8_t len);

#endif // misc.h
