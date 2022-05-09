#ifndef __CANBOOT_MAIN_H
#define __CANBOOT_MAIN_H

#include <stdint.h> // uint32_t

void canboot_process_rx(uint8_t *data, uint32_t len);
void canboot_sendf(uint8_t* data, uint16_t size);
void canboot_main(void);
void send_ack(uint32_t* data, uint8_t payload_len);

#define CMD_GET_CANBUS_ID 0x16
void command_get_canbus_id(void);

#endif // canboot_main.h
