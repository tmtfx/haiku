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
#include <string.h>
#include "trident_regs.h"

/*
 * color space for hardware cursor:
 * Transparent: AND = 0, XOR = 1
 * White:		AND = 1, XOR = 1
 * Black:		AND = 1, XOR = 0
 */

// Logging helper for CRTC cursor register writes
static inline void
write_crtc_reg_logged(const char* name, uint8 index, uint8 value)
{
	uint8 old_val = read_crtc_reg(index);
	write_crtc_reg(index, value);
	uint8 new_val = read_crtc_reg(index);
	debug_printf("Trident_CUR: CR%02X (%s) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
		index, name, old_val, value, new_val);
}


extern "C" {

uint32
GetCursorBits(void)
{
	SharedInfo& si = *gInfo.sharedInfo;
	
	return si.settings.cursorbits;
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	uint8* dest = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
	if (!dest)
		return B_NO_INIT;

	debug_printf("Trident_CUR: SetCursorShape starting. Width=%d, Height=%d, HotX=%d, HotY=%d, Offset=%u\n",
		width, height, hot_x, hot_y, si.cursorOffset);

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Ensure CRTC registers remain unlocked with MMIO active (CR39 = 0x87)
	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87); // 0x39

	// Initialize the 1024-byte cursor pattern buffer to transparent (AND=1, XOR=0)
	// Trident uses 32-bit interleaved (dword) AND/XOR masks:
	// Each row of 16 bytes is: AND (4 bytes), XOR (4 bytes), AND (4 bytes), XOR (4 bytes)
	for (int y = 0; y < 64; y++) {
		uint8* row = dest + y * 16;
		memset(row + 0, 0x00, 4);  // AND block 0 (pixels 0-31) ex 0xFF
		memset(row + 4, 0xFF, 4);  // XOR block 0 (pixels 0-31) ex 0x00
		memset(row + 8, 0x00, 4);  // AND block 1 (pixels 32-63) ex 0xFF
		memset(row + 12, 0xFF, 4); // XOR block 1 (pixels 32-63) ex 0x00
	}

	uint32 stride = (width + 7) / 8;

	for (uint32 y = 0; y < height && y < 64; y++) {
		uint8* row = dest + y * 16;
		for (uint32 x = 0; x < width && x < 64; x++) {
			uint32 src_byte = y * stride + (x / 8);
			uint8 src_bit = 7 - (x % 8);

			bool and_bit = (andMask[src_byte] >> src_bit) & 1;
			bool xor_bit = (xorMask[src_byte] >> src_bit) & 1;

			int and_byte_idx, xor_byte_idx;
			int bit_shift = 7 - (x % 8);

			if (x < 32) {
				and_byte_idx = x / 8;
				xor_byte_idx = (x / 8) + 4;
			} else {
				and_byte_idx = ((x - 32) / 8) + 8;
				xor_byte_idx = ((x - 32) / 8) + 12;
			}

			if (and_bit && !xor_bit) {
				// Trasparente: AND = 0, XOR = 1
				row[and_byte_idx] &= ~(1 << bit_shift);
				row[xor_byte_idx] |= (1 << bit_shift);
			} else if (!and_bit && !xor_bit) {
				// Nero: AND = 1, XOR = 0
				row[and_byte_idx] |= (1 << bit_shift);
				row[xor_byte_idx] &= ~(1 << bit_shift);
			} else if (!and_bit && xor_bit) {
				// Bianco: AND = 1, XOR = 1
				row[and_byte_idx] |= (1 << bit_shift);
				row[xor_byte_idx] |= (1 << bit_shift);
			} else {
				// Caso limite (AND=0, XOR=0, inversione del pixel sottostante) se la scheda lo supporta
				row[and_byte_idx] &= ~ (1 << bit_shift);
				row[xor_byte_idx] &= ~ (1 << bit_shift);
			}
		}
	}

	// Set cursor base address registers CR44, CR45 (CursorLocLow, CursorLocHigh)
	uint32 addr = si.cursorOffset / 1024;
	//write_crtc_reg_logged("CursorLocLow", 0x44, addr & 0xFF);
	write_crtc_reg(CursorLocLow, addr & 0xFF); // 0x44
	//write_crtc_reg_logged("CursorLocHigh", 0x45, (addr >> 8) & 0xFF);
	write_crtc_reg(CursorLocHigh, (addr >> 8) & 0xFF); // 0x45

	// Set cursor colors: Background to Black (CR4C-CR4F), Foreground to White (CR48-CR4B)
	for (int i = 0; i < 4; i++) {
		//write_crtc_reg_logged("CursorFG", 0x48 + i, 0xFF);
		write_crtc_reg(CursorFG1 + i, 0xFF); // 0x48, 0x49, ... 
		//write_crtc_reg_logged("CursorBG", 0x4C + i, 0x00);
		write_crtc_reg(CursorBG1 + i, 0x00); // 0x4C, 0x4D, ...
	}

	// Enable cursor (CR50: Bit 0=Enable, Bit 6=64x64, Bit 7=Windows/X11 mode)
	//write_crtc_reg_logged("CursorControl", 0x50, 0xC1);
	write_crtc_reg(CursorControl, 0xC1); // 0x50

	// Update cursor position
	MoveCursor(si.cursorHotX, si.cursorHotY);

	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	uint8* dest = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
	if (!dest)
		return B_NO_INIT;

	debug_printf("Trident_CUR: SetCursorBitmap starting. Space=0x%X, Width=%d, Height=%d, HotX=%d, HotY=%d, Offset=%u\n",
		colorSpace, width, height, hot_x, hot_y, si.cursorOffset);

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Ensure CRTC registers remain unlocked with MMIO active (CR39 = 0x87)
	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87); // 0x39

	// Initialize the 1024-byte cursor pattern buffer to transparent (AND=1, XOR=0)
	for (int y = 0; y < 64; y++) {
		uint8* row = dest + y * 16;
		memset(row + 0, 0x00, 4);  // AND block 0 (pixels 0-31) ex 0xFF
		memset(row + 4, 0xFF, 4);  // XOR block 0 (pixels 0-31) ex 0x00
		memset(row + 8, 0x00, 4);  // AND block 1 (pixels 32-63)
		memset(row + 12, 0xFF, 4); // XOR block 1 (pixels 32-63)
	}

	if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
		for (uint32 y = 0; y < height && y < 64; y++) {
			uint8* row = dest + y * 16;
			const uint8* srcRow = bitmapData + y * bytesPerRow;

			for (uint32 x = 0; x < width && x < 64; x++) {
				const uint8* pixel = srcRow + x * 4;
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 255;

				if (a < 128)
					continue; // Keep transparent (AND=0, XOR=1)

				int and_byte_idx, xor_byte_idx;
				int bit_shift = 7 - (x % 8);

				if (x < 32) {
					and_byte_idx = x / 8;
					xor_byte_idx = (x / 8) + 4;
				} else {
					and_byte_idx = ((x - 32) / 8) + 8;
					xor_byte_idx = ((x - 32) / 8) + 12;
				}

				// Render 2 colors based on luma and alpha
				uint32 luma = (r + g + b) / 3;
				if (luma > 128) {
					// White (AND=1, XOR=1)
					row[and_byte_idx] |= (1 << bit_shift);
					row[xor_byte_idx] |= (1 << bit_shift);
				} else {
					// Black (AND=1, XOR=0)
					row[and_byte_idx] |= (1 << bit_shift);
					row[xor_byte_idx] &= ~(1 << bit_shift);
				}
			}
		}
	} else {
		debug_printf("Trident_CUR: SetCursorBitmap UNSUPPORTED color space: 0x%X\n", colorSpace);
		return B_BAD_VALUE;
	}

	// Set cursor base address registers CR44, CR45 (CursorLocLow, CursorLocHigh)
	uint32 addr = si.cursorOffset / 1024;
	//write_crtc_reg_logged("CursorLocLow", 0x44, addr & 0xFF);
	write_crtc_reg(CursorLocLow, addr & 0xFF); // 0x44
	//write_crtc_reg_logged("CursorLocHigh", 0x45, (addr >> 8) & 0xFF);
	write_crtc_reg(CursorLocHigh, (addr >> 8) & 0xFF); // 0x45

	// Set cursor colors: Background to Black (CR4C-CR4F), Foreground to White (CR48-CR4B)
	for (int i = 0; i < 4; i++) {
		//write_crtc_reg_logged("CursorFG", 0x48 + i, 0xFF);
		write_crtc_reg(CursorFG1 + i, 0xFF); // 0x48, 0x49, ...
		//write_crtc_reg_logged("CursorBG", 0x4C + i, 0x00);
		write_crtc_reg(CursorBG1 + i, 0x00); // 0x4C, 0x4D, ...
	}

	// Enable cursor (CR50: Bit 0=Enable, Bit 6=64x64, Bit 7=Windows/X11 mode)
	//write_crtc_reg_logged("CursorControl", 0x50, 0xC1);
	write_crtc_reg(CursorControl, 0xC1); // 0x50

	// Update cursor position
	//MoveCursor(si.cursorHotX, si.cursorHotY);

	return B_OK;
}

// like S3 did
void
MoveCursor(uint16 xPos, uint16 yPos)
{
	int x = xPos;		// use signed int's since chip specific functions
	int y = yPos;		// need signed int to determine if cursor off screen

	SharedInfo& si = *gInfo.sharedInfo;
	DisplayModeEx& dm = si.displayMode;

	uint16 hds = dm.h_display_start;	// current horizontal starting pixel
	uint16 vds = dm.v_display_start;	// current vertical starting line

	// Clamp cursor to virtual display.
	if (x >= dm.virtual_width)
		x = dm.virtual_width - 1;
	if (y >= dm.virtual_height)
		y = dm.virtual_height - 1;

	// Adjust h/v display start to move cursor onto screen.
	if (x >= (dm.timing.h_display + hds))
		hds = x - dm.timing.h_display + 1;
	else if (x < hds)
		hds = x;

	if (y >= (dm.timing.v_display + vds))
		vds = y - dm.timing.v_display + 1;
	else if (y < vds)
		vds = y;

	// Reposition the desktop on the display if required.
	if (hds != dm.h_display_start || vds != dm.v_display_start)
		MoveDisplay(hds, vds);

	// Put cursor in correct physical position.
	x -= (hds + si.cursorHotX);
	y -= (vds + si.cursorHotY);
	
	uint8 preset_x = 0;
	uint8 preset_y = 0;

	if (x < 0) {
		preset_x = -x;
		x = 0;
	}
	if (y < 0) {
		preset_y = -y;
		y = 0;
	}



// SCRITTURA EFFETTIVA NELLA SCHEDA
	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87); // 0x39
	
	// Write preset offsets (CR46, CR47)
	//write_crtc_reg_logged("PresetX", 0x46, preset_x);
	write_crtc_reg(CursorXOffset, preset_x); // 0x46
	//write_crtc_reg_logged("PresetY", 0x47, preset_y);
	write_crtc_reg(CursorYOffset, preset_y); // 0x47

	// Write X position (CR40, CR41)
	//write_crtc_reg_logged("PosXLow", 0x40, x & 0xFF);
	write_crtc_reg(CursorXLow, x & 0xFF); // 0x40
	//write_crtc_reg_logged("PosXHigh", 0x41, (x >> 8) & 0xFF);
	write_crtc_reg(CursorXHigh, (x >> 8) & 0xFF); // 0x41

	// Write Y position (CR42, CR43)
	//write_crtc_reg_logged("PosYLow", 0x42, y & 0xFF);
	write_crtc_reg(CursorYLow, y & 0xFF); // 0x42
	//write_crtc_reg_logged("PosYHigh", 0x43, (y >> 8) & 0xFF);
	write_crtc_reg(CursorYHigh, (y >> 8) & 0xFF); // 0x43
}

/*
void
MoveCursor(uint16 xPos, uint16 yPos)
{
	SharedInfo& si = *gInfo.sharedInfo;

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87); // 0x39

	// In Haiku, MoveCursor is called with coordinates of the mouse tip (hotspot needs to be subtracted)
	int16 x = (int16)xPos - (int16)si.cursorHotX;
	int16 y = (int16)yPos - (int16)si.cursorHotY;
	uint8 preset_x = 0;
	uint8 preset_y = 0;

	if (x < 0) {
		preset_x = -x;
		x = 0;
	}
	if (y < 0) {
		preset_y = -y;
		y = 0;
	}

	// Write preset offsets (CR46, CR47)
	//write_crtc_reg_logged("PresetX", 0x46, preset_x);
	write_crtc_reg(CursorXOffset, preset_x); // 0x46
	//write_crtc_reg_logged("PresetY", 0x47, preset_y);
	write_crtc_reg(CursorYOffset, preset_y); // 0x47

	// Write X position (CR40, CR41)
	//write_crtc_reg_logged("PosXLow", 0x40, x & 0xFF);
	write_crtc_reg(CursorXLow, x & 0xFF); // 0x40
	//write_crtc_reg_logged("PosXHigh", 0x41, (x >> 8) & 0xFF);
	write_crtc_reg(CursorXHigh, (x >> 8) & 0xFF); // 0x41
	
	

	// Write Y position (CR42, CR43)
	//write_crtc_reg_logged("PosYLow", 0x42, y & 0xFF);
	write_crtc_reg(CursorYLow, y & 0xFF); // 0x42
	//write_crtc_reg_logged("PosYHigh", 0x43, (y >> 8) & 0xFF);
	write_crtc_reg(CursorYHigh, (y >> 8) & 0xFF); // 0x43
	
}*/


void
ShowCursor(bool bShow)
{
	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87); // 0x39

	uint8 ctrl = read_crtc_reg(CursorControl);
	if (bShow) {
		ctrl |= 0x01;
	} else {
		ctrl &= ~0x01;
	}
	//write_crtc_reg_logged("CursorControl", 0x50, ctrl);
	write_crtc_reg(CursorControl, ctrl); // 0x50
}

} // extern "C"
