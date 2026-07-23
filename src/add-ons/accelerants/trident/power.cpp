/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include <Accelerant.h>
#include "accel.h"


extern "C" {

uint32
trident_dpms_mode(void)
{
	return gInfo.sharedInfo->dpmsMode;
}


uint32
trident_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}


status_t
trident_set_dpms_mode(uint32 mode)
{
	SharedInfo& si = *gInfo.sharedInfo;

	// Protect from modifying if mode is identical
	if (si.dpmsMode == mode)
		return B_OK;

	write_crtc_reg(0x39, 0x80); // Unlock CRTC

	uint8 temp = read_seq_reg(0x0E);
	write_seq_reg(0x0E, 0xC2); // Unlock DPMS registers

	// Read/Write PMCont via 0x3C8 / 0x3C6
	write_vga_reg(0x3C8, 0x04);
	uint8 pmCont = read_vga_reg(0x3C6) & 0xFC;

	// Read/Write DPMSCont via 0x3CE / 0x3CF (index 0x23)
	write_vga_reg(0x3CE, 0x23);
	uint8 dpmsCont = read_vga_reg(0x3CF) & 0xFC;

	switch (mode) {
		case B_DPMS_ON:
			pmCont |= 0x03;
			dpmsCont |= 0x00;
			break;
		case B_DPMS_STAND_BY:
			pmCont |= 0x02;
			dpmsCont |= 0x01;
			break;
		case B_DPMS_SUSPEND:
			pmCont |= 0x02;
			dpmsCont |= 0x02;
			break;
		case B_DPMS_OFF:
			pmCont |= 0x00;
			dpmsCont |= 0x03;
			break;
		default:
			return B_BAD_VALUE;
	}

	write_vga_reg(0x3CF, dpmsCont);
	write_vga_reg(0x3C8, 0x04);
	write_vga_reg(0x3C6, pmCont);

	// Restore SR0E
	write_seq_reg(0x0E, temp);

	si.dpmsMode = mode;
	return B_OK;
}

} // extern "C"
