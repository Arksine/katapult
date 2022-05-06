#ifndef __CANBOOT_MAIN_H
#define __CANBOOT_MAIN_H

#include <stdint.h> // uint32_t

void canboot_process_rx(uint8_t *data, uint32_t len);
void canboot_sendf(uint8_t* data, uint16_t size);
void canboot_main(void);

#endif // canboot_main.h
