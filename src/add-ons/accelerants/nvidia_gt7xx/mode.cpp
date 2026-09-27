/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */


#include <stdlib.h>
#include <string.h>

#include <create_display_modes.h>

#include "accelerant_protos.h"
#include "accelerant.h"


static uint32
pack_xy(uint32 x, uint32 y)
{
	return (x & 0x7fff) | ((y & 0x7fff) << 16);
}


static uint32
pack_pixel_clock(const display_mode& mode)
{
	return (mode.timing.pixel_clock & 0x003fffff) | (2 << 22);
}


static uint32
pack_head_control(const display_mode& mode)
{
	uint32 value = 0;
	if ((mode.timing.flags & B_TIMING_INTERLACED) != 0)
		value |= (1 << 1);
	return value;
}


static uint32
bytes_per_pixel_for_space(color_space space)
{
	switch (space) {
		case B_CMAP8:
			return 1;
		case B_RGB15_LITTLE:
		case B_RGB16_LITTLE:
			return 2;
		case B_RGB24_LITTLE:
			return 3;
		case B_RGB32_LITTLE:
		default:
			return 4;
	}
}


static uint32
format_for_space(color_space space)
{
	switch (space) {
		case B_CMAP8:
			return 0x1e;
		case B_RGB15_LITTLE:
			return 0xe9;
		case B_RGB16_LITTLE:
			return 0xe8;
		case B_RGB32_LITTLE:
		default:
			return 0xcf;
	}
}


static uint32
pack_output_control(const display_mode& mode, uint8 outputType, uint8 head)
{
	uint32 owner = head + 1;
	uint32 value = owner;

	if ((mode.timing.flags & B_POSITIVE_HSYNC) != 0)
		value |= (1 << 12);
	if ((mode.timing.flags & B_POSITIVE_VSYNC) != 0)
		value |= (1 << 13);

	if (outputType == NVIDIA_GT7XX_OUTPUT_DIGITAL)
		value |= (1 << 8);

	return value;
}


static uint32
pack_dac_polarity(const display_mode& mode)
{
	uint32 value = 0;
	if ((mode.timing.flags & B_POSITIVE_HSYNC) == 0)
		value |= 1;
	if ((mode.timing.flags & B_POSITIVE_VSYNC) == 0)
		value |= 2;
	return value;
}


static uint32
pack_base_storage(uint32 bytesPerRow)
{
	uint32 pitch = (bytesPerRow + 0xff) & ~0xff;
	return ((pitch >> 8) & 0x3ff) << 8 | (1 << 20);
}


static uint32
pack_base_params(color_space space)
{
	return format_for_space(space) << 8;
}


static uint32
vertical_blank_duration(const display_mode& mode)
{
	if (mode.timing.pixel_clock == 0 || mode.timing.h_total == 0
		|| mode.timing.v_total <= mode.timing.v_display) {
		return 0;
	}

	uint64 lineNanoseconds = ((uint64)mode.timing.h_total * 1000000ULL
		+ mode.timing.pixel_clock / 2) / mode.timing.pixel_clock;
	uint64 blankMicros = ((uint64)(mode.timing.v_total - mode.timing.v_display)
		* lineNanoseconds + 999ULL) / 1000ULL;
	return blankMicros > 0xfff ? 0xfff : (uint32)blankMicros;
}


static status_t
submit_evo_mode_sequence(const display_mode& mode)
{
	nvidia_gt7xx_evo_push push = {};
	push.magic = NVIDIA_GT7XX_EVO_MAGIC;
	push.channel = NVIDIA_GT7XX_EVO_CHANNEL_CORE;

	const uint8 head = gInfo->shared_info->active_head;
	const uint8 output = 0;
	const uint32 bytesPerRow = gInfo->shared_info->fbc.bytes_per_row != 0
		? gInfo->shared_info->fbc.bytes_per_row
		: mode.virtual_width * bytes_per_pixel_for_space((color_space)mode.space);

	if (gInfo->shared_info->active_output == NVIDIA_GT7XX_OUTPUT_DIGITAL) {
		push.methods[push.count++] = {
			NVIDIA_GT7XX_EVO_SOR_SET_CONTROL(output),
			pack_output_control(mode, gInfo->shared_info->active_output, head)
		};
	} else {
		push.methods[push.count++] = {
			NVIDIA_GT7XX_EVO_DAC_SET_CONTROL(output),
			pack_output_control(mode, gInfo->shared_info->active_output, head)
		};
		push.methods[push.count++] = {
			NVIDIA_GT7XX_EVO_DAC_SET_POLARITY(output),
			pack_dac_polarity(mode)
		};
	}

	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_PIXEL_CLOCK(head),
		pack_pixel_clock(mode)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_CONTROL(head),
		pack_head_control(mode)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_OVERSCAN_COLOR(head),
		0
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SIZE(head),
		pack_xy(mode.timing.h_display, mode.timing.v_display)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SYNC_END(head),
		pack_xy(mode.timing.h_sync_end, mode.timing.v_sync_end)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_END(head),
		pack_xy(mode.timing.h_sync_start, mode.timing.v_sync_start)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_START(head),
		pack_xy(mode.timing.h_total, mode.timing.v_total)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK2(head),
		pack_xy(mode.timing.v_total, mode.timing.v_total)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK_DMI(head),
		vertical_blank_duration(mode)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_DEFAULT_BASE_COLOR(head),
		0
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_OFFSET(head, 0),
		0
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_SIZE(head),
		pack_xy(mode.virtual_width, mode.virtual_height)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_STORAGE(head),
		pack_base_storage(bytesPerRow)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_PARAMS(head),
		pack_base_params((color_space)mode.space)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_UPDATE,
		0
	};

	debug_printf("nvidia_gt7xx.accelerant: submit_evo_mode_sequence mode=%ux%u"
		" space=0x%08" B_PRIx32 " head=%u output=%u bpr=%" B_PRIu32
		" methods=%" B_PRIu32 "\n",
		mode.virtual_width, mode.virtual_height, mode.space, head,
		gInfo->shared_info->active_output, bytesPerRow, push.count);
	for (uint32 i = 0; i < push.count; i++) {
		debug_printf("nvidia_gt7xx.accelerant:   m[%02" B_PRIu32 "] method=0x%08"
			B_PRIx32 " value=0x%08" B_PRIx32 "\n",
			i, push.methods[i].method, push.methods[i].value);
	}

	status_t status = ioctl(gInfo->device, NVIDIA_GT7XX_SUBMIT_EVO, &push, sizeof(push));
	debug_printf("nvidia_gt7xx.accelerant: submit_evo_mode_sequence ioctl status=%"
		B_PRId32 "\n", status);
	return status;
}


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
nvidia_gt7xx_accelerant_mode_count(void)
{
	return gInfo->shared_info->mode_count;
}


status_t
nvidia_gt7xx_get_mode_list(display_mode* modeList)
{
	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
nvidia_gt7xx_propose_display_mode(display_mode* target, const display_mode* low,
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
nvidia_gt7xx_set_display_mode(display_mode* modeToSet)
{
	display_mode mode = *modeToSet;
	debug_printf("nvidia_gt7xx.accelerant: set_display_mode request=%ux%u"
		" space=0x%08" B_PRIx32 " pixel_clock=%" B_PRIu32 "\n",
		mode.virtual_width, mode.virtual_height, mode.space,
		mode.timing.pixel_clock);
	status_t status = nvidia_gt7xx_propose_display_mode(&mode, &mode, &mode);
	if (status != B_OK)
		debug_printf("nvidia_gt7xx.accelerant: propose_display_mode failed status=%"
			B_PRId32 "\n", status);
	if (status != B_OK)
		return status;

	status = submit_evo_mode_sequence(mode);
	if (status == B_OK) {
		gInfo->shared_info->current_mode = mode;
		debug_printf("nvidia_gt7xx.accelerant: set_display_mode applied=%ux%u"
			" space=0x%08" B_PRIx32 "\n",
			mode.virtual_width, mode.virtual_height, mode.space);
	} else {
		debug_printf("nvidia_gt7xx.accelerant: set_display_mode submit failed status=%"
			B_PRId32 "\n", status);
	}

	return status;
}


status_t
nvidia_gt7xx_get_display_mode(display_mode* currentMode)
{
	*currentMode = gInfo->shared_info->current_mode;
	return B_OK;
}


status_t
nvidia_gt7xx_get_edid_info(void* info, size_t size, uint32* version)
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
nvidia_gt7xx_get_frame_buffer_config(frame_buffer_config* config)
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
nvidia_gt7xx_get_pixel_clock_limits(display_mode* mode, uint32* low, uint32* high)
{
	const display_mode& current = gInfo->shared_info->current_mode;
	*low = current.timing.pixel_clock;
	*high = current.timing.pixel_clock;
	return same_mode(*mode, current) ? B_OK : B_BAD_VALUE;
}


status_t
nvidia_gt7xx_move_display(uint16 hDisplayStart, uint16 vDisplayStart)
{
	return B_ERROR;
}


status_t
nvidia_gt7xx_get_timing_constraints(display_timing_constraints* constraints)
{
	return B_ERROR;
}


void
nvidia_gt7xx_set_indexed_colors(uint count, uint8 first, uint8* colorData, uint32 flags)
{
}
