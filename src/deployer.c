// Main code for canboot deployer application
//
// Copyright (C) 2016-2022  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcmp
#include "autoconf.h" // CONFIG_BLOCK_SIZE
#include "board/armcm_reset.h" // try_request_canboot
#include "board/flash.h" // flash_write_block
#include "board/io.h" // readb
#include "board/misc.h" // timer_read_time
#include "canboot.h" // timer_setup
#include "deployer.h" // deployer_is_active
#include "sched.h" // sched_check_periodic

// The CanBoot "deployer application" is running
int
deployer_is_active(void)
{
    return 1;
}

// Implement simple delay mechanism
void
udelay(uint32_t usecs)
{
    uint32_t end = timer_read_time() + timer_from_us(usecs);
    while (timer_is_before(timer_read_time(), end))
        ;
}

// Wrapper for Klipper compatibility
void
sched_wake_tasks(void)
{
}

// Note that a task is ready to run
void
sched_wake_task(struct task_wake *w)
{
    writeb(&w->wake, 1);
}

// Check if a task is ready to run (as indicated by sched_wake_task)
uint8_t
sched_check_wake(struct task_wake *w)
{
    if (!readb(&w->wake))
        return 0;
    writeb(&w->wake, 0);
    return 1;
}

// Halt the processor
static void
halt(void)
{
    for (;;)
        ;
}

// Init followed by CanBoot flashing
void
sched_main(void)
{
    timer_setup();

    // Run all init functions marked with DECL_INIT()
    extern void ctr_run_initfuncs(void);
    ctr_run_initfuncs();

    // Check if flash has already been written
    void *fstart = (void*)CONFIG_FLASH_START;
    uint32_t cbsize = deployer_canboot_binary_size;
    if (memcmp(fstart, deployer_canboot_binary, cbsize) == 0) {
        try_request_canboot();
        halt();
    }

    // Wait 100ms to help ensure power supply is stable before
    // overwriting existing bootloader
    udelay(100000);

    // Write CanBoot to flash
    const uint8_t *p = deployer_canboot_binary, *end = &p[cbsize];
    while (p < end) {
        uint32_t data[CONFIG_BLOCK_SIZE / 4];
        memset(data, 0xff, sizeof(data));
        uint32_t c = CONFIG_BLOCK_SIZE;
        if (p + c > end)
            c = end - p;
        memcpy(data, p, c);
        int ret = flash_write_block((uint32_t)fstart, data);
        if (ret < 0)
            // An error occurred - halt to try avoiding endless boot loops
            halt();
        p += c;
        fstart += c;
    }
    flash_complete();

    // Flash completed - reboot into canboot
    try_request_canboot();
}
