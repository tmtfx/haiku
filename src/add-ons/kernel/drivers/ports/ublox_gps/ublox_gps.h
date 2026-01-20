/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef UBLOX_GPS_H
#define UBLOX_GPS_H

#include <Drivers.h>
#include <USB3.h>

#define UBLOX_VENDOR_ID     0x1546  // u-blox AG
#define UBLOX_7_PRODUCT_ID  0x01a7  // u-blox 7 (GNSS Receiver)

#define UBLOX_SET_UPDATE_RATE    (B_DEVICE_OP_CODES_END + 1)
#define UBLOX_SET_BAUD_RATE      (B_DEVICE_OP_CODES_END + 2)
#define UBLOX_GET_UPDATE_RATE    (B_DEVICE_OP_CODES_END + 3)
#define UBLOX_GET_BAUD_RATE      (B_DEVICE_OP_CODES_END + 4)

#define UBLOX_SAVE_CONFIG		(B_DEVICE_OP_CODES_END + 5)

struct ublox_device {
    usb_device      device;
    usb_pipe        bulk_in;
    usb_pipe        bulk_out;
    
    int32           open_count;    // Numero di handle aperti (atomic)
    bool            open;
    bool            removed;
    int             number;

    // Stato del dispositivo (Cache per Snooping)
    uint16          current_rate;
    uint32          current_baud;

    // Gestione trasferimenti USB
    sem_id          read_sem;
    status_t        transfer_status;
    size_t          actual_length;
};

#endif
