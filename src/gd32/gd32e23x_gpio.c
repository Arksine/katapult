// GPIO functions on GD32E23x
//
// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/irq.h"
#include "command.h"
#include "gd32e23x_gpio.h"
#include "gd32e23x_internal.h"

static const uint32_t ports[] = { GPIOA, GPIOB, GPIOC, 0, 0, GPIOF };
static const uint32_t clocks[] = { RCU_GPIOA, RCU_GPIOB, RCU_GPIOC, 0, 0, RCU_GPIOF };

static uint32_t gpio_port(uint32_t pin)
{
    uint32_t p = GPIO2PORT(pin);
    if (p >= ARRAY_SIZE(ports) || !ports[p])
        shutdown("Invalid GPIO pin");
    enable_pclock(clocks[p]);
    return ports[p];
}

void gpio_peripheral(uint32_t pin, uint32_t af, int pullup)
{
    uint32_t port = gpio_port(pin), pos = pin & 15;
    uint32_t shift = pos * 2, afshift = (pos & 7) * 4;
    GPIO_CTL(port) = (GPIO_CTL(port) & ~(3U << shift)) | (2U << shift);
    GPIO_PUD(port) = (GPIO_PUD(port) & ~(3U << shift))
                     | ((pullup > 0 ? 1U : pullup < 0 ? 2U : 0U) << shift);
    GPIO_OMODE(port) &= ~GPIO2BIT(pin);
    volatile uint32_t *afr = (void*)(port + (pos < 8 ? 0x20 : 0x24));
    *afr = (*afr & ~(15U << afshift)) | (af << afshift);
}

void gpio_out_reset(struct gpio_out g, uint32_t val)
{
    uint32_t port = gpio_port(g.pin), bit = GPIO2BIT(g.pin);
    uint32_t shift = (g.pin & 15) * 2;
    if (val) GPIO_BOP(port) = bit; else GPIO_BC(port) = bit;
    GPIO_OMODE(port) &= ~bit;
    GPIO_CTL(port) = (GPIO_CTL(port) & ~(3U << shift)) | (1U << shift);
}

struct gpio_out gpio_out_setup(uint32_t pin, uint32_t val)
{
    struct gpio_out g = { .pin = pin };
    gpio_out_reset(g, val);
    return g;
}

void gpio_out_write(struct gpio_out g, uint32_t val)
{
    uint32_t port = gpio_port(g.pin), bit = GPIO2BIT(g.pin);
    if (val) GPIO_BOP(port) = bit; else GPIO_BC(port) = bit;
}

void gpio_out_toggle_noirq(struct gpio_out g)
{
    GPIO_TG(gpio_port(g.pin)) = GPIO2BIT(g.pin);
}

void gpio_out_toggle(struct gpio_out g)
{
    irqstatus_t flags = irq_save();
    gpio_out_toggle_noirq(g);
    irq_restore(flags);
}

void gpio_in_reset(struct gpio_in g, int32_t pullup)
{
    uint32_t port = gpio_port(g.pin), shift = (g.pin & 15) * 2;
    GPIO_CTL(port) &= ~(3U << shift);
    GPIO_PUD(port) = (GPIO_PUD(port) & ~(3U << shift))
                     | ((pullup > 0 ? 1U : pullup < 0 ? 2U : 0U) << shift);
}

struct gpio_in gpio_in_setup(uint32_t pin, int32_t pullup)
{
    struct gpio_in g = { .pin = pin };
    gpio_in_reset(g, pullup);
    return g;
}

uint8_t gpio_in_read(struct gpio_in g)
{
    return !!(GPIO_ISTAT(gpio_port(g.pin)) & GPIO2BIT(g.pin));
}
