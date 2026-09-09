// Copyright (C) 2026  Xiaoyue Cui <2508041672@qq.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef __GD32E23X_FLASH_H
#define __GD32E23X_FLASH_H
#include <stdint.h>
int flash_write_block(uint32_t block_address, uint32_t *data);
int flash_complete(void);
#endif
