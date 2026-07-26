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
#ifndef TRIDENT_DRIVERINTERFACE_H
#define TRIDENT_DRIVERINTERFACE_H


#include <Accelerant.h>
#include <GraphicsDefs.h>
#include <Drivers.h>
#include <edid.h>


#define ENABLE_DEBUG_TRACE


struct Benaphore {
	sem_id	sem;
	int32	count;

	status_t Init(const char* name)
	{
		count = 0;
		sem = create_sem(0, name);
		return sem < 0 ? sem : B_OK;
	}

	status_t Acquire()
	{
		if (atomic_add(&count, 1) > 0)
			return acquire_sem(sem);
		return B_OK;
	}

	status_t Release()
	{
		if (atomic_add(&count, -1) > 1)
			return release_sem(sem);
		return B_OK;
	}

	void Delete()	{ delete_sem(sem); }
};


#define TRIDENT_PRIVATE_DATA_MAGIC	0x9880

enum {
	TRIDENT_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	TRIDENT_DEVICE_NAME,
	TRIDENT_GET_EDID,
	TRIDENT_RUN_INTERRUPTS,
	TRIDENT_GET_PIO,
	TRIDENT_SET_PIO,
	TRIDENT_ENABLE_MMIO,
};


struct TridentGetSetPIO {
	uint32	magic;
	uint16	offset;
	uint8	size;
	uint32	value;
};


enum ChipType {
	TRIDENT_BLADE3D = 1,
};


struct DisplayModeEx : display_mode {
	uint32	bpp;
	uint32	bytesPerRow;
};

typedef struct {
	char	accelerant[B_FILE_NAME_LENGTH];
	bool	dumprom;
	uint32 	memory;		/* Forza riconoscimento memoria */
	bool	hardcursor;
	uint32	cursorbits;
} trident_settings;

struct SharedInfo {
	// Device ID info
	uint16	vendorID;
	uint16	deviceID;
	uint8	revision;
	uint32	chipType;
	char	chipName[32];

	bool	bAccelerantInUse;
	bool	bInterruptAssigned;

	bool	bDisableHdwCursor;
	bool	bDisableAccelDraw;

	sem_id	vertBlankSem;

	// Memory mappings
	area_id	regsArea;			// mapped BAR 1
	area_id	videoMemArea;		// mapped BAR 0
	addr_t	videoMemAddr;
	phys_addr_t	videoMemPCI;
	uint32	videoMemSize;

	uint32	cursorOffset;
	uint32	frameBufferOffset;
	uint32	maxFrameBufferSize;

	// Color spaces
	color_space	colorSpaces[6];
	uint32	colorSpaceCount;

	// List of screen modes
	area_id	modeArea;
	uint32	modeCount;

	uint16	cursorHotX;
	uint16	cursorHotY;

	DisplayModeEx displayMode;
	uint32	dpmsMode;

	int32	commonCmd;

	edid1_info	edidInfo;
	bool		bHaveEDID;

	Benaphore	engineLock;

	bool	has_boot_info;
	uint32	boot_width;
	uint32	boot_height;
	uint32	boot_depth;
	trident_settings settings;
};

struct TridentGetPrivateData {
	uint32	magic;
	area_id	sharedInfoArea;
};


#endif	// TRIDENT_DRIVERINTERFACE_H
