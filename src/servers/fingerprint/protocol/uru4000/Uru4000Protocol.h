/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Register map and mode/irq constants for the DigitalPersona URU4000 /
 * Microsoft Fingerprint Reader chipset, ported from libfprint's uru4000.c
 * driver (LGPL-2.1) protocol knowledge. This header only, no code is
 * copied verbatim from libusb-based call sites.
 */
#ifndef URU4000_PROTOCOL_H
#define URU4000_PROTOCOL_H


#include <SupportDefs.h>


namespace Uru4000 {

enum {
	REG_HWSTAT = 0x07,
	REG_SCRAMBLE_DATA_INDEX = 0x33,
	REG_SCRAMBLE_DATA_KEY = 0x34,
	REG_MODE = 0x4e,
	REG_DEVICE_INFO = 0xf0,
};

enum {
	MODE_INIT = 0x00,
	MODE_AWAIT_FINGER_ON = 0x10,
	MODE_AWAIT_FINGER_OFF = 0x12,
	MODE_CAPTURE = 0x20,
	MODE_CAPTURE_AUX = 0x30,
	MODE_OFF = 0x70,
	MODE_READY = 0x80,
};

enum {
	IRQDATA_SCANPWR_ON = 0x56aa,
	IRQDATA_FINGER_ON = 0x0101,
	IRQDATA_FINGER_OFF = 0x0200,
	IRQDATA_DEATH = 0x0800,
};

const uint8 kVendorRequest = 0x04;
const uint8 kCtrlIn = 0x80 | 0x40;		// device-to-host | vendor | device
const uint8 kCtrlOut = 0x00 | 0x40;	// host-to-device | vendor | device

const int kImageHeight = 290;
const int kImageWidth = 384;
const size_t kImageHeaderSize = 4 + 2 + 1 + 9 + 15 * 2 + 18;
const size_t kImageStructSize = kImageHeaderSize
	+ (size_t)kImageHeight * kImageWidth;

const uint16 kVendorID = 0x045e;
const uint16 kProductID = 0x00bd;

} // namespace Uru4000


#endif // URU4000_PROTOCOL_H
