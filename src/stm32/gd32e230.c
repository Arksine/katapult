// Main clock and initialization functions for GD32E23x
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/armcm_boot.h"
#include "internal.h"
#include "sched.h"

#define OSC_TIMEOUT 0x000fffffU

static void clock_setup(void)
{
    RCU_CTL0 |= RCU_CTL0_HXTALEN;
    uint32_t timeout = OSC_TIMEOUT;
    while (!(RCU_CTL0 & RCU_CTL0_HXTALSTB) && --timeout)
        ;
    if (!timeout)
        for (;;)
            ;
    FMC_WS = (FMC_WS & ~FMC_WS_WSCNT) | WS_WSCNT_2;
    RCU_CFG0 = (RCU_CFG0 & ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC
                             | RCU_CFG0_APB2PSC | RCU_CFG0_PLLSEL
                             | RCU_CFG0_PLLMF | RCU_CFG0_PLLDV))
               | RCU_AHB_CKSYS_DIV1 | RCU_APB1_CKAHB_DIV1
               | RCU_APB2_CKAHB_DIV1 | RCU_PLLSRC_HXTAL | RCU_PLL_MUL9;
    RCU_CTL0 |= RCU_CTL0_PLLEN;
    while (!(RCU_CTL0 & RCU_CTL0_PLLSTB))
        ;
    RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS) | RCU_CKSYSSRC_PLL;
    while ((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL)
        ;
}

void enable_pclock(uint32_t pclk)
{
    if (pclk == USART0)
        pclk = RCU_USART0;
    else if (pclk == USART1)
        pclk = RCU_USART1;
    RCU_REG_VAL(pclk) |= BIT(RCU_BIT_POS(pclk));
    (void)RCU_REG_VAL(pclk);
}

void gpio_clock_enable(GPIO_TypeDef *regs)
{
    static const uint32_t gpio_clocks[] = {
        RCU_GPIOA, RCU_GPIOB, RCU_GPIOC, 0, 0, RCU_GPIOF
    };
    uint32_t port = ((uint32_t)regs - GPIO_BASE) / 0x400U;
    if (port < 6 && gpio_clocks[port])
        enable_pclock(gpio_clocks[port]);
}

uint32_t get_pclock_frequency(uint32_t periph_base)
{
    return CONFIG_CLOCK_FREQ;
}

void armcm_main(void)
{
    clock_setup();
    SCB->VTOR = (uint32_t)VectorTable;
    sched_main();
}
