// Code to setup clocks on stm32f0
//
// Copyright (C) 2019-2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_REF_FREQ
#include "board/armcm_boot.h" // armcm_main
#include "board/irq.h" // irq_disable
#include "board/misc.h" // read magic key
#include "internal.h" // enable_pclock
#include "canboot_main.h" // sched_main

#define FREQ_PERIPH 48000000

// Map a peripheral address to its enable bits
struct cline
lookup_clock_line(uint32_t periph_base)
{
    if (periph_base >= AHB2PERIPH_BASE) {
        uint32_t bit = 1 << ((periph_base - AHB2PERIPH_BASE) / 0x400 + 17);
        return (struct cline){.en=&RCC->AHBENR, .rst=&RCC->AHBRSTR, .bit=bit};
    } else if (periph_base >= SYSCFG_BASE) {
        uint32_t bit = 1 << ((periph_base - SYSCFG_BASE) / 0x400);
        return (struct cline){.en=&RCC->APB2ENR, .rst=&RCC->APB2RSTR, .bit=bit};
    } else {
        uint32_t bit = 1 << ((periph_base - APBPERIPH_BASE) / 0x400);
        return (struct cline){.en=&RCC->APB1ENR, .rst=&RCC->APB1RSTR, .bit=bit};
    }
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
    uint32_t rcc_pos = ((uint32_t)regs - AHB2PERIPH_BASE) / 0x400;
    RCC->AHBENR |= 1 << (rcc_pos + 17);
    RCC->AHBENR;
}

// Configure and enable the PLL as clock source
static void
pll_setup(void)
{
    uint32_t cfgr;
    if (!CONFIG_STM32_CLOCK_REF_INTERNAL) {
        // Configure 48Mhz PLL from external crystal (HSE)
        uint32_t div = CONFIG_CLOCK_FREQ / CONFIG_CLOCK_REF_FREQ;
        RCC->CR = ((RCC->CR & ~RCC_CR_HSITRIM) | RCC_CR_HSEON
                   | (CONFIG_STM32F0_TRIM << RCC_CR_HSITRIM_Pos));
        cfgr = RCC_CFGR_PLLSRC_HSE_PREDIV | ((div - 2) << RCC_CFGR_PLLMUL_Pos);
    } else {
        // Configure 48Mhz PLL from internal 8Mhz oscillator (HSI)
        uint32_t div2 = (CONFIG_CLOCK_FREQ / 8000000) * 2;
        cfgr = RCC_CFGR_PLLSRC_HSI_DIV2 | ((div2 - 2) << RCC_CFGR_PLLMUL_Pos);
    }
    RCC->CFGR = cfgr;
    RCC->CR |= RCC_CR_PLLON;

    // Wait for PLL lock
    while (!(RCC->CR & RCC_CR_PLLRDY))
        ;

    // Switch system clock to PLL
    RCC->CFGR = cfgr | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL)
        ;

    // Setup CFGR3 register
    uint32_t cfgr3 = RCC_CFGR3_I2C1SW;

    RCC->CFGR3 = cfgr3;
}

// Enable high speed internal 14Mhz clock for ADC
static void
hsi14_setup(void)
{
    // Enable HSI14 for ADC
    RCC->CR2 = RCC_CR2_HSI14ON;
    while (!(RCC->CR2 & RCC_CR2_HSI14RDY))
        ;
}

// Main entry point - called from armcm_boot.c:ResetHandler()
void
armcm_main(void)
{
    SystemInit();

    // Set flash latency
    FLASH->ACR = (1 << FLASH_ACR_LATENCY_Pos) | FLASH_ACR_PRFTBE;

    // Configure main clock
    pll_setup();

    // Turn on hsi14 oscillator for ADC
    hsi14_setup();

    // Support pin remapping USB/CAN pins on low pinout stm32f042
#ifdef SYSCFG_CFGR1_PA11_PA12_RMP
    if (CONFIG_STM32_CANBUS_PA11_PA12_REMAP) {
        enable_pclock(SYSCFG_BASE);
        SYSCFG->CFGR1 |= SYSCFG_CFGR1_PA11_PA12_RMP;
    }
#endif

    timer_init();
    canboot_main();
}

uint16_t
read_magic_key(void)
{
    irq_disable();
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    RCC->APB1ENR;
    uint16_t val = RTC->BKP4R;
    if (val) {
    // clear the key
        PWR->CR |= PWR_CR_DBP;
        RTC->BKP4R = 0;
        PWR->CR &=~ PWR_CR_DBP;
    }
    RCC->APB1ENR &= ~RCC_APB1ENR_PWREN;
    irq_enable();
    return val;
}

void
set_magic_key(void)
{
    irq_disable();
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP4R = CONFIG_MAGIC_KEY;
    PWR->CR &=~ PWR_CR_DBP;
    RCC->APB1ENR &= ~RCC_APB1ENR_PWREN;
    irq_enable();
}

typedef void (*func_ptr)(void);

void
jump_to_application(void)
{
    func_ptr application = (func_ptr) *(volatile uint32_t*)
        (CONFIG_APPLICATION_START + 0x04);

    // Set the main stack pointer
    asm volatile ("MSR msp, %0" : : "r" (*(volatile uint32_t*)
        CONFIG_APPLICATION_START) : );

    // Jump to application
    application();
}
