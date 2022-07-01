// Code to setup clocks on stm32g0
//
// Copyright (C) 2019-2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_REF_FREQ
#include "board/armcm_boot.h" // armcm_main
#include "board/irq.h" // irq_disable
#include "command.h" // DECL_CONSTANT_STR
#include "internal.h" // enable_pclock
#include "sched.h" // sched_main


/****************************************************************
 * Clock setup
 ****************************************************************/

#define FREQ_PERIPH 64000000
#define FREQ_USB 48000000

// Map a peripheral address to its enable bits
struct cline
lookup_clock_line(uint32_t periph_base)
{
    if (periph_base >= IOPORT_BASE) {
        uint32_t bit = 1 << ((periph_base - IOPORT_BASE) / 0x400);
        return (struct cline){.en=&RCC->IOPENR, .rst=&RCC->IOPRSTR, .bit=bit};
    } else if (periph_base >= AHBPERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - AHBPERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->AHBENR, .rst=&RCC->AHBRSTR, .bit=bit};
    }
    if ((periph_base == FDCAN1_BASE) || (periph_base == FDCAN2_BASE))
        return (struct cline){.en=&RCC->APBENR1,.rst=&RCC->APBRSTR1,.bit=1<<12};
    if (periph_base == USB_BASE)
        return (struct cline){.en=&RCC->APBENR1,.rst=&RCC->APBRSTR1,.bit=1<<13};
    if (periph_base == CRS_BASE)
        return (struct cline){.en=&RCC->APBENR1,.rst=&RCC->APBRSTR1,.bit=1<<16};
    if (periph_base == SPI1_BASE)
        return (struct cline){.en=&RCC->APBENR2,.rst=&RCC->APBRSTR2,.bit=1<<12};
    if (periph_base == USART1_BASE)
        return (struct cline){.en=&RCC->APBENR2,.rst=&RCC->APBRSTR2,.bit=1<<14};
    if (periph_base == ADC1_BASE)
        return (struct cline){.en=&RCC->APBENR2,.rst=&RCC->APBRSTR2,.bit=1<<20};
    uint32_t bit = 1 << ((periph_base - APBPERIPH_BASE) / 0x400);
    return (struct cline){.en=&RCC->APBENR1, .rst=&RCC->APBRSTR1, .bit=bit};
}

// Return the frequency of the given peripheral clock
uint32_t
get_pclock_frequency(uint32_t periph_base)
{
    return FREQ_PERIPH;
}

// Enable a GPIO peripheral clock
void
gpio_clock_enable(GPIO_TypeDef *regs)
{
    uint32_t rcc_pos = ((uint32_t)regs - IOPORT_BASE) / 0x400;
    RCC->IOPENR |= 1 << rcc_pos;
    RCC->IOPENR;
}

#if !CONFIG_STM32_CLOCK_REF_INTERNAL
DECL_CONSTANT_STR("RESERVE_PINS_crystal", "PF0,PF1");
#endif

// Configure and enable the PLL as clock source
static void
clock_setup(void)
{
    uint32_t pll_base = 4000000, pll_freq = 192000000, pllcfgr;
    if (!CONFIG_STM32_CLOCK_REF_INTERNAL) {
        // Configure PLL from external crystal (HSE)
        uint32_t div = CONFIG_CLOCK_REF_FREQ / pll_base;
        RCC->CR |= RCC_CR_HSEON;
        pllcfgr = RCC_PLLCFGR_PLLSRC_HSE | ((div - 1) << RCC_PLLCFGR_PLLM_Pos);
    } else {
        // Configure PLL from internal 16Mhz oscillator (HSI)
        uint32_t div = 16000000 / pll_base;
        pllcfgr = RCC_PLLCFGR_PLLSRC_HSI | ((div - 1) << RCC_PLLCFGR_PLLM_Pos);
    }
    pllcfgr |= (pll_freq/pll_base) << RCC_PLLCFGR_PLLN_Pos;
    pllcfgr |= (pll_freq/CONFIG_CLOCK_FREQ - 1) << RCC_PLLCFGR_PLLR_Pos;
    pllcfgr |= (pll_freq/FREQ_USB - 1) << RCC_PLLCFGR_PLLQ_Pos;
    RCC->PLLCFGR = pllcfgr | RCC_PLLCFGR_PLLREN | RCC_PLLCFGR_PLLQEN;
    RCC->CR |= RCC_CR_PLLON;

    // Wait for PLL lock
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    // Switch system clock to PLL
    RCC->CFGR = (2 << RCC_CFGR_SW_Pos);
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != (2 << RCC_CFGR_SWS_Pos))
        ;

    // Use PLLQCLK for USB (setting USBSEL=2 works in practice)
    RCC->CCIPR2 = 2 << RCC_CCIPR2_USBSEL_Pos;
}


/****************************************************************
 * Startup
 ****************************************************************/

// Main entry point - called from armcm_boot.c:ResetHandler()
void
armcm_main(void)
{
    SCB->VTOR = (uint32_t)VectorTable;

    // Reset clock registers (in case bootloader has changed them)
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY))
        ;
    RCC->CFGR = 0x00000000;
    RCC->CR = RCC_CR_HSION;
    while (RCC->CR & RCC_CR_PLLRDY)
        ;
    RCC->PLLCFGR = 0x00001000;
    RCC->IOPENR = 0x00000000;
    RCC->AHBENR = 0x00000100;
    RCC->APBENR1 = 0x00000000;
    RCC->APBENR2 = 0x00000000;

    // Set flash latency
    FLASH->ACR = (2<<FLASH_ACR_LATENCY_Pos) | FLASH_ACR_ICEN | FLASH_ACR_PRFTEN;

    // Configure main clock
    clock_setup();

    sched_main();
}
