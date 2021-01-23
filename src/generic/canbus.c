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
 * CAN command handling
 ****************************************************************/

// Helper to retry sending until successful
static void
canbus_send_blocking(uint32_t id, uint32_t len, uint8_t *data)
{
    for (;;) {
        int ret = canbus_send(id, len, data);
        if (ret >= 0)
            return;
    }
}

static void
can_process_ping(uint32_t id, uint32_t len, uint8_t *data)
{
    canbus_send_blocking(canbus_assigned_id + 1, 0, NULL);
}

static void
can_process_reset(uint32_t id, uint32_t len, uint8_t *data)
{
    /*uint32_t reset_id = data[0] | (data[1] << 8);
    if (reset_id == canbus_assigned_id)
        canbus_reboot();*/
}

static void
can_process_uuid(uint32_t id, uint32_t len, uint8_t *data)
{
    if (canbus_assigned_id)
        return;
    canbus_send_blocking(CANBUS_ID_UUID_RESP, sizeof(canbus_uuid), canbus_uuid);
}

static void
can_process_set_id(uint32_t id, uint32_t len, uint8_t *data)
{
    // compare my UUID with packet to check if this packet mine
    if (memcmp(&data[2], canbus_uuid, sizeof(canbus_uuid)) == 0) {
        canbus_assigned_id = data[0] | (data[1] << 8);
        canbus_set_dataport(canbus_assigned_id);
    }
}

static void
can_process(uint32_t id, uint32_t len, uint8_t *data)
{
    if (id == canbus_assigned_id) {
        if (len)
            canboot_process_rx(id, len, data);
        else
            can_process_ping(id, len, data);
    } else if (id == CANBUS_ID_UUID) {
        if (len)
            can_process_reset(id, len, data);
        else
            can_process_uuid(id, len, data);
    } else if (id==CANBUS_ID_SET) {
        can_process_set_id(id, len, data);
    }
}


/****************************************************************
 * CAN packet reading
 ****************************************************************/

static volatile uint8_t canbus_rx_wake;

void
canbus_notify_rx(void)
{
    canbus_rx_wake = 1;
}

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

    // Send initial message
    can_process_uuid(0, 0, NULL);
}
