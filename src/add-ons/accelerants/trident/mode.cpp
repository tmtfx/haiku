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


#include "accel.h"

#include <create_display_modes.h>
#include <string.h>
#include <unistd.h>

bool enable_log = false;

// Redefine standard X.org macros to log everything before and after writes
#undef OUTW_3C4
#undef OUTW_3CE
#undef OUTW_3x4

#define OUTW_3C4(reg) \
	do { \
		if (enable_log) { \
			uint8 old_val = read_seq_reg(reg); \
			write_seq_reg(reg, tridentReg->tridentRegs3C4[reg]); \
			uint8 new_val = read_seq_reg(reg); \
			debug_printf("Trident_REG: SR%02X Old=0x%02X, Write=0x%02X, Readback=0x%02X\n", \
				reg, old_val, tridentReg->tridentRegs3C4[reg], new_val); \
		} else { \
			write_seq_reg(reg, tridentReg->tridentRegs3C4[reg]); \
		} \
	} while (0)

#define OUTW_3CE(reg) \
	do { \
		if (enable_log) { \
			write_vga_reg(0x3CE, reg); \
			uint8 old_val = read_vga_reg(0x3CF); \
			write_vga_reg(0x3CF, tridentReg->tridentRegs3CE[reg]); \
			write_vga_reg(0x3CE, reg); \
			uint8 new_val = read_vga_reg(0x3CF); \
			debug_printf("Trident_REG: GR%02X Old=0x%02X, Write=0x%02X, Readback=0x%02X\n", \
				reg, old_val, tridentReg->tridentRegs3CE[reg], new_val); \
		} else { \
			write_vga_reg(0x3CE, reg); \
			write_vga_reg(0x3CF, tridentReg->tridentRegs3CE[reg]); \
		} \
	} while (0)

#define OUTW_3x4(reg) \
	do { \
		if (enable_log) { \
			uint8 old_val = read_crtc_reg(reg); \
			write_crtc_reg(reg, tridentReg->tridentRegs3x4[reg]); \
			uint8 new_val = read_crtc_reg(reg); \
			debug_printf("Trident_REG: CR%02X Old=0x%02X, Write=0x%02X, Readback=0x%02X\n", \
				reg, old_val, tridentReg->tridentRegs3x4[reg], new_val); \
		} else { \
			write_crtc_reg(reg, tridentReg->tridentRegs3x4[reg]); \
		} \
	} while (0)


static bool
IsThereEnoughFBMemory(const display_mode* mode, uint32 bitsPerPixel)
{
	uint32 maxWidth = mode->virtual_width;
	if (mode->timing.h_display > maxWidth)
		maxWidth = mode->timing.h_display;

	uint32 maxHeight = mode->virtual_height;
	if (mode->timing.v_display > maxHeight)
		maxHeight = mode->timing.v_display;

	uint32 bytesPerPixel = (bitsPerPixel + 7) / 8;

	return (maxWidth * maxHeight * bytesPerPixel < gInfo.sharedInfo->maxFrameBufferSize);
}


bool
IsModeUsable(display_mode* mode)
{
	uint32 bitsPerPixel = 0;
	uint32 maxPixelClock = 230000; // max pixel clock for Blade3D (KHz)

	switch (mode->space) {
		case B_CMAP8:
			bitsPerPixel = 8;
			break;
		case B_RGB15:
		case B_RGB16:
			bitsPerPixel = 16;
			break;
		case B_RGB32:
			bitsPerPixel = 32;
			break;
		default:
			return false;
	}

	if (!IsThereEnoughFBMemory(mode, bitsPerPixel))
		return false;

	if (mode->timing.pixel_clock > maxPixelClock)
		return false;

	// Enforce hardware-supported resolutions and depths from the user manual
	uint32 w = mode->timing.h_display;
	uint32 h = mode->timing.v_display;

	if (w == 640 && h == 480) {
		// Supports 8, 16, 32
	} else if (w == 800 && h == 600) {
		// Supports 8, 16, 32
	} else if (w == 1024 && h == 768) {
		// Supports 8, 16, 32
	} else if (w == 1280 && h == 1024) {
		if (bitsPerPixel == 32)
			return false; // only 8bit and 16bit supported
	} else if (w == 1600 && h == 1200) {
		if (bitsPerPixel == 32)
			return false; // only 8bit and 16bit supported
	} else {
		// Other non-standard resolutions (e.g., 1366x768) are not hardware supported
		return false;
	}

	return true;
}


status_t
CreateModeList(void)
{
	SharedInfo& si = *gInfo.sharedInfo;

	si.bHaveEDID = false;

	// Attempt to get EDID info from the driver
	edid1_raw rawEdid;
	if (ioctl(gInfo.deviceFileDesc, TRIDENT_GET_EDID, &rawEdid, sizeof(rawEdid)) == B_OK) {
		if (rawEdid.version.version == 1 && rawEdid.version.revision <= 4) {
			edid_decode(&si.edidInfo, &rawEdid);
			si.bHaveEDID = true;
		}
	}

	if (si.bHaveEDID) {
#ifdef ENABLE_DEBUG_TRACE
		edid_dump(&(si.edidInfo));
#endif
	} else {
		TRACE("CreateModeList(); Unable to get EDID info\n");
	}

	// Supported color spaces on Trident Blade3D
	static const color_space spaces[] = {
		B_RGB32,
		B_RGB16,
		B_CMAP8
	};
	static const uint32 spacesCount = sizeof(spaces) / sizeof(spaces[0]);

	display_mode* list;
	uint32 count = 0;
	area_id listArea;

	listArea = create_display_modes("Trident modes",
		si.bHaveEDID ? &si.edidInfo : NULL,
		NULL, 0, spaces, spacesCount, 
		(check_display_mode_hook)IsModeUsable, &list, &count);

	if (listArea < 0)
		return listArea;

	si.modeArea = gInfo.modeListArea = listArea;
	si.modeCount = count;
	gInfo.modeList = list;

	return B_OK;
}


status_t
ProposeDisplayMode(display_mode *target, const display_mode *low,
    const display_mode *high)
{
	(void)low;
	(void)high;

	if (enable_log)
		debug_printf("ProposeDisplayMode() %dx%d, pixel clock: %d kHz, space: 0x%X\n",
			target->timing.h_display, target->timing.v_display,
			target->timing.pixel_clock, target->space);

	SharedInfo& si = *(gInfo.sharedInfo);

	uint32 bytesPerPixel = 0;
	switch (target->space) {
		case B_CMAP8:  bytesPerPixel = 1; break;
		case B_RGB15:
		case B_RGB16:  bytesPerPixel = 2; break;
		case B_RGB32:  bytesPerPixel = 4; break;
		default:
			return B_BAD_VALUE;
	}

	uint32 requiredMemory = target->timing.h_display * target->timing.v_display * bytesPerPixel;
	if (si.videoMemSize > 0 && requiredMemory > si.videoMemSize)
		return B_BAD_VALUE;

	uint32 modeCount = si.modeCount;
	for (uint32 j = 0; j < modeCount; j++) {
		display_mode& mode = gInfo.modeList[j];

		if (target->timing.h_display == mode.timing.h_display
			&& target->timing.v_display == mode.timing.v_display
			&& target->space == mode.space) {
			*target = mode;
			return B_OK;
		}
	}

	if (modeCount > 0) {
		*target = gInfo.modeList[0];
		return B_OK;
	}

	return B_BAD_VALUE;
}

/*
static void
WriteClockReg(uint16 port, uint8 value)
{
	// Write via MMIO
	write_reg8(port, value);

	// Also write via PIO for maximum compatibility and safety
	TridentGetSetPIO gsp;
	gsp.magic = TRIDENT_PRIVATE_DATA_MAGIC;
	gsp.offset = port;
	gsp.size = 1;
	gsp.value = value;
	ioctl(gInfo.deviceFileDesc, TRIDENT_SET_PIO, &gsp, sizeof(gsp));
}


static uint8
ReadClockReg(uint16 port)
{
	// Attempt MMIO read first
	uint8 val = read_reg8(port);
	return val;
}
*/

static void
CalculateTridentPLL(uint32 clock, uint8& sr19, uint8& sr1a)
{
	double target = clock; // in kHz
	double f_ref = 14318.18; // kHz
	double best_error = 1e10;
	uint32 best_reg_n = 0;
	uint32 best_reg_m = 0;
	uint32 best_p = 0;

	for (uint32 p = 0; p <= 3; p++) {
		uint32 div = 1 << p;
		for (uint32 m_reg = 0; m_reg <= 63; m_reg++) {
			uint32 m_hw = m_reg + 2; // actual denominator is M_reg + 2
			// target = f_ref * (N_reg + 8) / (m_hw * div)
			double n_approx = target * m_hw * div / f_ref;
			int32 n_reg = (int32)(n_approx + 0.5) - 8; // subtract 8 to get register value

			if (n_reg < 0 || n_reg > 255)
				continue;

			uint32 n_hw = n_reg + 8;
			double actual = f_ref * n_hw / (m_hw * div);
			double error = actual - target;
			if (error < 0)
				error = -error;

			if (error < best_error) {
				best_error = error;
				best_reg_n = n_reg;
				best_reg_m = m_reg;
				best_p = p;
			}
		}
	}

	sr19 = (uint8)best_reg_n;
	sr1a = (uint8)((best_p << 6) | (best_reg_m & 0x3F));
}


status_t 
SetDisplayMode(display_mode* pMode)
{
	SharedInfo& si = *gInfo.sharedInfo;
	DisplayModeEx mode;
	(display_mode&)mode = *pMode;

	switch (mode.space) {
		case B_CMAP8:
			mode.bpp = 8;
			break;
		case B_RGB15:
		case B_RGB16:
			mode.bpp = 16;
			break;
		case B_RGB32:
			mode.bpp = 32;
			break;
		default:
			return B_BAD_VALUE;
	}

	if (ProposeDisplayMode(&mode, pMode, pMode) != B_OK)
		return B_BAD_VALUE;

	// Align virtual width to a multiple of 8 pixels to prevent line skewing
	mode.virtual_width = (mode.timing.h_display + 7) & ~7;
	int bytesPerPixel = (mode.bpp + 7) / 8;
	mode.bytesPerRow = mode.virtual_width * bytesPerPixel;

	if (!IsThereEnoughFBMemory(&mode, mode.bpp))
		return B_NO_MEMORY;

	if (enable_log)
		debug_printf("Trident_ACC: SetDisplayMode starting for %dx%d, virtual %dx%d, %d bpp, bytesPerRow %d\n",
			mode.timing.h_display, mode.timing.v_display,
			mode.virtual_width, mode.virtual_height, mode.bpp, mode.bytesPerRow);

	// Enable MMIO at the very start of SetDisplayMode so we can read the original registers correctly
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Local structure to hold registers matching the X.org layout
	TridentRegRec regRec = {};
	TridentRegPtr tridentReg = &regRec;

	int offset = 0;
	uint32 clock = mode.timing.pixel_clock;

	// 1. Initialize register structure unprotect and default settings (TridentInit)
	tridentReg->tridentRegs3x4[PixelBusReg] = 0x00;
	tridentReg->tridentRegsDAC[0x00] = 0x00;
	tridentReg->tridentRegs3C4[NewMode2] = 0x20;
	tridentReg->tridentRegs3CE[MiscExtFunc] = read_gfx_reg(MiscExtFunc) & 0xF0;
	tridentReg->tridentRegs3x4[GraphEngReg] = 0x00;
	tridentReg->tridentRegs3x4[PreEndControl] = 0;
	tridentReg->tridentRegs3x4[PreEndFetch] = 0;

	// Program CR27 (CRTHiOrd) for high-order vertical timing bits
	int v_total = mode.timing.v_total - 2;
	int v_display = mode.timing.v_display - 1;
	int v_sync_start = mode.timing.v_sync_start;
	int v_sync_end = mode.timing.v_sync_end;
	int v_blank_start = v_display;
	int v_blank_end = v_total;

	tridentReg->tridentRegs3x4[CRTHiOrd] = (((v_blank_end - 1) & 0x400) >> 4)
		| (((v_total - 2) & 0x400) >> 3)
		| ((v_sync_start & 0x400) >> 5)
		| (((v_display - 1) & 0x400) >> 6)
		| 0x08;

	// Program CR2B (HorizOverflow) for high-order horizontal timing bits
	tridentReg->tridentRegs3x4[HorizOverflow] = ((mode.timing.h_total & 0x800) >> 11)
		| (((mode.timing.h_display + 1) & 0x800) >> 7);

	// PreEndControl & PreEndFetch calculation
	int mul = mode.bpp >> 3;
	if (!mul) mul = 1;
	int val = (mode.timing.h_display * mul / 8) + 16;
	tridentReg->tridentRegs3x4[PreEndControl] = ((val >> 8) < 2 ? 2 : 0) | ((val >> 8) & 0x01);
	tridentReg->tridentRegs3x4[PreEndFetch] = val & 0xff;

	// Chipset specific: Blade3D timing registers
	tridentReg->tridentRegs3x4[RAMDACTiming] = read_crtc_reg(RAMDACTiming) | 0x0F;
	tridentReg->tridentRegs3CE[MiscExtFunc] |= 0x10;
	if (!tridentReg->tridentRegs3x4[PreEndControl])
		tridentReg->tridentRegs3x4[PreEndControl] = 0x01;
	if (!tridentReg->tridentRegs3x4[PreEndFetch])
		tridentReg->tridentRegs3x4[PreEndFetch] = 0xFF;

	// Disable stretching/scaler by default
	tridentReg->tridentRegs3CE[VertStretch] = 0x00;
	tridentReg->tridentRegs3CE[HorStretch] = 0x00;

	// BPP specific registers
	switch (mode.bpp) {
		case 8:
			tridentReg->tridentRegs3CE[MiscExtFunc] |= 0x02;
			offset = mode.virtual_width >> 3;
			break;
		case 16:
			tridentReg->tridentRegs3CE[MiscExtFunc] |= 0x02;
			offset = mode.virtual_width >> 2;
			tridentReg->tridentRegsDAC[0x00] = 0x30;
			tridentReg->tridentRegs3x4[PixelBusReg] = 0x05; // 0x04 | 0x01
			break;
		case 32:
			tridentReg->tridentRegs3CE[MiscExtFunc] |= 0x02;
			tridentReg->tridentRegs3CE[MiscExtFunc] |= 0x08; // clock divisor by 2
			clock *= 2; // clock is doubled for 32bpp modes
			offset = mode.virtual_width >> 1;
			tridentReg->tridentRegs3x4[PixelBusReg] = 0x09;
			tridentReg->tridentRegsDAC[0x00] = 0xD0;
			break;
	}
	tridentReg->tridentRegs3x4[Offset] = offset & 0xFF;

	// Set clock registers
	uint8 clk_a = 0, clk_b = 0;
	CalculateTridentPLL(clock, clk_a, clk_b);
	// tridentReg->tridentRegsClock[0x00] = (read_vga_reg(0x3CC) & 0xF3) | 0x08;

	// Determine Miscellaneous Output Register sync polarities deterministically
	uint8 misc = 0x23; // default: color emulation, ram enable, clock 0 <- should it be read_vga_reg(0x3CC) & 0xF3 ?
	if (!(mode.timing.flags & B_POSITIVE_HSYNC))
		misc |= 0x40; // negative hsync
	if (!(mode.timing.flags & B_POSITIVE_VSYNC))
		misc |= 0x80; // negative vsync

	tridentReg->tridentRegsClock[0x00] = (misc & 0xF3) | 0x08; // select external clock (clock 2)
	tridentReg->tridentRegsClock[0x01] = clk_a;
	tridentReg->tridentRegsClock[0x02] = clk_b;

	tridentReg->tridentRegs3C4[NewMode1] = 0xC0;
	tridentReg->tridentRegs3C4[Protection] = 0x92;

	// Linear frame buffer mapping
	tridentReg->tridentRegs3x4[LinearAddReg] = 0x20; // Enable Linear frame buffer

	tridentReg->tridentRegs3x4[CRTCModuleTest] = 0x80; // No interlace
	tridentReg->tridentRegs3x4[InterfaceSel] = read_crtc_reg(InterfaceSel) | 0x40;
	tridentReg->tridentRegs3x4[Performance] = read_crtc_reg(Performance) | 0x10;
	tridentReg->tridentRegs3x4[DRAMControl] = read_crtc_reg(DRAMControl) | 0x10;

	// AddColReg (overflow bits of offset)
	tridentReg->tridentRegs3x4[AddColReg] = read_crtc_reg(AddColReg) & 0xEF;
	tridentReg->tridentRegs3x4[AddColReg] |= (offset & 0x100) >> 4;
	tridentReg->tridentRegs3x4[AddColReg] &= 0xDF;
	tridentReg->tridentRegs3x4[AddColReg] |= (offset & 0x200) >> 4;

	// Accel / Bus
	tridentReg->tridentRegs3x4[GraphEngReg] |= 0x80; // MMIO accel enable
	tridentReg->tridentRegs3CE[MiscIntContReg] = read_vga_reg(0x3CF) | 0x04;

	// PCI Burst
	tridentReg->tridentRegs3x4[PCIReg] = read_crtc_reg(PCIReg) & 0xF9; // Clear bursts first
	tridentReg->tridentRegs3x4[PCIReg] |= 0x06; // Enable PCI bursts

	// Video Thresholds
	tridentReg->tridentRegs3C4[SSetup] = read_seq_reg(SSetup) | 0x04;
	tridentReg->tridentRegs3C4[SKey] = 0x00;
	tridentReg->tridentRegs3C4[SPKey] = 0xC0;
	tridentReg->tridentRegs3C4[Threshold] = read_seq_reg(Threshold);
	if (mode.bpp > 16) {
		tridentReg->tridentRegs3C4[Threshold] = (tridentReg->tridentRegs3C4[Threshold] & 0xF0) | 0x02;
	}

	// 2. Program standard VGA timing values
	uint8 crtc[25];
	int h_total = (mode.timing.h_total / 8) - 5;
	int h_display = (mode.timing.h_display / 8) - 1;
	int h_sync_start = (mode.timing.h_sync_start / 8);
	int h_sync_end = (mode.timing.h_sync_end / 8);
	int h_blank_start = h_display + 1;
	int h_blank_end = h_total;

	crtc[0x00] = h_total & 0xFF;
	crtc[0x01] = h_display & 0xFF;
	crtc[0x02] = h_blank_start & 0xFF;
	crtc[0x03] = (h_blank_end & 0x1F) | 0x80;
	crtc[0x04] = h_sync_start & 0xFF;
	crtc[0x05] = ((h_sync_end & 0x1F) | ((h_blank_end & 0x20) << 2));
	crtc[0x06] = v_total & 0xFF;
	crtc[0x07] = (((v_total & 0x100) >> 8)
		| ((v_display & 0x100) >> 7)
		| ((v_sync_start & 0x100) >> 6)
		| ((v_blank_start & 0x100) >> 5)
		| 0x10
		| ((v_total & 0x200) >> 4)
		| ((v_display & 0x200) >> 3)
		| ((v_sync_start & 0x200) >> 2));
	crtc[0x08] = 0x00;
	crtc[0x09] = ((v_blank_start & 0x200) >> 4) | 0x40;
	crtc[0x0a] = 0x00;
	crtc[0x0b] = 0x00;
	crtc[0x0c] = 0x00;
	crtc[0x0d] = 0x00;
	crtc[0x0e] = 0x00;
	crtc[0x0f] = 0x00;
	crtc[0x10] = v_sync_start & 0xFF;
	crtc[0x11] = (v_sync_end & 0x0F) | 0x20;
	crtc[0x12] = v_display & 0xFF;
	crtc[0x13] = (mode.bytesPerRow / 8) & 0xFF;
	crtc[0x14] = 0x00;
	crtc[0x15] = v_blank_start & 0xFF;
	crtc[0x16] = v_blank_end & 0xFF;
	crtc[0x17] = 0xE3;
	crtc[0x18] = 0xFF;

	// Unlock CRTC registers (especially CR0-7)
	write_crtc_reg(0x11, read_crtc_reg(0x11) & ~0x80);

	for (int i = 0; i < 25; i++) {
		write_crtc_reg(i, crtc[i]);
	}

	// 3. Program standard Sequencer Registers
	write_seq_reg(0x00, 0x03); // Reset
	write_seq_reg(0x01, 0x01); // 8-dot clock, normal display
	write_seq_reg(0x02, 0x0F); // Enable write to all planes
	write_seq_reg(0x03, 0x00);
	write_seq_reg(0x04, 0x0E); // Chain-4 mode, linear addressing

	// 4. Program standard Graphics Controller Registers
	write_vga_reg(0x3CE, 0x00); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x01); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x02); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x03); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x04); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x05); write_vga_reg(0x3CF, 0x40); // 256-color graphics mode
	write_vga_reg(0x3CE, 0x06); write_vga_reg(0x3CF, 0x05); // Graphics mode, map to A000-BFFF
	write_vga_reg(0x3CE, 0x07); write_vga_reg(0x3CF, 0x0F);
	write_vga_reg(0x3CE, 0x08); write_vga_reg(0x3CF, 0xFF);

	// 5. Program standard Attribute Controller Registers
	// Always reset AC flip-flop and re-enable display output to avoid black screen
	(void)read_vga_reg(0x3DA); // Reset AC flip-flop to Index mode
	for (uint8 i = 0; i < 16; i++) {
		write_vga_reg(0x3C0, i);
		write_vga_reg(0x3C0, i); // write 1:1 palette indices
	}
	write_vga_reg(0x3C0, 0x10); write_vga_reg(0x3C0, 0x41); // Graphics, color mode
	write_vga_reg(0x3C0, 0x11); write_vga_reg(0x3C0, 0x00);
	write_vga_reg(0x3C0, 0x12); write_vga_reg(0x3C0, 0x0F);
	write_vga_reg(0x3C0, 0x13); write_vga_reg(0x3C0, 0x00);
	write_vga_reg(0x3C0, 0x14); write_vga_reg(0x3C0, 0x00);

	(void)read_vga_reg(0x3DA); // Reset AC flip-flop to Index mode
	write_vga_reg(0x3C0, 0x20); // Enable display output (PAS bit = 1)

	// Ensure standard VGA palette mask is fully open on the physical RAMDAC port (0x3C6)
	write_reg8(0x3C6, 0xFF);

	if (enable_log)
		debug_printf("Trident_ACC: Standard Attribute Controller programmed, PAS enabled\n");

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// 6. Restore / write Trident extended registers (TridentRestore)
	// Unprotect
	OUTB(0x3C4, Protection);
	OUTB(0x3C5, 0x92);

	// Goto New Mode
	OUTB(0x3C4, 0x0B);
	(void)INB(0x3C5);

	// Unprotect registers
	OUTW(0x3C4, ((0xC0 ^ 0x02) << 8) | NewMode1);

	// Restore RAMDAC extended command register at physical port 0x3C6
	volatile uint8 dummy;
	dummy = INB(0x3C8);
	uint8 r_0 = INB(0x3C6);
	uint8 r_1 = INB(0x3C6);
	uint8 r_2 = INB(0x3C6);
	uint8 r_3 = INB(0x3C6);
	uint8 r_4 = INB(0x3C6); // old DAC command
	OUTB(0x3C6, tridentReg->tridentRegsDAC[0x00]);
	
	// Read back to verify
	dummy = INB(0x3C8);
	(void)INB(0x3C6); (void)INB(0x3C6); (void)INB(0x3C6); (void)INB(0x3C6);
	uint8 r_back = INB(0x3C6);
	dummy = INB(0x3C8);
	(void)dummy;

	if (enable_log)
		debug_printf("Trident_REG: DAC Command Old=0x%02X, Write=0x%02X, Readback=0x%02X (four dummy reads were: 0x%02X, 0x%02X, 0x%02X, 0x%02X)\n",
			r_4, tridentReg->tridentRegsDAC[0x00], r_back, r_0, r_1, r_2, r_3);

	// Restore extended registers with active readback logging
	OUTW_3x4(CRTCModuleTest);
	OUTW_3x4(LinearAddReg);
	OUTW_3C4(NewMode2);
	OUTW_3x4(CursorControl);
	OUTW_3x4(CRTHiOrd);
	OUTW_3x4(HorizOverflow);
	OUTW_3x4(AddColReg);
	OUTW_3x4(GraphEngReg);
	OUTW_3x4(Performance);
	OUTW_3x4(InterfaceSel);
	OUTW_3x4(DRAMControl);
	OUTW_3x4(PixelBusReg);
	OUTW_3x4(PCIReg);
	OUTW_3x4(PCIRetry);
	OUTW_3CE(MiscIntContReg);
	OUTW_3CE(MiscExtFunc);
	OUTW_3CE(VertStretch);
	OUTW_3CE(HorStretch);
	OUTW_3x4(Offset);

	OUTW_3C4(Threshold);
	OUTW_3C4(SSetup);
	OUTW_3C4(SKey);
	OUTW_3C4(SPKey);
	OUTW_3x4(PreEndControl);
	OUTW_3x4(PreEndFetch);

	OUTW_3x4(RAMDACTiming);

	// Restore clock via dedicated clock synthesizer ports at 0x43C8 and 0x43C9 (NewClockCode)
	// TmTFx: NO this is not for CYBERBLADE 3D chipsets look at trident_dac.c
	uint8 old_clk_low = read_seq_reg(ClockLow);
	uint8 old_clk_high = read_seq_reg(ClockHigh);

	OUTW(0x3C4, (tridentReg->tridentRegsClock[0x01]) << 8 | ClockLow);
	OUTW(0x3C4, (tridentReg->tridentRegsClock[0x02]) << 8 | ClockHigh);

	uint8 new_clk_low = read_seq_reg(ClockLow);
	uint8 new_clk_high = read_seq_reg(ClockHigh);

	if (enable_log)
		debug_printf("Trident_REG: Clock Low Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
			old_clk_low, tridentReg->tridentRegsClock[0x01], new_clk_low);
	if (enable_log)
		debug_printf("Trident_REG: Clock High Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
			old_clk_high, tridentReg->tridentRegsClock[0x02], new_clk_high);

	// Scrittura finale di MiscOut (0x3C2)
	uint8 old_misc = read_vga_reg(0x3CC);
	OUTB(0x3C2, tridentReg->tridentRegsClock[0x00]);
	uint8 new_misc = read_vga_reg(0x3CC);

	if (enable_log)
		debug_printf("Trident_REG: MiscOut Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
			old_misc, tridentReg->tridentRegsClock[0x00], new_misc);

	// Protect / Lock registri
	OUTB(0x3C4, Protection);
	//OUTB(0x3C5, 0x92);
	OUTB(0x3C5, tridentReg->tridentRegs3C4[Protection]);

	//OUTW(0x3C4, (0x80 << 8) | NewMode1);
	OUTW(0x3C4, ((tridentReg->tridentRegs3C4[NewMode1] ^ 0x02) << 8) | NewMode1);

	si.displayMode = mode;

	// Place cursor pattern at the end of the frame buffer
	si.maxFrameBufferSize = si.videoMemSize;
	si.cursorOffset = si.maxFrameBufferSize - 4096;

	if (enable_log)
		debug_printf("Trident_ACC: SetDisplayMode completed successfully. Cursor offset: %u\n",
			si.cursorOffset);

	return B_OK;
}


status_t 
MoveDisplay(uint16 horizontalStart, uint16 verticalStart)
{
	DisplayModeEx& mode = gInfo.sharedInfo->displayMode;

	if (mode.timing.h_display + horizontalStart > mode.virtual_width
		|| mode.timing.v_display + verticalStart > mode.virtual_height)
		return B_ERROR;

	mode.h_display_start = horizontalStart;
	mode.v_display_start = verticalStart;

	return B_OK;
}


uint32 
AccelerantModeCount(void)
{
	if (gInfo.sharedInfo->modeCount == 0) {
		CreateModeList();
	}
	return gInfo.sharedInfo->modeCount;
}


status_t 
GetModeList(display_mode* dmList)
{
	if (gInfo.sharedInfo->modeCount == 0) {
		CreateModeList();
	}
	memcpy(dmList, gInfo.modeList, gInfo.sharedInfo->modeCount * sizeof(display_mode));
	return B_OK;
}


status_t 
GetDisplayMode(display_mode* current_mode)
{
	*current_mode = gInfo.sharedInfo->displayMode;
	return B_OK;
}


status_t 
GetFrameBufferConfig(frame_buffer_config* pFBC)
{
	SharedInfo& si = *gInfo.sharedInfo;

	pFBC->frame_buffer = (void*)((addr_t)si.videoMemAddr + si.frameBufferOffset);
	pFBC->frame_buffer_dma = (void*)((addr_t)si.videoMemPCI + si.frameBufferOffset);
	uint32 bytesPerPixel = (si.displayMode.bpp + 7) / 8;
	pFBC->bytes_per_row = si.displayMode.virtual_width * bytesPerPixel;

	return B_OK;
}


status_t 
GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high)
{
	uint32 maxPixelClock = 230000;

	if (low != NULL) {
		uint32 totalClocks = (uint32)mode->timing.h_total * (uint32)mode->timing.v_total;
		uint32 lowClock = (totalClocks * 48L) / 1000L;
		if (lowClock > maxPixelClock)
			return B_ERROR;

		*low = lowClock;
	}

	if (high != NULL)
		*high = maxPixelClock;

	return B_OK;
}


status_t
GetTimingConstraints(display_timing_constraints *constraints)
{
	(void)constraints;
	return B_ERROR;
}


status_t
GetPreferredDisplayMode(display_mode* preferredMode)
{
	(void)preferredMode;
	return B_ERROR;
}


#ifdef __HAIKU__
status_t
GetEdidInfo(void* info, size_t size, uint32* _version)
{
	SharedInfo& si = *gInfo.sharedInfo;

	if (!si.bHaveEDID)
		return B_ERROR;

	if (size < sizeof(struct edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &si.edidInfo, sizeof(struct edid1_info));
	*_version = EDID_VERSION_1;
	return B_OK;
}
#endif


extern "C" void
trident_set_indexed_colors(uint count, uint8 first, uint8* color_data, uint32 flags)
{
	(void)flags;

	if (first + count > 256)
		count = 256 - first;

	// Write starting index to 0x3C8 (mapped directly on BAR 1)
	write_reg8(0x3C8, first);

	for (uint i = 0; i < count; i++) {
		uint8 r = color_data[i * 3 + 0];
		uint8 g = color_data[i * 3 + 1];
		uint8 b = color_data[i * 3 + 2];

		// VGA DAC expects 6 bits per component
		write_reg8(0x3C9, r >> 2);
		write_reg8(0x3C9, g >> 2);
		write_reg8(0x3C9, b >> 2);
	}
}
