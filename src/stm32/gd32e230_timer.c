// Timer support for GD32E23x
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/armcm_boot.h"
#include "board/io.h"
#include "board/irq.h"
#include "board/misc.h"
#include "canboot.h"
#include "internal.h"

static uint32_t timer_high;

static inline uint32_t timer_get(void) { return TIMER_CNT(TIMER2) & 0xffff; }
static inline void timer_set(uint32_t next) {
    TIMER_CH0CV(TIMER2) = next & 0xffff;
    TIMER_INTF(TIMER2) = ~TIMER_INTF_CH0IF;
}

uint32_t timer_read_time(void)
{
    uint32_t high = readl(&timer_high), low = timer_get();
    return (high ^ low) + (high & 0xffff);
}

void __aligned(16) TIMER2_IRQHandler(void)
{
    irq_disable();
    timer_high += 0x8000;
    timer_set(timer_high + 0x8000);
    irq_enable();
}

void timer_setup(void)
{
    irqstatus_t flags = irq_save();
    enable_pclock(RCU_TIMER2);
    TIMER_CTL0(TIMER2) = 0;
    TIMER_PSC(TIMER2) = 0;
    TIMER_CAR(TIMER2) = 0xffff;
    TIMER_CNT(TIMER2) = 0;
    TIMER_DMAINTEN(TIMER2) = TIMER_INT_CH0;
    armcm_enable_irq(TIMER2_IRQHandler, TIMER2_IRQn, 2);
    timer_set(0x8000);
    TIMER_CTL0(TIMER2) = TIMER_CTL0_CEN;
    irq_restore(flags);
}

uint32_t timer_from_us(uint32_t us) { return us * (CONFIG_CLOCK_FREQ / 1000000); }
uint8_t timer_is_before(uint32_t t1, uint32_t t2) { return (int32_t)(t1 - t2) < 0; }
