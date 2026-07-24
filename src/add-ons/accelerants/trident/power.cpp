/*
 * Copyright 1992-2003, Alan Hourihane. All rights reserved.
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alan Hourihane <alanh@fairlite.demon.co.uk>
 *		Fabio Tomat <f.t.public@gmail.com>
 *		Gemini CLI <gemini-cli@google.com>
 */


#include <Accelerant.h>
#include "accel.h"
#include <unistd.h>


// Helper logging function for power registers
static inline void
write_crtc_reg_logged(const char* name, uint8 index, uint8 value)
{
	uint8 old_val = read_crtc_reg(index);
	write_crtc_reg(index, value);
	uint8 new_val = read_crtc_reg(index);
	debug_printf("Trident_PWR: CR%02X (%s) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
		index, name, old_val, value, new_val);
}

static inline void
write_seq_reg_logged(const char* name, uint8 index, uint8 value)
{
	uint8 old_val = read_seq_reg(index);
	write_seq_reg(index, value);
	uint8 new_val = read_seq_reg(index);
	debug_printf("Trident_PWR: SR%02X (%s) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
		index, name, old_val, value, new_val);
}


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

	debug_printf("Trident_PWR: trident_set_dpms_mode started. Mode=%d\n", mode);

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Unlock CRTC registers with MMIO enabled (CR39 = 0x87)
	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87);

	// Unlock DPMS registers (SR0E = 0xC2)
	//write_seq_reg_logged("NewMode1", 0x0E, 0xC2);
	write_seq_reg(NewMode1, 0xC2); //0x0E

	// Read/Write PMCont via physical DAC ports 0x3C8 / 0x3C6 (BAR 1 base offset 0!)
	write_reg8(0x3C8, 0x04);
	uint8 old_pmCont = read_reg8(0x3C6);
	uint8 pmCont = old_pmCont & 0xFC;

	// Read/Write DPMSCont via standard VGA Graphics Controller index 0x23
	write_vga_reg(0x3CE, 0x23);
	uint8 old_dpmsCont = read_vga_reg(0x3CF);
	uint8 dpmsCont = old_dpmsCont & 0xFC;

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

	// Write back the new DPMSCont to Graphics Controller index 0x23
	write_vga_reg(0x3CE, 0x23);
	write_vga_reg(0x3CF, dpmsCont);
	uint8 new_dpmsCont = read_vga_reg(0x3CF);

	// Write back the new PMCont directly to physical BAR 1 offset 0x3C6
	write_reg8(0x3C8, 0x04);
	write_reg8(0x3C6, pmCont);
	uint8 new_pmCont = read_reg8(0x3C6);

	//debug_printf("Trident_PWR: DPMSCont (GR23) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
	//	old_dpmsCont, dpmsCont, new_dpmsCont);
	//debug_printf("Trident_PWR: PMCont (0x3C6) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
	//	old_pmCont, pmCont, new_pmCont);

	// Keep registers unlocked for multi-call stability
	//write_seq_reg_logged("NewMode1", 0x0E, 0x80);
	write_seq_reg(NewMode1, 0x80);

	si.dpmsMode = mode;
	return B_OK;
}

} // extern "C"
