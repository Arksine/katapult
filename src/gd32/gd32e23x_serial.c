// Hardware serial support for GD32E23x
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h"
#include "board/armcm_boot.h"
#include "board/serial_irq.h"
#include "command.h"
#include "gd32e23x_internal.h"
#include "sched.h"

#if CONFIG_GD32E230_SERIAL_USART1_PA2_PA3
#define GPIO_Rx GPIO('A', 3)
#define GPIO_Tx GPIO('A', 2)
#define USARTx USART1
#define USARTx_IRQn USART1_IRQn
#define USARTx_PCLK RCU_USART1
#define USARTx_IRQHandler USART1_IRQHandler
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA3,PA2");
#elif CONFIG_GD32E230_SERIAL_USART0_PA9_PA10
#define GPIO_Rx GPIO('A', 10)
#define GPIO_Tx GPIO('A', 9)
#define USARTx USART0
#define USARTx_IRQn USART0_IRQn
#define USARTx_PCLK RCU_USART0
#define USARTx_IRQHandler USART0_IRQHandler
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA10,PA9");
#else
#error Unknown GD32E230 serial mapping
#endif

void USARTx_IRQHandler(void)
{
    if (USART_STAT(USARTx) & USART_STAT_RBNE)
        serial_rx_byte(USART_RDATA(USARTx) & 0xff);
    if ((USART_STAT(USARTx) & USART_STAT_TBE)
        && (USART_CTL0(USARTx) & USART_CTL0_TBEIE)) {
        uint8_t data;
        if (serial_get_tx_byte(&data))
            USART_CTL0(USARTx) &= ~USART_CTL0_TBEIE;
        else
            USART_TDATA(USARTx) = data;
    }
}

void serial_enable_tx_irq(void) { USART_CTL0(USARTx) |= USART_CTL0_TBEIE; }

void serial_init(void)
{
    gpio_peripheral(GPIO_Rx, GPIO_AF_1, 1);
    gpio_peripheral(GPIO_Tx, GPIO_AF_1, 1);
    enable_pclock(USARTx_PCLK);
    USART_CTL0(USARTx) = 0;
    USART_CTL1(USARTx) = 0;
    uint32_t div = (CONFIG_CLOCK_FREQ + CONFIG_SERIAL_BAUD / 2)
                   / CONFIG_SERIAL_BAUD;
    USART_BAUD(USARTx) = div;
    USART_CTL0(USARTx) = USART_CTL0_UEN | USART_CTL0_REN
                         | USART_CTL0_TEN | USART_CTL0_RBNEIE;
    armcm_enable_irq(USARTx_IRQHandler, USARTx_IRQn, 0);
}
DECL_INIT(serial_init);
