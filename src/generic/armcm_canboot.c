// CanBoot specific entry code for ARM Cortex-M vector table and bootup
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "armcm_boot.h" // DECL_ARMCM_IRQ
#include "autoconf.h" // CONFIG_MCU
#include "board/internal.h" // SysTick
#include "board/irq.h" // irq_disable
#include "canboot.h" // get_bootup_code
#include "command.h" // DECL_CONSTANT_STR

// Export MCU type
DECL_CONSTANT_STR("MCU", CONFIG_MCU);

// Symbols created by armcm_link.lds.S linker script
extern uint32_t _data_start, _data_end, _data_flash;
extern uint32_t _bss_start, _bss_end, _stack_start;
extern uint32_t _stack_end;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored  "-Warray-bounds"

uint64_t
get_bootup_code(void)
{
    uint64_t *req_code = (void*)&_stack_end;
    return *req_code;
}

void
set_bootup_code(uint64_t code)
{
    uint64_t *req_code = (void*)&_stack_end;
    *req_code = code;
    barrier();
#if __CORTEX_M >= 7
    SCB_CleanDCache_by_Addr((void*)req_code, sizeof(*req_code));
#endif
}

#pragma GCC diagnostic pop

// Helper function to read area of flash
void
application_read_flash(uint32_t address, uint32_t *dest)
{
    memcpy(dest, (void*)address, CONFIG_BLOCK_SIZE);
}

// Check if the application flash area looks valid
int
application_check_valid(void)
{
    uint32_t *app = (void*)CONFIG_LAUNCH_APP_ADDRESS;
    return *app != 0 && *app != 0xffffffff;
}

// Jump to the main application (exiting the bootloader)
void
application_jump(void)
{
    irq_disable();
    set_bootup_code(REQUEST_START_APP);
    NVIC_SystemReset();
}

static void
start_application(void)
{
    set_bootup_code(0);
    uint32_t *vtor = (void*)CONFIG_LAUNCH_APP_ADDRESS;
#if __CORTEX_M > 0 || __VTOR_PRESENT
    SCB->VTOR = (uint32_t)vtor;
#endif
    asm volatile("MSR msp, %0\n    bx %1" : : "r"(vtor[0]), "r"(vtor[1]));
}

void __noreturn __visible
reset_handler_stage_two(void)
{
    uint64_t bootup_code = get_bootup_code();
    if (bootup_code == REQUEST_START_APP)
        start_application();

    // Copy global variables from flash to ram
    uint32_t count = (&_data_end - &_data_start) * 4;
    __builtin_memcpy(&_data_start, &_data_flash, count);

    // Clear the bss segment
    __builtin_memset(&_bss_start, 0, (&_bss_end - &_bss_start) * 4);

    barrier();

    // Initializing the C library isn't needed...
    //__libc_init_array();

    // Run the main board specific code
    armcm_main();

    // The armcm_main() call should not return
    for (;;)
        ;
}

// Initial code entry point - invoked by the processor after a reset
asm(".section .text.ResetHandler\n"
    ".balign 8\n"
    ".8byte " __stringify(CANBOOT_SIGNATURE) "\n"
    ".global ResetHandler\n"
    ".type ResetHandler, %function\n"
    "ResetHandler:\n"
    "    b reset_handler_stage_two\n"
    );
extern void ResetHandler();
DECL_ARMCM_IRQ(ResetHandler, -15);

// Code called for any undefined interrupts
void
DefaultHandler(void)
{
    for (;;)
        ;
}
