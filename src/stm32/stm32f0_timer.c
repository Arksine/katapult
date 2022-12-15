// STM32F0 timer support
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/armcm_boot.h" // armcm_enable_irq
#include "board/armcm_timer.h" // udelay
#include "board/internal.h" // TIM3
#include "board/io.h" // readl
#include "board/irq.h" // irq_disable
#include "board/misc.h" // timer_read_time
#include "canboot.h" // timer_setup


/****************************************************************
 * Low level timer code
 ****************************************************************/

// Use 32bit TIM2 timer if available (otherwise use 16bit TIM3 timer)
#if defined(TIM2)
  #define TIMx TIM2
  #define TIMx_IRQn TIM2_IRQn
  #define HAVE_TIMER_32BIT 1
#elif defined(TIM3)
  #define TIMx TIM3
  #define TIMx_IRQn TIM3_IRQn
  #define HAVE_TIMER_32BIT 0
#endif

// Some chips have slightly different register names
#if CONFIG_MACH_STM32G0B0
  #define TIM3_IRQn TIM3_TIM4_IRQn
#endif

static inline uint32_t
timer_get(void)
{
    return TIMx->CNT;
}

static inline void
timer_set(uint32_t next)
{
    TIMx->CCR1 = next;
    TIMx->SR = 0;
}


/****************************************************************
 * 16bit hardware timer to 32bit conversion
 ****************************************************************/

// High bits of timer (top 17 bits)
static uint32_t timer_high;

// Return the current time (in absolute clock ticks).
uint32_t __always_inline
timer_read_time(void)
{
    if (HAVE_TIMER_32BIT)
        return timer_get();
    uint32_t th = readl(&timer_high);
    uint32_t cur = timer_get();
    // Combine timer_high (high 17 bits) and current time (low 16
    // bits) using method that handles rollovers correctly.
    return (th ^ cur) + (th & 0xffff);
}


/****************************************************************
 * Setup and irqs
 ****************************************************************/

// Hardware timer IRQ handler - dispatch software timers
void __aligned(16)
TIMx_IRQHandler(void)
{
    irq_disable();
    timer_high += 0x8000;
    timer_set(timer_high + 0x8000);
    irq_enable();
}

// Initialize the timer
void
timer_setup(void)
{
    irqstatus_t flag = irq_save();
    enable_pclock((uint32_t)TIMx);
    TIMx->CNT = 0;
#if !HAVE_TIMER_32BIT
    TIMx->DIER = TIM_DIER_CC1IE;
    TIMx->CCER = TIM_CCER_CC1E;
    armcm_enable_irq(TIMx_IRQHandler, TIMx_IRQn, 2);
    timer_set(0x8000);
#endif
    TIMx->CR1 = TIM_CR1_CEN;
    irq_restore(flag);
}

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
