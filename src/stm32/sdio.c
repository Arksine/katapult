// SDIO Support for STM32 microcontrollers
//
// Copyright (C) 2022 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "autoconf.h" // CONFIG_MACH_STM32F1
#include "board/io.h" // barrier
#include "board/armcm_timer.h" // udelay
#include "command.h" // DECL_CONSTANT_STR
#include "sdcard.h" // sdcard_init
#include "internal.h" // enable_pclock
#include "sched.h"    // udelay

DECL_CONSTANT_STR("RESERVE_PINS_SDIO", "PD2,PC8,PC9,PC10,PC11,PC12");
#define GPIO_SDIO_CMD GPIO('D', 2)
#define GPIO_SDIO_D0 GPIO('C', 8)
#define GPIO_SDIO_D1 GPIO('C', 9)
#define GPIO_SDIO_D2 GPIO('C', 10)
#define GPIO_SDIO_D3 GPIO('C', 11)
#define GPIO_SDIO_CLK GPIO('C', 12)

#define SDIO_RESPONSE_SUCCESS   0
#define SDIO_RESPONSE_TIMEOUT   -1
#define SDIO_RESPONSE_CRCERR    -2
#define SDIO_RESPONSE_STA_ERR   -3

#define SDIO_SHORT_RESPONSE     (1 << 6)
#define SDIO_LONG_RESPONSE      (3 << 6)
#define SDIO_IO_TIMEOUT_MS      200
#define SDIO_INIT_RATE          400000
#define SDIO_TRANSFER_RATE      4000000
#if CONFIG_USBSERIAL
 #define SDIO_ADAPTER_CLOCK     48000000
#else
 #define SDIO_ADAPTER_CLOCK     50000000
#endif

#define SDIO_IO_ERR_MASK        (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_STBITERR)
#define SDIO_RX_DONE_MASK       (SDIO_STA_RXOVERR | SDIO_IO_ERR_MASK | SDIO_STA_DBCKEND)
#define SDIO_TX_DONE_MASK       (SDIO_STA_TXUNDERR | SDIO_IO_ERR_MASK | SDIO_STA_DBCKEND)
#define SDIO_ICR_CLEAR_FLAGS    0x7FF

static struct {
    uint32_t rca;
    uint8_t flags, error;
} sdio_config;

enum {SDF_INITIALIZED = 1, SDF_XFER_MODE = 2, SDF_HIGH_CAPACITY = 4,
      SDF_WRITE_PROTECTED = 8, SDF_CD_OFF = 16, SDF_DEINIT =  32};

static uint8_t
get_response_type(uint8_t command)
{
    switch (command) {
        case 0:
        case 4:
        case 15:
            // No response
            return 0;
        case 3:
            return 6;
        case 5:
            return 4;
        case 2:
        case 9:
        case 10:
            return 2;
        case 41:
            return 3;
        case 8:
            return 7;
        default:
            return 1;
    }
}

static uint16_t
get_response_length(uint8_t command)
{
    if (get_response_type(command) == 2)
        return SDIO_LONG_RESPONSE;
    return SDIO_SHORT_RESPONSE;
}

static uint8_t
response_has_crc(uint8_t command)
{
    // Commands that return an R3 and R4b response do not
    // include a CRC
    if (command == 1 || command == 5 || command == 41)
        return 0;
    return 1;
}

static void
sdio_set_rate(uint32_t rate)
{
    uint32_t div = SDIO_ADAPTER_CLOCK / rate;
    div = (div > 256) ? 255 : ((div >= 2) ? (div - 2) : 0);
    SDIO->CLKCR = (1 << 8) | (div & 0xFF);
}

static void
sdio_init(void)
{
    enable_pclock((uint32_t)SDIO);

    // Init SDIO GPIOs
    gpio_peripheral(GPIO_SDIO_CMD, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);
    gpio_peripheral(GPIO_SDIO_D0, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);
    gpio_peripheral(GPIO_SDIO_D1, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);
    gpio_peripheral(GPIO_SDIO_D2, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);
    gpio_peripheral(GPIO_SDIO_D3, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);
    gpio_peripheral(GPIO_SDIO_CLK, GPIO_FUNCTION(12) | GPIO_HIGH_SPEED, 1);

    // Power on SDIO
    SDIO->CLKCR = 0;
    SDIO->POWER = 0x3;
    udelay(2000);
    // Initialize rate to 400K
    sdio_set_rate(SDIO_INIT_RATE);
    SDIO->DTIMER = SDIO_TRANSFER_RATE / 1000 * SDIO_IO_TIMEOUT_MS;
}

static void
sdio_power_off(void)
{
    SDIO->POWER = 0;
}

static int
sdio_send_command(uint8_t command, uint32_t arg)
{
    // clear status flags
    SDIO->ICR = SDIO_ICR_CLEAR_FLAGS;
    // set the command argument
    SDIO->ARG = arg;
    uint8_t resp_type = get_response_type(command);
    uint8_t resp_len = 0;
    uint32_t sta_mask;
    if (resp_type) {
        resp_len = get_response_length(command);
        sta_mask = SDIO_STA_CTIMEOUT | SDIO_STA_CMDREND | SDIO_STA_CCRCFAIL;
    }
    else
        sta_mask = SDIO_STA_CTIMEOUT | SDIO_STA_CMDSENT;
    SDIO->CMD = (1 << 10) | resp_len | (command & 0x3f);
    while(!(SDIO->STA & sta_mask));
    uint32_t done_sta = SDIO->STA;
    if (done_sta & SDIO_STA_CTIMEOUT)
        return SDIO_RESPONSE_TIMEOUT;
    if (response_has_crc(command) && (done_sta & SDIO_STA_CCRCFAIL))
        return SDIO_RESPONSE_CRCERR;
    return SDIO_RESPONSE_SUCCESS;
}

static uint8_t
wait_programming_done(void)
{
    int ret = 0;
    uint8_t state;
    uint8_t tries = 100;
    uint32_t response;
    while (tries) {
        udelay(1000);
        ret = sdio_send_command(SDCMD_SEND_STATUS, sdio_config.rca);
        response = SDIO->RESP1;
        state = ((response >> 9) & 0xF);
        if (ret != SDIO_RESPONSE_SUCCESS)
            tries--;
        else if (state != 6 && state != 7)
            return 1;
    }
    return 0;
}

static int
sdio_write_block(uint32_t* write_buf, uint32_t address)
{
    // write a single block
    SDIO->DCTRL = 0;
    SDIO->DLEN = SD_SECTOR_SIZE;
    int ret = sdio_send_command(SDCMD_WRITE_BLOCK, address);
    if (ret != SDIO_RESPONSE_SUCCESS) {
        sdio_config.error = SD_ERROR_WRITE_BLOCK;
        return ret;
    }
    SDIO->DCTRL = (9 << 4) | SDIO_DCTRL_DTEN;
    uint32_t write_count = 0;
    uint32_t status = 0;
    do {
        status = SDIO->STA;
        if (status & SDIO_STA_TXFIFOHE && write_count < SD_SECTOR_SIZE) {
            for (uint8_t i = 0; i < 8; i++) {
                SDIO->FIFO = readl(write_buf++);
            }
            write_count += 32;
        }
    } while (!(status & SDIO_TX_DONE_MASK));
    if (write_count < SD_SECTOR_SIZE) {
        sdio_config.error = SD_ERROR_WRITE_BLOCK;
        ret = -4;
    } else if (!wait_programming_done()) {
        // Wait for card to finish transmitting/programming
        ret = -5;
        sdio_config.error = SD_ERROR_WRITE_BLOCK;
    }
    return ret;
}


static int
sdio_read_block(uint32_t* read_buf, uint32_t address)
{
    // read a single block
    SDIO->DCTRL = 0;
    SDIO->DLEN = SD_SECTOR_SIZE;
    int ret = sdio_send_command(SDCMD_READ_SINGLE_BLOCK, address);
    if (ret != SDIO_RESPONSE_SUCCESS) {
        sdio_config.error = SD_ERROR_READ_BLOCK;
        return ret;
    }
    SDIO->DCTRL = (9 << 4) | SDIO_DCTRL_DTDIR | SDIO_DCTRL_DTEN;
    uint32_t read_count = 0;
    uint32_t status = 0;
    do {
        status = SDIO->STA;
        if (status & SDIO_STA_RXFIFOHF && read_count < SD_SECTOR_SIZE) {
            for (uint8_t i = 0; i < 8; i++)
                writel(read_buf++, SDIO->FIFO);
            read_count += 32;
        }
    } while (!(status & SDIO_RX_DONE_MASK));
    if (read_count < SD_SECTOR_SIZE) {
        sdio_config.error = SD_ERROR_READ_BLOCK;
        ret = -4;
    }
    return ret;
}


static uint8_t
check_command(uint8_t cmd, uint32_t arg, uint8_t is_acmd,
              uint32_t expect, uint32_t mask, uint8_t attempts)
{
    int ret;
    while (attempts--) {
        if (is_acmd) {
            ret = sdio_send_command(SDCMD_APP_CMD, sdio_config.rca);
            if (ret != SDIO_RESPONSE_SUCCESS) {
                if (attempts) udelay(1000);
                continue;
            }
        }
        ret = sdio_send_command(cmd, arg);
        if (ret == SDIO_RESPONSE_SUCCESS)
        {
            if ((get_response_type(cmd) == 0) ||
                ((SDIO->RESP1 & mask) == expect))
            {
                return 1;
            }
        }
        if (attempts)
            udelay((cmd == SDCMD_SEND_OP_COND) ? 100000 : 1000);
    }
    return 0;
}

static uint8_t
check_interface_condition(void)
{
    int ret;
    uint8_t attempts = 3;
    uint32_t response;
    while(attempts--)
    {
        // CMD8, Request 2.7V-3.6V range, check pattern 0x0A
        ret = sdio_send_command(SDCMD_SEND_IF_COND, 0x10A);
        response = SDIO->RESP1;
        if (ret == SDIO_RESPONSE_TIMEOUT || (response & (1 << 22)))
            // illegal command, version 1 card
            return 1;
        else if (ret == SDIO_RESPONSE_SUCCESS && (response & 0xFFF) == 0x10A)
            return 2;
        if (attempts) udelay(1000);
    }
    return 0;
}

/**********************************************************
 *
 * SDCard Methods
 *
 * ********************************************************/

uint8_t
sdcard_write_sector(uint8_t *buf, uint32_t sector)
{
    if (!(sdio_config.flags & SDF_INITIALIZED))
        return 0;
    uint32_t addr = sector;
    if (!(sdio_config.flags & SDF_HIGH_CAPACITY))
        addr = sector * SD_SECTOR_SIZE;
    int ret = sdio_write_block((uint32_t *)buf, addr);
    if (ret < 0)
        return 0;
    return 1;
}

uint8_t
sdcard_read_sector(uint8_t *buf, uint32_t sector)
{
    if (!(sdio_config.flags & SDF_INITIALIZED))
        return 0;
    uint32_t addr = sector;
    if (!(sdio_config.flags & SDF_HIGH_CAPACITY))
        addr = sector * SD_SECTOR_SIZE;
    int ret = sdio_read_block((uint32_t *)buf, addr);
    if (ret < 0)
        return 0;
    return 1;
}

uint8_t
sdcard_init(void)
{
    uint8_t sd_ver;
    sdio_init();
    // Attempt software reset

    // Software Reset (CMD0).  Send twice in an attempt to ensure reset
    sdio_send_command(SDCMD_GO_IDLE_STATE, 0);
    udelay(100000);
    sdio_send_command(SDCMD_GO_IDLE_STATE, 0);
    // Check interface condition to determine card version
    sd_ver = check_interface_condition();
    if (!sd_ver) {
        sdio_config.error = SD_ERROR_SEND_IF_COND;
        return 0;
    }

    // CMD41 - Go Operational, voltage window 3.2-3.4v
    uint32_t arg = ((sd_ver == 1) ? 0 : (1 << 30)) | (1 << 20);
    uint32_t ocr_expect = (1 << 31) | (1 << 20);
    if (!check_command(SDCMD_SEND_OP_COND, arg, 1, ocr_expect, ocr_expect, 20))
    {
        sdio_config.error = SD_ERROR_SEND_OP_COND;
        return 0;
    }
    if (SDIO->RESP1 & (1 << 30))
        sdio_config.flags |= SDF_HIGH_CAPACITY;

    // request CID
    if (sdio_send_command(SDCMD_ALL_SEND_CID, 0) < 0) {
        sdio_config.error = SD_ERROR_ALL_SEND_CID;
        return 0;
    }
    // Set Relative Address, don't allow the card to select "0"
    while (!sdio_config.rca) {
        if (sdio_send_command(SDCMD_SEND_REL_ADDR, 0) < 0) {
            sdio_config.error = SD_ERROR_SEND_REL_ADDR;
            return 0;
        }
        sdio_config.rca = SDIO->RESP1 & 0xFFFF0000;
    }
    // Request CSD
    if (sdio_send_command(SDCMD_SEND_CSD, sdio_config.rca) < 0) {
        sdio_config.error = SD_ERROR_SEND_CSD;
        return 0;
    }
    if (SDIO->RESP4 & (3 << 12)) {
        // card is write protected, can't rename the
        // firmware file after upload.
        sdio_config.flags |= SDF_WRITE_PROTECTED;
        sdio_config.error = SD_ERROR_WRITE_BLOCK;
        return 0;
    }
    // put card in transfer mode
    if (sdio_send_command(SDCMD_SEL_DESEL_CARD, sdio_config.rca) < 0) {
        sdio_config.error = SD_ERROR_SEL_DESEL_CARD;
        return 0;
    }
    // card is out of identification mode, increase clock
    sdio_set_rate(SDIO_TRANSFER_RATE);
    sdio_config.flags |= SDF_XFER_MODE;
    if (check_command(SDCMD_SET_CLR_CD_DETECT, 0, 1, 0, 0, 3))
        sdio_config.flags |= SDF_CD_OFF;
    else {
        sdio_config.error = SD_ERROR_SET_CARD_DETECT;
        return 0;
    }
    // Set block length to 512
    uint32_t bl_mask = 1 << 29;
    if (!check_command(SDCMD_SET_BLOCKLEN, SD_SECTOR_SIZE, 0, 0, bl_mask, 5)) {
        sdio_config.error = SD_ERROR_SET_BLOCKLEN;
        return 0;
    }
    sdio_config.flags |= SDF_INITIALIZED;
    return 1;
}

void
sdcard_deinit(void)
{
    if (sdio_config.flags & SDF_DEINIT)
        return;
    sdio_config.flags |= SDF_DEINIT;
    if (sdio_config.flags & SDF_CD_OFF) {
        if (check_command(SDCMD_SET_CLR_CD_DETECT, 1, 1, 0, 0, 1))
            sdio_config.flags &= ~SDF_CD_OFF;
    }
    if (sdio_config.flags |= SDF_XFER_MODE) {
        sdio_send_command(SDCMD_SEL_DESEL_CARD, 0);
        sdio_config.flags &= ~SDF_XFER_MODE;
    }
    sdio_send_command(SDCMD_GO_IDLE_STATE, 0);
    sdio_power_off();
    sdio_config.rca = 0;
}

uint16_t
sdcard_report_status(void)
{
    return (sdio_config.error << 8) | (sdio_config.flags);
}
