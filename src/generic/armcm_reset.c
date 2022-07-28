// Generic reset command handler for ARM Cortex-M boards
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_FLASH_START
#include "board/internal.h" // NVIC_SystemReset
#include "board/irq.h" // irq_disable
#include "board/misc.h" // try_request_canboot

void
try_request_canboot(void)
{
    uint32_t *bl_vectors = (uint32_t *)(CONFIG_FLASH_START & 0xFF000000);
    uint64_t *req_sig = (uint64_t *)bl_vectors[0];
    irq_disable();
    *req_sig = REQUEST_CANBOOT;
    NVIC_SystemReset();
}
