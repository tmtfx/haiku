/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */
#ifndef TRIDENT_ACCEL_H
#define TRIDENT_ACCEL_H


#include "DriverInterface.h"
#include <video_overlay.h>


#undef TRACE

#ifdef ENABLE_DEBUG_TRACE
extern "C" void  _sPrintf(const char* format, ...);
#	define TRACE(x...) _sPrintf("Trident: " x)
#else
#	define TRACE(x...) ;
#endif


struct AccelerantInfo {
	int			 deviceFileDesc;

	SharedInfo*	 sharedInfo;
	area_id		 sharedInfoArea;

	uint8*		 regs;
	area_id		 regsArea;

	display_mode* modeList;
	area_id		 modeListArea;

	bool	bAccelerantIsClone;
};

extern AccelerantInfo gInfo;


// VGA MMIO offset and helper definitions
#define TRIDENT_MMIO_VGA 0x1F000

#define INREG8(addr)        *((volatile uint8*)(gInfo.regs + (uint32)(addr)))
#define INREG16(addr)       *((volatile uint16*)(gInfo.regs + (uint32)(addr)))
#define INREG32(addr)       *((volatile uint32*)(gInfo.regs + (uint32)(addr)))

#define OUTREG8(addr, val)   *((volatile uint8*)(gInfo.regs + (uint32)(addr))) = (uint8)(val)
#define OUTREG16(addr, val)  *((volatile uint16*)(gInfo.regs + (uint32)(addr))) = (uint16)(val)
#define OUTREG32(addr, val)  *((volatile uint32*)(gInfo.regs + (uint32)(addr))) = (uint32)(val)

inline uint8 read_reg8(uint32 offset) { return INREG8(offset); }
inline void write_reg8(uint32 offset, uint8 value) { OUTREG8(offset, value); }

inline uint8 read_vga_reg(uint32 port) { return read_reg8(TRIDENT_MMIO_VGA + port); }
inline void write_vga_reg(uint32 port, uint8 value) { write_reg8(TRIDENT_MMIO_VGA + port, value); }

inline uint8 read_crtc_reg(uint8 index) {
	write_vga_reg(0x3D4, index);
	return read_vga_reg(0x3D5);
}

inline void write_crtc_reg(uint8 index, uint8 value) {
	write_vga_reg(0x3D4, index);
	write_vga_reg(0x3D5, value);
}

inline uint8 read_seq_reg(uint8 index) {
	write_vga_reg(0x3C4, index);
	return read_vga_reg(0x3C5);
}

inline void write_seq_reg(uint8 index, uint8 value) {
	write_vga_reg(0x3C4, index);
	write_vga_reg(0x3C5, value);
}


#if defined(__cplusplus)
extern "C" {
#endif

// General Hook Declarations
status_t InitAccelerant(int fd);
ssize_t  AccelerantCloneInfoSize(void);
void	 GetAccelerantCloneInfo(void* data);
status_t CloneAccelerant(void* data);
void	 UninitAccelerant(void);
status_t GetAccelerantDeviceInfo(accelerant_device_info* adi);
sem_id	 AccelerantRetraceSemaphore(void);

// Mode Hook Declarations
uint32	 AccelerantModeCount(void);
status_t GetModeList(display_mode* dm);
status_t ProposeDisplayMode(display_mode* target, const display_mode* low, const display_mode* high);
status_t SetDisplayMode(display_mode* mode_to_set);
status_t GetDisplayMode(display_mode* current_mode);
status_t GetFrameBufferConfig(frame_buffer_config* a_frame_buffer);
status_t GetPixelClockLimits(display_mode* dm, uint32* low, uint32* high);
status_t MoveDisplay(uint16 h_display_start, uint16 v_display_start);
status_t GetTimingConstraints(display_timing_constraints* dtc);
status_t GetPreferredDisplayMode(display_mode* preferredMode);
status_t GetEdidInfo(void* info, size_t size, uint32* _version);

// Engine Hook Declarations
uint32   AccelerantEngineCount(void);
status_t AcquireEngine(uint32 capabilities, uint32 max_wait, sync_token* st, engine_token** et);
status_t ReleaseEngine(engine_token* et, sync_token* st);
void	 WaitEngineIdle(void);
status_t GetSyncToken(engine_token* et, sync_token* st);
status_t SyncToToken(sync_token* st);

// Cursor Hook Declarations
status_t SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
						uint8* andMask, uint8* xorMask);
status_t SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
						color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData);
uint32   GetCursorBits(void);
void	 MoveCursor(uint16 x, uint16 y);
void	 ShowCursor(bool bShow);

// Overlay Hook Declarations
uint32 trident_overlay_count(const display_mode* dm);
const uint32* trident_overlay_supported_spaces(const display_mode* dm);
uint32 trident_overlay_supported_features(uint32 a_color_space);
const overlay_buffer* trident_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height);
status_t trident_release_overlay_buffer(const overlay_buffer* ob);
status_t trident_get_overlay_constraints(const display_mode* dm, const overlay_buffer* ob, overlay_constraints* oc);
overlay_token trident_allocate_overlay(void);
status_t trident_release_overlay(overlay_token ot);
status_t trident_configure_overlay(overlay_token ot, const overlay_buffer* ob, const overlay_window* ow, const overlay_view* ov);

// DPMS Hook Declarations
uint32 trident_dpms_mode(void);
uint32 trident_dpms_capabilities(void);
status_t trident_set_dpms_mode(uint32 mode);

// Palette Hook Declarations
void trident_set_indexed_colors(uint count, uint8 first, uint8* color_data, uint32 flags);

#if defined(__cplusplus)
}
#endif


#endif	// TRIDENT_ACCEL_H
