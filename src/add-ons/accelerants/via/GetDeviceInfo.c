/*
	Author:
	Rudolf Cornelissen 7/2004-1/2016

	Portions based on the OpenChrome Project (https://www.freedesktop.org/wiki/Openchrome):
	Copyright 2005-2016 The OpenChrome Project
	Copyright 2004-2005 The Unichrome Project
	Copyright 1998-2003 VIA Technologies, Inc.
	Copyright 2001-2003 S3 Graphics, Inc.
	Authors: Kevin Brace, James Simmons, Xavier Bachelot, Thomas Hellström, Luc Verhaegen
*/

#define MODULE_BIT 0x04000000

#include "acc_std.h"

/* Get some info about the device */
status_t GET_ACCELERANT_DEVICE_INFO(accelerant_device_info * adi)
{
	LOG(4,("GET_ACCELERANT_DEVICE_INFO: returning info\n"));

	/* no info on version is provided, so presumably this is for my info */
	adi->version = 1;

	sprintf(adi->name, "VIA Graphics");
	switch (si->ps.card_type)
	{
	case VT3022:
		sprintf(adi->chipset, "CLE266 (VT3022)");
		break;
	case VT3122:
		sprintf(adi->chipset, "CLE266 (VT3122)");
		break;
	case VT3205:
		sprintf(adi->chipset, "KM400 / KN400 (VT3205)");
		break;
	case VT7205:
		sprintf(adi->chipset, "KM400 / P4M800 (VT7205)");
		break;
	case VT3108:
		sprintf(adi->chipset, "K8M800 (VT3204)");
		break;
	case VT3118:
		sprintf(adi->chipset, "PM800 / PN800 (VT3259)");
		break;
	case VT3314:
		sprintf(adi->chipset, "P4M800 Pro (VT3314)");
		break;
	case VT3157:
		sprintf(adi->chipset, "CX700 / VX700 (VT3324)");
		break;
	case VT3225:
		sprintf(adi->chipset, "CN750 (VT3293)");
		break;
	case VT3327:
		sprintf(adi->chipset, "P4M890 / VN890 (VT3327)");
		break;
	case VT3336:
		sprintf(adi->chipset, "K8M890 (VT3336)");
		break;
	case VT3364:
		sprintf(adi->chipset, "P4M900 / VN896 (VT3364)");
		break;
	case VT3353:
		sprintf(adi->chipset, "VX800 Chrome9 (VT3353)");
		break;
	case VT3409:
		sprintf(adi->chipset, "VX855 Chrome9 (VT3409)");
		break;
	case VT3410:
		sprintf(adi->chipset, "VX900 Chrome9 HD (VT3410)");
		break;
	default:
		sprintf(adi->chipset, "unknown");
		break;
	}
	sprintf(adi->serial_no, "unknown");
	adi->memory = si->ps.memory_size;
	adi->dac_speed = si->ps.max_dac1_clock;

	return B_OK;
}
