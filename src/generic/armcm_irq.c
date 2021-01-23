// Definitions for irq enable/disable on ARM Cortex-M processors
//
// Copyright (C) 2017-2018  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "irq.h" // irqstatus_t

void
irq_disable(void)
{
    asm volatile("cpsid i" ::: "memory");
}

void
irq_enable(void)
{
    asm volatile("cpsie i" ::: "memory");
}

irqstatus_t
irq_save(void)
{
    irqstatus_t flag;
    asm volatile("mrs %0, primask" : "=r" (flag) :: "memory");
    irq_disable();
    return flag;
}

void
irq_restore(irqstatus_t flag)
{
    asm volatile("msr primask, %0" :: "r" (flag) : "memory");
}

void
irq_wait(void)
{
    asm volatile("cpsie i\n    wfi\n    cpsid i\n" ::: "memory");
}

void
irq_poll(void)
{
}
