#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

#define SDCMD_GO_IDLE_STATE     0
#define SDCMD_ALL_SEND_CID      2
#define SDCMD_SEND_REL_ADDR     3
#define SDCMD_IO_SEND_OP_COND   5
#define SDCMD_SEL_DESEL_CARD    7
#define SDCMD_SEND_IF_COND      8
#define SDCMD_SEND_CSD          9
#define SDCMD_SEND_OP_COND      41
#define SDCMD_SET_CLR_CD_DETECT 42
#define SDCMD_SEND_STATUS       13
#define SDCMD_SET_BLOCKLEN      16
#define SDCMD_READ_SINGLE_BLOCK 17
#define SDCMD_WRITE_BLOCK       24
#define SDCMD_APP_CMD           55
#define SDCMD_READ_OCR          58
#define SDCMD_CRC_ON_OFF        59
#define SD_SECTOR_SIZE          512

uint8_t sdcard_write_sector(uint8_t* buf, uint32_t sector);
uint8_t sdcard_read_sector(uint8_t *buf, uint32_t sector);
uint8_t sdcard_init(void);
uint16_t sdcard_report_status(void);
void sdcard_deinit(void);

#endif
