/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */


#include <stdlib.h>
#include <string.h>

#include <create_display_modes.h>

#include "accelerant_protos.h"
#include "accelerant.h"


static bool
same_mode(const display_mode& left, const display_mode& right)
{
	return left.space == right.space
		&& left.virtual_width == right.virtual_width
		&& left.virtual_height == right.virtual_height
		&& left.timing.pixel_clock == right.timing.pixel_clock
		&& left.timing.h_display == right.timing.h_display
		&& left.timing.h_sync_start == right.timing.h_sync_start
		&& left.timing.h_sync_end == right.timing.h_sync_end
		&& left.timing.h_total == right.timing.h_total
		&& left.timing.v_display == right.timing.v_display
		&& left.timing.v_sync_start == right.timing.v_sync_start
		&& left.timing.v_sync_end == right.timing.v_sync_end
		&& left.timing.v_total == right.timing.v_total
		&& left.timing.flags == right.timing.flags;
}


static bool
is_mode_supported(display_mode* mode)
{
	return same_mode(*mode, gInfo->shared_info->current_mode);
}


status_t
create_mode_list(void)
{
	display_mode initialMode = gInfo->shared_info->current_mode;
	color_space space = (color_space)initialMode.space;
	gInfo->mode_list_area = create_display_modes("nvidia_gt7xx modes",
		gInfo->shared_info->has_edid ? &gInfo->shared_info->edid_info : NULL,
		&initialMode, 1, &space, 1, is_mode_supported,
		&gInfo->mode_list, &gInfo->shared_info->mode_count);

	if (gInfo->mode_list_area < 0)
		return gInfo->mode_list_area;

	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
	return B_OK;
}


uint32
vesa_accelerant_mode_count(void)
{
	return gInfo->shared_info->mode_count;
}


status_t
vesa_get_mode_list(display_mode* modeList)
{
	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
vesa_propose_display_mode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		if (target->virtual_width != gInfo->mode_list[i].virtual_width
			|| target->virtual_height != gInfo->mode_list[i].virtual_height
			|| target->space != gInfo->mode_list[i].space) {
			continue;
		}

		*target = gInfo->mode_list[i];
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
vesa_set_display_mode(display_mode* modeToSet)
{
	display_mode mode = *modeToSet;
	status_t status = vesa_propose_display_mode(&mode, &mode, &mode);
	if (status != B_OK)
		return status;

	status = ioctl(gInfo->device, VESA_SET_DISPLAY_MODE, &mode, sizeof(mode));
	if (status == B_OK)
		gInfo->shared_info->current_mode = mode;

	return status;
}


status_t
vesa_get_display_mode(display_mode* currentMode)
{
	*currentMode = gInfo->shared_info->current_mode;
	return B_OK;
}


status_t
vesa_get_edid_info(void* info, size_t size, uint32* version)
{
	if (!gInfo->shared_info->has_edid)
		return B_ERROR;
	if (size < sizeof(edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &gInfo->shared_info->edid_info, sizeof(edid1_info));
	*version = EDID_VERSION_1;
	return B_OK;
}


status_t
vesa_get_frame_buffer_config(frame_buffer_config* config)
{
	if (gInfo->frame_buffer == NULL) {
		area_info info;
		status_t status = ioctl(gInfo->device, VESA_CLONE_FRAME_BUFFER,
			&info, sizeof(info));
		if (status != B_OK)
			return status;

		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
	}

	*config = gInfo->shared_info->fbc;
	config->frame_buffer = gInfo->frame_buffer;
	return B_OK;
}


status_t
vesa_get_pixel_clock_limits(display_mode* mode, uint32* low, uint32* high)
{
	const display_mode& current = gInfo->shared_info->current_mode;
	*low = current.timing.pixel_clock;
	*high = current.timing.pixel_clock;
	return same_mode(*mode, current) ? B_OK : B_BAD_VALUE;
}


status_t
vesa_move_display(uint16 hDisplayStart, uint16 vDisplayStart)
{
	return B_ERROR;
}


status_t
vesa_get_timing_constraints(display_timing_constraints* constraints)
{
	return B_ERROR;
}


void
vesa_set_indexed_colors(uint count, uint8 first, uint8* colorData, uint32 flags)
{
}
