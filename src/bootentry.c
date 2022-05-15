// Determine if the bootloader or application should start
//
// Copyright (C) 2021 Eric Callahan <arksine.code@gmail.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h>         // strlen
#include "autoconf.h"       // CONFIG_*
#include "board/flash.h"    // flash_read_block
#include "board/gpio.h"     // gpio_in_setup
#include "board/misc.h"     // set_bootup_code
#include "bootentry.h"      // bootentry_check
#include "ctr.h"            // DECL_CTR
#include "sched.h"          // udelay

#define REQUEST_SIG    0x5984E3FA6CA1589B // Random request sig

// Generated by buildcommands.py
DECL_CTR("DECL_BUTTON " __stringify(CONFIG_BUTTON_PIN));
extern int32_t button_gpio, button_high, button_pullup;

// Check for a bootloader request via double tap of reset button
static int
check_button_pressed(void)
{
    if (!CONFIG_ENABLE_BUTTON)
        return 0;
    struct gpio_in button = gpio_in_setup(button_gpio, button_pullup);
    udelay(10);
    return gpio_in_read(button) == button_high;
}

// Check for a bootloader request via double tap of reset button
static void
check_double_reset(void)
{
    if (!CONFIG_ENABLE_DOUBLE_RESET)
        return;
    // set request signature and delay for two seconds.  This enters the bootloader if
    // the reset button is double clicked
    set_bootup_code(REQUEST_SIG);
    udelay(500000);
    set_bootup_code(0);
    // No reset, read the key back out to clear it
}

// Check if bootloader or application should be started
int
bootentry_check(void)
{
    // Enter the bootloader in the following conditions:
    // - The request signature is set in memory (request from app)
    // - No application code is present
    uint64_t bootup_code = get_bootup_code();
    if (bootup_code == REQUEST_SIG || !application_check_valid()
        || check_button_pressed()) {
        // Start bootloader main loop
        set_bootup_code(0);
        return 1;
    }
    check_double_reset();

    // jump to app
    return 0;
}
