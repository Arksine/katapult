// Generic reset command handler for ARM Cortex-M boards
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_FLASH_BOOT_ADDRESS
#include "armcm_reset.h" // try_request_canboot
#include "board/internal.h" // NVIC_SystemReset
#include "board/irq.h" // irq_disable
#include "board/misc.h" // try_request_canboot
#include "canboot.h" // REQUEST_CANBOOT

void
try_request_canboot(void)
{
    uint32_t *bl_vectors = (uint32_t *)(CONFIG_FLASH_BOOT_ADDRESS);
    uint64_t *req_sig = (uint64_t *)bl_vectors[0];
    irq_disable();
    *req_sig = REQUEST_CANBOOT;
#if __CORTEX_M >= 7
    SCB_CleanDCache_by_Addr((void*)req_sig, sizeof(*req_sig));
#endif
    NVIC_SystemReset();
}
