// rp2350 specific handling for checking if double reset occurred
//
// Copyright (C) 2024  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <stdint.h> // uint32_t
#include "bootentry.h" // board_check_double_reset
#include "canboot.h" // udelay
#include "hardware/structs/powman.h" // powman_hw

#define DOUBLE_CLICK_MIN_US 10000
#define DOUBLE_CLICK_MAX_US 500000

int
board_check_double_reset(void)
{
    if (powman_hw->chip_reset & POWMAN_CHIP_RESET_DOUBLE_TAP_BITS) {
        // Double reset detected - clear flag and enter bootloader
        powman_hw->chip_reset = 0x5afe0000;
        return 1;
    }
    // Initial delay (reset in under 10ms isn't a "double tap")
    udelay(DOUBLE_CLICK_MIN_US);
    // Set the DOUBLE_TAP_BITS bits
    powman_hw->chip_reset = 0x5afe0001;
    udelay(DOUBLE_CLICK_MAX_US - DOUBLE_CLICK_MIN_US);
    // No reset, clear the DOUBLE_TAP_BITS bits
    powman_hw->chip_reset = 0x5afe0000;
    return 0;
}
