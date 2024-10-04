// SDCard Upload implementation
//
// Copyright (C) 2022 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memmove
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "canboot.h" // application_read_flash
#include "board/misc.h"  // timer_read_time
#include "board/flash.h" // flash_write_block
#include "sdcard.h"     // sdcard_init
#include "ff.h"         // f_open
#include "diskio.h"     // disk_initialize
#include "command.h"    // command_set_enable
#include "flashcmd.h"   // set_in_transfer
#include "sched.h" // DECL_TASK

static struct {
    FATFS fs;
    FIL file_obj;
    DSTATUS disk_status;
    uint8_t file_open;
    uint8_t flash_state;
    uint32_t block_address;
    uint32_t start_time;
} ff_data;

enum {SDS_BEGIN_XFER = 1, SDS_NEED_UPLOAD, SDS_NEED_VERIFY, SDS_DONE};

/**********************************************************
 * FatFS Callbacks
 * ********************************************************/

DSTATUS
disk_initialize(BYTE pdrv)
{
    if (sdcard_init()) {
        ff_data.disk_status = 0;
        return 0;
    }
    return STA_NOINIT;
}

DSTATUS
disk_status(BYTE pdrv)
{
    return ff_data.disk_status;
}


DRESULT
disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t* read_buf = buff;
    while (count) {
        if (!sdcard_read_sector(read_buf, sector++))
            return RES_ERROR;
        count--;
        if (count)
            read_buf += SD_SECTOR_SIZE;
    }
    return RES_OK;
}


DRESULT
disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    uint8_t* write_buf = (uint8_t *)buff;
    while (count) {
        if (!sdcard_write_sector(write_buf, sector++))
            return RES_ERROR;
        count--;
        if (count)
            write_buf += SD_SECTOR_SIZE;
    }
    return RES_OK;
}

DRESULT
disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    // No need for ioctls in a minimal fatfs implementation
    return RES_OK;
}

static void
sdcard_close(void)
{
    command_set_enable(1);
    set_in_transfer(0);
    if (ff_data.file_open)
        f_close(&(ff_data.file_obj));
    ff_data.file_open = 0;
    f_unmount("");
    memset(&(ff_data.fs), 0, sizeof(ff_data.fs));
    sdcard_deinit();
}

void
sdcard_open(void)
{
    if (!CONFIG_ENABLE_SDCARD)
        return;
    command_set_enable(0);
    ff_data.disk_status = STA_NOINIT;
    FRESULT res;
    res = f_mount(&(ff_data.fs), "", 1);
    if (res != FR_OK)
        goto fail;
    // open file in read mode
    const char* firmware_name = CONFIG_SD_FIRMWARE_NAME;
    res = f_open(&(ff_data.file_obj), firmware_name, 1);
    if (res != FR_OK)
        goto fail;
    ff_data.file_open = 1;
    ff_data.flash_state = SDS_BEGIN_XFER;
    ff_data.block_address = CONFIG_LAUNCH_APP_ADDRESS;
    ff_data.start_time = timer_read_time() + timer_from_us(1000000);
    set_in_transfer(1);
    return;
fail:
    sdcard_close();
}
DECL_INIT(sdcard_open);

static void
rename_firmware(const char* new_ext)
{
    if (ff_data.file_open) {
        f_close(&ff_data.file_obj);
        ff_data.file_open = 0;
    }
    const char* firmware_name = CONFIG_SD_FIRMWARE_NAME;
    uint8_t len = strlen(firmware_name);
    char* end = strrchr(firmware_name, '.');
    uint8_t count = end == NULL ? len : end - firmware_name;
    char new_fname[len + 5];
    memset(new_fname, 0, len + 5);
    memcpy(new_fname, firmware_name, count);
    new_fname[count] = '.';
    strcat(new_fname, new_ext);
    f_unlink(new_fname);
    f_rename(firmware_name, new_fname);
}

static void
sdcard_flash_error(void)
{
    ff_data.flash_state = 0;
    rename_firmware("err");
    sdcard_close();
    set_complete();
}

static void
sdcard_upload_block(void)
{
    uint32_t sd_buf[CONFIG_BLOCK_SIZE / 4];
    memset(sd_buf, 0xFF, CONFIG_BLOCK_SIZE);
    UINT bcount;
    if (f_read(&(ff_data.file_obj), sd_buf, CONFIG_BLOCK_SIZE,
               &bcount) != FR_OK) {
        // Transfer Error
        sdcard_flash_error();
        return;
    }
    if (bcount > 0) {
        if (flash_write_block(ff_data.block_address, sd_buf) < 0) {
            sdcard_flash_error();
            return;
        }
        ff_data.block_address += CONFIG_BLOCK_SIZE;
    }
    else {
        if (flash_complete() < 0)
            sdcard_flash_error();
        else {
            f_rewind(&ff_data.file_obj);
            ff_data.block_address = CONFIG_LAUNCH_APP_ADDRESS;
            ff_data.flash_state = SDS_NEED_VERIFY;
        }
    }
}

static void
sdcard_verify_block(void)
{
    uint32_t sd_buf[CONFIG_BLOCK_SIZE / 4];
    uint32_t flash_buf[CONFIG_BLOCK_SIZE / 4];
    memset(sd_buf, 0xFF, CONFIG_BLOCK_SIZE);
    UINT bcount;
    if (f_read(&(ff_data.file_obj), sd_buf, CONFIG_BLOCK_SIZE,
               &bcount) != FR_OK) {
        // Transfer Error
        sdcard_flash_error();
        return;
    }
    if (bcount > 0) {
        application_read_flash(ff_data.block_address, flash_buf);
        if (memcmp(flash_buf, sd_buf, CONFIG_BLOCK_SIZE) != 0) {
            sdcard_flash_error();
            return;
        }
        ff_data.block_address += CONFIG_BLOCK_SIZE;
    }
    else
        ff_data.flash_state = SDS_DONE;
}

void
sdcard_upload_task(void)
{
    if (!CONFIG_ENABLE_SDCARD)
        return;
    switch(ff_data.flash_state) {
        case SDS_BEGIN_XFER:
            // Delay 1 second to prevent accidental reset during upload
            if (!timer_is_before(timer_read_time(), ff_data.start_time))
                ff_data.flash_state = SDS_NEED_UPLOAD;
            break;
        case SDS_NEED_UPLOAD:
            sdcard_upload_block();
            break;
        case SDS_NEED_VERIFY:
            sdcard_verify_block();
            break;
        case SDS_DONE:
            ff_data.flash_state = 0;
            rename_firmware("cur");
            sdcard_close();
            set_complete();
    }
}
DECL_TASK(sdcard_upload_task);
