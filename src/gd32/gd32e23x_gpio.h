// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef __GD32E23X_GPIO_H
#define __GD32E23X_GPIO_H
#include <stdint.h>
struct gpio_out { uint8_t pin; };
struct gpio_in { uint8_t pin; };
struct gpio_out gpio_out_setup(uint32_t pin, uint32_t val);
void gpio_out_reset(struct gpio_out g, uint32_t val);
void gpio_out_toggle_noirq(struct gpio_out g);
void gpio_out_toggle(struct gpio_out g);
void gpio_out_write(struct gpio_out g, uint32_t val);
struct gpio_in gpio_in_setup(uint32_t pin, int32_t pull_up);
void gpio_in_reset(struct gpio_in g, int32_t pull_up);
uint8_t gpio_in_read(struct gpio_in g);
#endif
