// Generic handling of serial over CAN support
//
// Copyright (C) 2019 Eug Krashtan <eug.krashtan@gmail.com>
// Copyright (C) 2020 Pontus Borg <glpontus@gmail.com>
// Copyright (C) 2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
#include "canbus.h" // canbus_set_uuid
#include "canboot_main.h"

static uint32_t canbus_assigned_id;
static uint8_t canbus_uuid[CANBUS_UUID_LEN];


/****************************************************************
 * Data transmission over CAN
 ****************************************************************/

static volatile uint8_t canbus_tx_wake;
static uint8_t transmit_buf[96], transmit_pos, transmit_max;

uint8_t
canbus_tx_clear(void)
{
    return transmit_pos >= transmit_max;
}

void
canbus_notify_tx(void)
{
    canbus_tx_wake = 1;
}

void
canbus_tx_task(void)
{
    if (!canbus_tx_wake)
        return;
    canbus_tx_wake = 0;
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

// Encode and transmit a "response" message
void
canboot_sendf(uint8_t* data, uint16_t size)
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
#define CANBUS_CMD_SET_NODEID 0x01
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
    return (nodeid << 1) + 0x200;
}

static void
can_process_query_unassigned(uint32_t id, uint32_t len, uint8_t *data)
{
    if (canbus_assigned_id)
        return;
    uint8_t send[8];
    send[0] = CANBUS_RESP_NEED_NODEID;
    memcpy(&send[1], canbus_uuid, sizeof(canbus_uuid));
    // Send with retry
    for (;;) {
        int ret = canbus_send(CANBUS_ID_ADMIN_RESP, 7, send);
        if (ret >= 0)
            return;
    }
}

static void
can_id_conflict(void)
{
    canbus_assigned_id = 0;
    canbus_set_filter(canbus_assigned_id);
    // TODO: We should likely do something here, such as report back
}

static void
can_process_set_nodeid(uint32_t id, uint32_t len, uint8_t *data)
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
    case CANBUS_CMD_SET_NODEID:
        can_process_set_nodeid(id, len, data);
        break;
    }
}


/****************************************************************
 * CAN packet reading
 ****************************************************************/

static volatile uint8_t canbus_rx_wake;

// Handle incoming data (called from IRQ handler)
void
canbus_process_data(uint32_t id, uint32_t len, uint8_t *data)
{
    if (!id || id != canbus_assigned_id)
        return;
    canboot_process_rx(id, len, data);
    canbus_notify_rx();
}

void
canbus_notify_rx(void)
{
    canbus_rx_wake = 1;
}

// Task to process incoming commands and admin messages
void
canbus_rx_task(void)
{
    if (!canbus_rx_wake)
        return;
    canbus_rx_wake = 0;

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

/****************************************************************
 * Setup and shutdown
 ****************************************************************/

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