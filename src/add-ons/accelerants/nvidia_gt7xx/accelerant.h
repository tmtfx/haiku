/*
 * Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NVIDIA_GT7XX_ACCELERANT_H
#define NVIDIA_GT7XX_ACCELERANT_H

#include "DriverInterface.h"


typedef struct accelerant_info {
	int					device;
	bool				is_clone;

	area_id				shared_info_area;
	vesa_shared_info	*shared_info;
	area_id				regs_area;
	uint8*				registers;

	area_id				mode_list_area;
	display_mode		*mode_list;

	area_id				frame_buffer_area;
	void*				frame_buffer;
} accelerant_info;

extern accelerant_info *gInfo;

extern status_t create_mode_list(void);

#endif	/* NVIDIA_GT7XX_ACCELERANT_H */
