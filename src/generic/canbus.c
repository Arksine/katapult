// Generic handling of serial over CAN support
//
// Copyright (C) 2019 Eug Krashtan <eug.krashtan@gmail.com>
// Copyright (C) 2020 Pontus Borg <glpontus@gmail.com>
// Copyright (C) 2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "canbus.h" // canbus_set_uuid
#include "command.h" // DECL_TASK
#include "sched.h" // sched_wake_task

static uint32_t canbus_assigned_id;
static uint8_t canbus_uuid[CANBUS_UUID_LEN];


/****************************************************************
 * Data transmission over CAN
 ****************************************************************/

static struct task_wake canbus_tx_wake;
static uint8_t transmit_buf[96], transmit_pos, transmit_max;

void
canbus_notify_tx(void)
{
    sched_wake_task(&canbus_tx_wake);
}

void
canbus_tx_task(void)
{
    if (!sched_check_wake(&canbus_tx_wake))
        return;
    uint32_t id = canbus_assigned_id;
    if (!id) {
        transmit_pos = transmit_max = 0;
        return;
    }
    uint32_t tpos = transmit_pos, tmax = transmit_max;
    for (;;) {
        int avail = tmax - tpos, now = avail > 8 ? 8 : avail;
        if (avail <= 0)
            break;
        int ret = canbus_send(id + 1, now, &transmit_buf[tpos]);
        if (ret <= 0)
            break;
        tpos += now;
    }
    transmit_pos = tpos;
}
DECL_TASK(canbus_tx_task);

// Encode and transmit a "response" message
void
console_process_tx(uint8_t *data, uint32_t size)
{
    // Verify space for message
    uint32_t tpos = transmit_pos, tmax = transmit_max;
    if (tpos >= tmax)
        transmit_pos = transmit_max = tpos = tmax = 0;

    if (tmax + size > sizeof(transmit_buf)) {
        if (tmax + size - tpos > sizeof(transmit_buf))
            // Not enough space for message
            return;
        // Move buffer
        tmax -= tpos;
        memmove(&transmit_buf[0], &transmit_buf[tpos], tmax);
        transmit_pos = tpos = 0;
        transmit_max = tmax;
    }

    // Generate message
    memcpy(&transmit_buf[tmax], data, size);

    // Start message transmit
    transmit_max = tmax + size;
    canbus_notify_tx();
}


/****************************************************************
 * CAN "admin" command handling
 ****************************************************************/

// Available commands and responses
#define CANBUS_CMD_QUERY_UNASSIGNED 0x00
#define CANBUS_CMD_SET_CANBOOT_NODEID 0x11
#define CANBUS_CMD_CLEAR_CANBOOT_NODEID 0x12
#define CANBUS_RESP_NEED_NODEID 0x20

// Helper to verify a UUID in a command matches this chip's UUID
static int
can_check_uuid(uint32_t id, uint32_t len, uint8_t *data)
{
    return len >= 7 && memcmp(&data[1], canbus_uuid, sizeof(canbus_uuid)) == 0;
}

static uint32_t
can_decode_nodeid(int nodeid)
{
    return (nodeid << 1) + 0x100;
}

static void
can_process_query_unassigned(uint32_t id, uint32_t len, uint8_t *data)
{
    if (canbus_assigned_id)
        return;
    uint8_t send[8];
    send[0] = CANBUS_RESP_NEED_NODEID;
    memcpy(&send[1], canbus_uuid, sizeof(canbus_uuid));
    send[7] = CANBUS_CMD_SET_CANBOOT_NODEID;
    // Send with retry
    for (;;) {
        int ret = canbus_send(CANBUS_ID_ADMIN_RESP, 8, send);
        if (ret >= 0)
            return;
    }
}

static void
can_process_clear_canboot_nodeid(void)
{
    canbus_assigned_id = 0;
    canbus_set_filter(canbus_assigned_id);
}

static void
can_id_conflict(void)
{
    canbus_assigned_id = 0;
    canbus_set_filter(canbus_assigned_id);
    // TODO: We should likely do something here, such as report back
}

static void
can_process_set_canboot_nodeid(uint32_t id, uint32_t len, uint8_t *data)
{
    if (len < 8)
        return;
    uint32_t newid = can_decode_nodeid(data[7]);
    if (can_check_uuid(id, len, data)) {
        if (newid != canbus_assigned_id) {
            canbus_assigned_id = newid;
            canbus_set_filter(canbus_assigned_id);
        }
    } else if (newid == canbus_assigned_id) {
        can_id_conflict();
    }
}

// Handle an "admin" command
static void
can_process(uint32_t id, uint32_t len, uint8_t *data)
{
    if (!len)
        return;
    switch (data[0]) {
    case CANBUS_CMD_QUERY_UNASSIGNED:
        can_process_query_unassigned(id, len, data);
        break;
    case CANBUS_CMD_SET_CANBOOT_NODEID:
        can_process_set_canboot_nodeid(id, len, data);
        break;
    case CANBUS_CMD_CLEAR_CANBOOT_NODEID:
        can_process_clear_canboot_nodeid();
        break;
    }
}


/****************************************************************
 * CAN packet reading
 ****************************************************************/

static struct task_wake canbus_rx_wake;

void
canbus_notify_rx(void)
{
    sched_wake_task(&canbus_rx_wake);
}

// Handle incoming data (called from IRQ handler)
void
canbus_process_data(uint32_t id, uint32_t len, uint8_t *data)
{
    if (!id || id != canbus_assigned_id)
        return;
    console_process_rx(data, len);
    canbus_notify_rx();
}

// Task to process incoming commands and admin messages
void
canbus_rx_task(void)
{
    if (!sched_check_wake(&canbus_rx_wake))
        return;

    // Read any pending CAN packets
    for (;;) {
        uint8_t data[8];
        uint32_t id;
        int ret = canbus_read(&id, data);
        if (ret < 0)
            break;
        if (id && id == canbus_assigned_id + 1)
            can_id_conflict();
        else if (id == CANBUS_ID_ADMIN)
            can_process(id, ret, data);
    }
}
DECL_TASK(canbus_rx_task);


/****************************************************************
 * Setup and shutdown
 ****************************************************************/

void
command_get_canbus_id(uint32_t *data)
{
    uint32_t out[5] = {};
    memcpy(&out[2], canbus_uuid, 6);
    command_respond_ack(CMD_GET_CANBUS_ID, out, ARRAY_SIZE(out));
}

void
canbus_set_uuid(void *uuid)
{
    memcpy(canbus_uuid, uuid, sizeof(canbus_uuid));
    canbus_notify_rx();
}

void
canbus_shutdown(void)
{
    canbus_notify_tx();
    canbus_notify_rx();
}
DECL_SHUTDOWN(canbus_shutdown);
