/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Minimal shared accelerant state for intel_arc.accelerant.
 */

#pragma once

#include "intel_arc.h"
#include <edid.h>


struct accelerant_info {
	int						device;
	bool					is_clone;

	area_id					shared_info_area;
	intel_arc_shared_info*	shared_info;

	area_id					regs_area;
	uint8*					registers;

	area_id					mode_list_area;
	display_mode*			mode_list;

	area_id					frame_buffer_area;
	void*					frame_buffer;

	edid1_info				edid_info;
	bool					has_edid;
	uint32					last_hotplug_event_count;
};


extern accelerant_info* gInfo;
