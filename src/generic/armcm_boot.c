// ARM Cortex-M vector table and initial bootup handling
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "armcm_boot.h" // DECL_ARMCM_IRQ
#include "autoconf.h" // CONFIG_MCU
#include "board/internal.h" // SysTick


// Symbols created by armcm_link.lds.S linker script
extern uint32_t _data_start, _data_end, _data_flash;
extern uint32_t _bss_start, _bss_end, _stack_start;
extern uint32_t _stack_end;

/****************************************************************
 * Basic interrupt handlers
 ****************************************************************/

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
}

void __noreturn __visible
reset_handler_stage_two(void)
{
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

#define CANBOOT_SIGNATURE 0x21746f6f426e6143 // CanBoot!

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
