// rp2040 timer support
//
// Copyright (C) 2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_FREQ
#include "board/armcm_boot.h" // armcm_enable_irq
#include "board/irq.h" // irq_disable
#include "board/misc.h" // timer_read_time
#include "canboot.h" // timer_setup
#include "hardware/structs/resets.h" // RESETS_RESET_UART0_BITS
#include "hardware/structs/timer.h" // RESETS_RESET_UART0_BITS
#include "internal.h" // enable_pclock
#include "sched.h" // DECL_INIT


/****************************************************************
 * Low level timer code
 ****************************************************************/

// Return the number of clock ticks for a given number of microseconds
uint32_t
timer_from_us(uint32_t us)
{
    return us * (CONFIG_CLOCK_FREQ / 1000000);
}

// Return true if time1 is before time2.  Always use this function to
// compare times as regular C comparisons can fail if the counter
// rolls over.
uint8_t
timer_is_before(uint32_t time1, uint32_t time2)
{
    return (int32_t)(time1 - time2) < 0;
}

// Return the current time (in absolute clock ticks).
uint32_t
timer_read_time(void)
{
    return timer_hw->timerawl;
}

/****************************************************************
 * Setup and irqs
 ****************************************************************/

void
timer_setup(void)
{
    irq_disable();
    enable_pclock(RESETS_RESET_TIMER_BITS);
    timer_hw->timelw = 0;
    timer_hw->timehw = 0;
    irq_enable();
}
