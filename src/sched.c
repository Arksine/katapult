// Basic scheduling functions and startup code
//
// Copyright (C) 2016-2022  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/io.h" // readb
#include "board/misc.h" // jump_to_application
#include "bootentry.h" // bootentry_check
#include "sched.h" // sched_check_periodic

// Wrapper for Klipper compatibility
void
sched_wake_tasks(void)
{
}

// Note that a task is ready to run
void
sched_wake_task(struct task_wake *w)
{
    writeb(&w->wake, 1);
}

// Check if a task is ready to run (as indicated by sched_wake_task)
uint8_t
sched_check_wake(struct task_wake *w)
{
    if (!readb(&w->wake))
        return 0;
    writeb(&w->wake, 0);
    return 1;
}

// Init followed by main task dispatch loop
void
sched_main(void)
{
    if (!bootentry_check())
        jump_to_application();

    // Run all init functions marked with DECL_INIT()
    extern void ctr_run_initfuncs(void);
    ctr_run_initfuncs();

    for (;;) {
        // Run all task functions marked with DECL_TASK()
        extern void ctr_run_taskfuncs(void);
        ctr_run_taskfuncs();
    }
}
