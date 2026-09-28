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
	// mode.timing.pixel_clock is in kHz, GF119- expects Hz in [30:0]
	return (uint32)mode.timing.pixel_clock * 1000U;
}


static uint32
pack_pixel_clock_config(void)
{
	// Mode 2 = CUSTOM (bits 20-21)
	return (2U << 20);
}


static uint32
pack_head_control(const display_mode& mode)
{
	uint32 value = 0;
	if ((mode.timing.flags & B_TIMING_INTERLACED) != 0)
		value |= 1; // bit 0 in GF119-
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
pack_head_output_resource(const display_mode& mode)
{
	uint32 value = (0x5 << 6); // 24bpp 4:4:4
	if ((mode.timing.flags & B_POSITIVE_HSYNC) == 0)
		value |= (1 << 3);
	if ((mode.timing.flags & B_POSITIVE_VSYNC) == 0)
		value |= (1 << 4);
	return value;
}


static uint32
pack_sor_control(uint8 head)
{
	uint32 ownerMask = (1 << head) & 0xf;
	uint32 protocol = (0x1 << 8); // SINGLE_TMDS_A
	return ownerMask | protocol;
}


static uint32
pack_dac_control(uint8 head)
{
	uint32 ownerMask = (1 << head) & 0xf;
	return ownerMask;
}


static uint32
pack_base_storage(uint32 bytesPerRow)
{
	uint32 pitch = (bytesPerRow + 0x3f) & ~0x3f;
	return ((pitch >> 4) << 8) | (1 << 24); // bit 24 = LINEAR on GF119-
}


static uint32
pack_base_params(color_space space)
{
	return format_for_space(space) << 8;
}


static const char*
method_name(uint32 method)
{
	if (method == NVIDIA_GT7XX_EVO_UPDATE)
		return "UPDATE";
	if ((method & ~0x60) == NVIDIA_GT7XX_EVO_DAC_SET_CONTROL(0))
		return "DAC_SET_CONTROL";
	if ((method & ~0xe0) == NVIDIA_GT7XX_EVO_SOR_SET_CONTROL(0))
		return "SOR_SET_CONTROL";

	uint32 headMethod = method & 0xff;
	switch (headMethod) {
		case 0x04: return "HEAD_OUTPUT_RESOURCE";
		case 0x08: return "HEAD_CONTROL";
		case 0x10: return "HEAD_OVERSCAN_COLOR";
		case 0x14: return "HEAD_RASTER_SIZE";
		case 0x18: return "HEAD_RASTER_SYNC_END";
		case 0x1c: return "HEAD_RASTER_BLANK_END";
		case 0x20: return "HEAD_RASTER_BLANK_START";
		case 0x24: return "HEAD_RASTER_VERT_BLANK2";
		case 0x2c: return "HEAD_DEFAULT_BASE_COLOR";
		case 0x50: return "HEAD_PIXEL_CLOCK";
		case 0x54: return "HEAD_PIXEL_CLOCK_CONFIG";
		case 0x60: return "HEAD_OFFSET";
		case 0x68: return "HEAD_SIZE";
		case 0x6c: return "HEAD_STORAGE";
		case 0x70: return "HEAD_PARAMS";
		default:   return "UNKNOWN";
	}
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
			pack_sor_control(head)
		};
	} else {
		push.methods[push.count++] = {
			NVIDIA_GT7XX_EVO_DAC_SET_CONTROL(output),
			pack_dac_control(head)
		};
	}

	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_OUTPUT_RESOURCE(head),
		pack_head_output_resource(mode)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_PIXEL_CLOCK(head),
		pack_pixel_clock(mode)
	};
	push.methods[push.count++] = {
		NVIDIA_GT7XX_EVO_HEAD_SET_PIXEL_CLOCK_CONFIGURATION(head),
		pack_pixel_clock_config()
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

	debug_printf("nvidia_gt7xx.accelerant: submit_evo_mode_sequence building sequence for %ux%u"
		" (clock=%" B_PRIu32 " kHz, %" B_PRIu32 " Hz, head=%u, output=%s, bpr=%" B_PRIu32 "):\n",
		mode.virtual_width, mode.virtual_height, mode.timing.pixel_clock,
		pack_pixel_clock(mode), head,
		gInfo->shared_info->active_output == NVIDIA_GT7XX_OUTPUT_DIGITAL ? "DIGITAL" : "ANALOG",
		bytesPerRow);

	for (uint32 i = 0; i < push.count; i++) {
		debug_printf("nvidia_gt7xx.accelerant:   [%02" B_PRIu32 "] 0x%08" B_PRIx32
			" (%-26s) = 0x%08" B_PRIx32 "\n",
			i, push.methods[i].method, method_name(push.methods[i].method),
			push.methods[i].value);
	}

	debug_printf("nvidia_gt7xx.accelerant: calling ioctl(NVIDIA_GT7XX_SUBMIT_EVO)...\n");
	status_t status = ioctl(gInfo->device, NVIDIA_GT7XX_SUBMIT_EVO, &push, sizeof(push));
	if (status == B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: ioctl(NVIDIA_GT7XX_SUBMIT_EVO) SUCCESS!\n");
	} else {
		debug_printf("nvidia_gt7xx.accelerant: ioctl(NVIDIA_GT7XX_SUBMIT_EVO) FAILED: %"
			B_PRId32 "\n", status);
	}

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
	debug_printf("nvidia_gt7xx.accelerant: create_mode_list initial mode: %ux%u space=0x%08"
		B_PRIx32 " (has_edid=%d)\n",
		initialMode.virtual_width, initialMode.virtual_height, initialMode.space,
		gInfo->shared_info->has_edid);

	gInfo->mode_list_area = create_display_modes("nvidia_gt7xx modes",
		gInfo->shared_info->has_edid ? &gInfo->shared_info->edid_info : NULL,
		&initialMode, 1, &space, 1, is_mode_supported,
		&gInfo->mode_list, &gInfo->shared_info->mode_count);

	if (gInfo->mode_list_area < 0) {
		debug_printf("nvidia_gt7xx.accelerant: create_display_modes failed: %" B_PRId32 "\n",
			gInfo->mode_list_area);
		return gInfo->mode_list_area;
	}

	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
	debug_printf("nvidia_gt7xx.accelerant: create_mode_list SUCCESS, mode_count=%" B_PRIu32 "\n",
		gInfo->shared_info->mode_count);
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
	debug_printf("nvidia_gt7xx.accelerant: propose_display_mode requested: %ux%u space=0x%08"
		B_PRIx32 "\n", target->virtual_width, target->virtual_height, target->space);

	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		if (target->virtual_width != gInfo->mode_list[i].virtual_width
			|| target->virtual_height != gInfo->mode_list[i].virtual_height
			|| target->space != gInfo->mode_list[i].space) {
			continue;
		}

		*target = gInfo->mode_list[i];
		debug_printf("nvidia_gt7xx.accelerant: propose_display_mode MATCHED mode [%" B_PRIu32
			"]: %ux%u clock=%" B_PRIu32 " kHz\n",
			i, target->virtual_width, target->virtual_height, target->timing.pixel_clock);
		return B_OK;
	}

	debug_printf("nvidia_gt7xx.accelerant: propose_display_mode NO MATCH found\n");
	return B_BAD_VALUE;
}


status_t
nvidia_gt7xx_set_display_mode(display_mode* modeToSet)
{
	display_mode mode = *modeToSet;
	debug_printf("nvidia_gt7xx.accelerant: set_display_mode request: %ux%u"
		" space=0x%08" B_PRIx32 " pixel_clock=%" B_PRIu32 " kHz\n",
		mode.virtual_width, mode.virtual_height, mode.space,
		mode.timing.pixel_clock);

	status_t status = nvidia_gt7xx_propose_display_mode(&mode, &mode, &mode);
	if (status != B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: set_display_mode propose_display_mode FAILED: %"
			B_PRId32 "\n", status);
		return status;
	}

	status = submit_evo_mode_sequence(mode);
	if (status == B_OK) {
		gInfo->shared_info->current_mode = mode;
		debug_printf("nvidia_gt7xx.accelerant: set_display_mode APPLIED: %ux%u"
			" space=0x%08" B_PRIx32 " clock=%" B_PRIu32 " kHz\n",
			mode.virtual_width, mode.virtual_height, mode.space,
			mode.timing.pixel_clock);
	} else {
		debug_printf("nvidia_gt7xx.accelerant: set_display_mode submit FAILED: %"
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
