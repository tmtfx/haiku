/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"

#include <create_display_modes.h>
#include <string.h>
#include <unistd.h>


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

	TRACE("ProposeDisplayMode() %dx%d, pixel clock: %d kHz, space: 0x%X\n",
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

	debug_printf("Trident_ACC: SetDisplayMode starting for %dx%d, virtual %dx%d, %d bpp, bytesPerRow %d\n",
		mode.timing.h_display, mode.timing.v_display,
		mode.virtual_width, mode.virtual_height, mode.bpp, mode.bytesPerRow);

	// Let's program the hardware registers!

	// 1. Unlock CRTC registers (especially CR0-7) and Trident extended features
	write_crtc_reg(0x11, read_crtc_reg(0x11) & ~0x80); // Unlock CR0-CR07
	
	// Toggle Trident "New Mode" - MUST READ from port 0x3C5 at index 0x0B
	read_seq_reg(0x0B);

	// Unlock Extended Sequencer registers (SR0E = 0x80)
	write_seq_reg(0x0E, 0x80);

	// Unlock CyberBlade/Blade3D-specific registers (SR11 = 0x92)
	write_seq_reg(0x11, 0x92);

	// Unlock Extended CRTC registers CR30-CR3F (CR3E = 0x80)
	write_crtc_reg(0x3E, 0x80);

	// Ensure PCIReg is unlocked / MMIO is enabled (CR39 = 0x01 or CR39 | 0x01)
	write_crtc_reg(0x39, read_crtc_reg(0x39) | 0x01);

	debug_printf("Trident_ACC: Extended sequencer and CRTC registers unlocked\n");

	// 2. Program Pixel Clock (PLL)
	uint32 clock = mode.timing.pixel_clock;
	if (mode.bpp == 32)
		clock *= 2; // clock is doubled for 32bpp modes

	uint8 sr19 = 0, sr1a = 0;
	CalculateTridentPLL(clock, sr19, sr1a);
	write_seq_reg(0x19, sr19);
	write_seq_reg(0x1A, sr1a);

	debug_printf("Trident_ACC: PLL calculated for clock %u kHz: SR19 = 0x%02X, SR1A = 0x%02X\n",
		clock, sr19, sr1a);

	// Select programmable clock (Clock 2) in Misc Output
	uint8 misc = read_vga_reg(0x3CC);
	misc |= 0x2B; // enable VGA, select color emulation
	misc &= ~0x0C; // clear clock bits
	misc |= 0x08;  // Clock 2 (programmable)
	write_vga_reg(0x3C2, misc);

	// 3. Calculate and set standard VGA timing values
	uint8 crtc[25];
	int h_total = (mode.timing.h_total / 8) - 5;
	int h_display = (mode.timing.h_display / 8) - 1;
	int h_sync_start = (mode.timing.h_sync_start / 8);
	int h_sync_end = (mode.timing.h_sync_end / 8);
	int h_blank_start = h_display + 1;
	int h_blank_end = h_total;

	int v_total = mode.timing.v_total - 2;
	int v_display = mode.timing.v_display - 1;
	int v_sync_start = mode.timing.v_sync_start;
	int v_sync_end = mode.timing.v_sync_end;
	int v_blank_start = v_display;
	int v_blank_end = v_total;

	debug_printf("Trident_ACC: Standard timings calculated: HTotal=%d, HDisplay=%d, VTotal=%d, VDisplay=%d\n",
		h_total, h_display, v_total, v_display);

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

	for (int i = 0; i < 25; i++) {
		write_crtc_reg(i, crtc[i]);
	}

	// 4. Program standard Sequencer Registers
	write_seq_reg(0x00, 0x03); // Reset
	write_seq_reg(0x01, 0x01); // 8-dot clock, normal display
	write_seq_reg(0x02, 0x0F); // Enable write to all planes
	write_seq_reg(0x03, 0x00);
	write_seq_reg(0x04, 0x0E); // Chain-4 mode, linear addressing

	// 5. Program standard Graphics Controller Registers
	write_vga_reg(0x3CE, 0x00); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x01); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x02); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x03); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x04); write_vga_reg(0x3CF, 0x00);
	write_vga_reg(0x3CE, 0x05); write_vga_reg(0x3CF, 0x40); // 256-color graphics mode
	write_vga_reg(0x3CE, 0x06); write_vga_reg(0x3CF, 0x05); // Graphics mode, map to A000-BFFF
	write_vga_reg(0x3CE, 0x07); write_vga_reg(0x3CF, 0x0F);
	write_vga_reg(0x3CE, 0x08); write_vga_reg(0x3CF, 0xFF);
	// Program MiscExtFunc (index 0x0F): configure clocks and multiplex path
	write_vga_reg(0x3CE, 0x0F); write_vga_reg(0x3CF, (mode.bpp == 32) ? 0x1A : 0x12);

	// 6. Program standard Attribute Controller Registers and write Palette Mask
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

	write_vga_reg(0x3C6, 0xFF); // Ensure palette mask is fully open

	debug_printf("Trident_ACC: Standard Attribute Controller programmed, PAS enabled\n");

	// 7. Enable Linear Frame Buffer on Trident via LinearAddReg (CR21 bit 5)
	uint8 cr21 = read_crtc_reg(0x21);
	cr21 |= 0x20; // LFB Linear Addressing Enable
	write_crtc_reg(0x21, cr21);

	// 8. Configure RAMDACTiming (CR25)
	write_crtc_reg(0x25, read_crtc_reg(0x25) | 0x0F);

	// 9. Configure Performance (CR2F)
	write_crtc_reg(0x2F, read_crtc_reg(0x2F) | 0x10);

	// 10. Configure DRAMControl (CR3A)
	write_crtc_reg(0x3A, read_crtc_reg(0x3A) | 0x10);

	// 11. Configure InterfaceSel (CR2A)
	write_crtc_reg(0x2A, read_crtc_reg(0x2A) | 0x40);

	// 12. Configure New32 (CR23) for 32bpp modes
	if (mode.bpp == 32) {
		write_crtc_reg(0x23, read_crtc_reg(0x23) | 0x80);
	} else {
		write_crtc_reg(0x23, read_crtc_reg(0x23) & ~0x80);
	}

	// 13. Configure Trident Pixel Bus Register (CR38) to multiplex pixel stream
	// 8bpp: 0x00, 16bpp: 0x05, 32bpp: 0x29
	uint8 cr38 = 0x00;
	switch (mode.bpp) {
		case 8:  cr38 = 0x00; break;
		case 16: cr38 = 0x05; break;
		case 32: cr38 = 0x29; break;
	}
	write_crtc_reg(0x38, cr38);

	// 14. Configure RAMDAC Command register via port 0x3C6 (5-read sequence)
	// 8bpp: 0x00, 16bpp: 0x30, 32bpp: 0xD0
	uint8 dac_cmd = 0x00;
	switch (mode.bpp) {
		case 8:  dac_cmd = 0x00; break;
		case 16: dac_cmd = 0x30; break;
		case 32: dac_cmd = 0xD0; break;
	}
	// 5-read sequence to write to RAMDAC extended command register
	volatile uint8 dummy;
	dummy = read_vga_reg(0x3C8);
	dummy = read_vga_reg(0x3C6);
	dummy = read_vga_reg(0x3C6);
	dummy = read_vga_reg(0x3C6);
	dummy = read_vga_reg(0x3C6);
	write_vga_reg(0x3C6, dac_cmd);
	dummy = read_vga_reg(0x3C8); // Reset state machine of DAC to standard mode!
	(void)dummy;

	debug_printf("Trident_ACC: Color depth configured. PixelBus = 0x%02X, DAC Cmd = 0x%02X\n",
		cr38, dac_cmd);

	// 15. Configure Screen Pitch (Offset) in CR13 and CR29 (preserving other CR29 bits)
	uint32 offset = mode.bytesPerRow / 8;
	write_crtc_reg(0x13, offset & 0xFF);

	uint8 cr29 = read_crtc_reg(0x29);
	cr29 &= ~0x30; // Clear bits 4 and 5 of CR29
	if (offset & (1 << 8)) cr29 |= (1 << 4); // Offset bit 8 maps to CR29 bit 4
	if (offset & (1 << 9)) cr29 |= (1 << 5); // Offset bit 9 maps to CR29 bit 5
	write_crtc_reg(0x29, cr29);

	debug_printf("Trident_ACC: Pitch Offset configured: Offset=%d (CR13=0x%02X, CR29=0x%02X)\n",
		offset, offset & 0xFF, cr29);

	// 16. Configure CR27 (CRTHiOrd) for high-order vertical timing bits
	uint8 cr27 = 0x08; // default bit 3 must be set
	if ((v_blank_end - 1) & 0x400) cr27 |= (1 << 6);
	if ((v_total - 2) & 0x400)     cr27 |= (1 << 7);
	if (v_sync_start & 0x400)      cr27 |= (1 << 5);
	if ((v_display - 1) & 0x400)   cr27 |= (1 << 4);
	write_crtc_reg(0x27, cr27);

	// Configure CR2B (HorizOverflow) for high-order horizontal timing bits
	uint8 cr2b = 0x00;
	if (mode.timing.h_total & 0x800) cr2b |= (1 << 0);
	if (mode.timing.h_display & 0x800) cr2b |= (1 << 4);
	write_crtc_reg(0x2B, cr2b);

	// Configure CR1E (CRTCModuleTest)
	write_crtc_reg(0x1E, 0x80);

	debug_printf("Trident_ACC: Extended overflows configured: CR27=0x%02X, CR2B=0x%02X\n",
		cr27, cr2b);

	// 17. Protect and lock the extended sequencer registers
	write_seq_reg(0x0D, 0x20); // NewMode2
	write_seq_reg(0x0E, 0xC0); // NewMode1
	write_seq_reg(0x11, 0x92); // Protection

	// Lock Extended CRTC registers
	write_crtc_reg(0x3E, 0x00); // Lock CR30-CR3F

	si.displayMode = mode;

	// Place cursor pattern at the end of the frame buffer
	si.maxFrameBufferSize = si.videoMemSize;
	si.cursorOffset = si.maxFrameBufferSize - 4096;

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
