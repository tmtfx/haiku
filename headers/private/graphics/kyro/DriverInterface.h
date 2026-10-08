/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */
#ifndef KYRO_DRIVER_INTERFACE_H
#define KYRO_DRIVER_INTERFACE_H


#include <Accelerant.h>
#include <Drivers.h>
#include <GraphicsDefs.h>
#include <edid.h>
#include <video_overlay.h>


#define KYRO_DRIVER_NAME				"kyro"
#define KYRO_ACCELERANT_NAME			"kyro.accelerant"
#define KYRO_PRIVATE_DATA_MAGIC			0x4b59524f


enum {
	KYRO_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	KYRO_DEVICE_NAME,
	KYRO_GET_EDID,
};


struct KyroGetPrivateData {
	uint32	magic;
	area_id	sharedInfoArea;
};


typedef struct {
	char	accelerant[B_FILE_NAME_LENGTH];
	bool	hardcursor;
	uint32	cursorbits;
} kyro_settings;


struct SharedInfo {
	uint16		vendorID;
	uint16		deviceID;
	uint8		revision;
	char		chipName[32];
	char		deviceName[B_PATH_NAME_LENGTH];

	area_id		regsArea;
	area_id		frameBufferArea;
	area_id		modeArea;
	uint32		modeCount;

	uint8*		frameBuffer;
	phys_addr_t	frameBufferPCI;
	uint32		frameBufferSize;

	uint32		maxPixelClock;
	uint32		colorSpaceCount;
	color_space	colorSpaces[2];

	uint32		cursorOffset;
	uint32		cursorBufferSize;

	struct {
		uint16	hotX;
		uint16	hotY;
		uint16	x;
		uint16	y;
		bool	visible;
	} cursor;

	struct {
		uint32			token;
		overlay_buffer	buffers[4];
		bool			bufferAllocated[4];
		uint32			bufferOffset[4];
		bool			active;
	} overlay;

	display_mode	currentMode;
	display_mode	preferredMode;
	frame_buffer_config frameBufferConfig;
	uint32			dpmsMode;
	edid1_info		edidInfo;
	edid1_raw		edidRaw;
	bool			hasEdid;
	kyro_settings	settings;

	bool		accelerantInUse;
	bool		hasBootMode;
	uint16		bootWidth;
	uint16		bootHeight;
	uint16		bootDepth;
};


#endif	// KYRO_DRIVER_INTERFACE_H
