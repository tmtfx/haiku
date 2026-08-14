/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * This accelerant is intentionally minimal but functional. Structure and
 * API usage were derived only from MIT-compatible Haiku sources:
 *  - src/add-ons/accelerants/framebuffer/accelerant.cpp
 *  - src/add-ons/accelerants/framebuffer/hooks.cpp
 *  - src/add-ons/accelerants/framebuffer/mode.cpp
 *  - src/add-ons/accelerants/intel_extreme/accelerant.cpp
 *
 * No Linux/GPL code was copied.
 */

#include "accelerant_protos.h"
#include "accelerant.h"

#include <compute_display_timing.h>
#include <create_display_modes.h>
#include <ddc.h>
#include <dp.h>

#include <edid.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AutoDeleterOS.h>


accelerant_info* gInfo;

/*
 * AUX/DDC implementation below is a standalone procedural adaptation of the
 * MIT-licensed logic in src/add-ons/accelerants/intel_extreme/Ports.cpp.
 * It intentionally keeps only the pieces needed for EDID/DPCD reads on ARC.
 */

#define INTEL_ARC_MMIO_PIPE_BLOCK_BASE			0x60000
#define INTEL_ARC_MMIO_PIPE_OFFSET				0x1000
#define INTEL_ARC_MMIO_AUX_CH_CTL_A				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4010)
#define INTEL_ARC_MMIO_AUX_CH_DATA1_A			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4014)
#define INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE		0x100
#define INTEL_ARC_MMIO_PIPE_A_HTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0000)
#define INTEL_ARC_MMIO_PIPE_A_HBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0004)
#define INTEL_ARC_MMIO_PIPE_A_HSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PIPE_A_VTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x000c)
#define INTEL_ARC_MMIO_PIPE_A_VBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0010)
#define INTEL_ARC_MMIO_PIPE_A_VSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0014)
#define INTEL_ARC_MMIO_PIPE_A_SIZE				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x001c)
#define INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0400)
#define INTEL_ARC_MMIO_DDI_PIPE_A_DATA_M		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0030)
#define INTEL_ARC_MMIO_DDI_PIPE_A_DATA_N		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0034)
#define INTEL_ARC_MMIO_DDI_PIPE_A_LINK_M		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0040)
#define INTEL_ARC_MMIO_DDI_PIPE_A_LINK_N		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0044)
#define INTEL_ARC_MMIO_PLANE_BLOCK_BASE			0x70000
#define INTEL_ARC_MMIO_PIPE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PLANE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0180)
#define INTEL_ARC_MMIO_PLANE_A_BASE				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0184)
#define INTEL_ARC_MMIO_PLANE_A_STRIDE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0188)
#define INTEL_ARC_MMIO_PLANE_A_POS				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x018c)
#define INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE		(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0190)
#define INTEL_ARC_MMIO_PLANE_A_SURFACE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x019c)

#define INTEL_ARC_DP_AUX_CTL_BUSY				(1U << 31)
#define INTEL_ARC_DP_AUX_CTL_DONE				(1U << 30)
#define INTEL_ARC_DP_AUX_CTL_INTERRUPT			(1U << 29)
#define INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR		(1U << 28)
#define INTEL_ARC_DP_AUX_CTL_TIMEOUT_1600US		(3U << 26)
#define INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR		(1U << 25)
#define INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT		20
#define INTEL_ARC_DP_AUX_CTL_MSG_SIZE_MASK		(0x1fU << 20)
#define INTEL_ARC_DP_AUX_CTL_FW_SYNC_PULSE_SKL(c) (((c) - 1) << 5)
#define INTEL_ARC_DP_AUX_CTL_SYNC_PULSE_SKL(c)	((c) - 1)
#define INTEL_ARC_DDI_MN_TU_SIZE_MASK			(0x3fU << 25)
#define INTEL_ARC_PIPE_ENABLED					(1U << 31)
#define INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE		(1U << 31)
#define INTEL_ARC_PIPE_DDI_SELECT_MASK			(7U << 28)
#define INTEL_ARC_PIPE_DDI_MODESEL_MASK			(7U << 24)
#define INTEL_ARC_PIPE_DDI_MODE_DP_SST			2U
#define INTEL_ARC_PIPE_DDI_MODE_DP_MST			3U
#define INTEL_ARC_PIPE_DDI_BPC_MASK				(7U << 20)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK		(7U << 1)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT		1
#define INTEL_ARC_DISPLAY_CONTROL_ENABLED		(1U << 31)
#define INTEL_ARC_DISPLAY_CONTROL_COLOR_MASK_SKY (0x0fU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_CMAP8_SKY		(0x0cU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_RGB16_SKY		(0x0eU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_RGB32_SKY		(0x04U << 24)


static bool read_register(uint32 offset, uint32& value);
static void write_register(uint32 offset, uint32 value);
static status_t wait_for_clear(uint32 offset, uint32 mask, bigtime_t timeout);
static status_t wait_for_set(uint32 offset, uint32 mask, bigtime_t timeout);
static uint32 aux_control_register(uint8 ddiPort);
static uint32 aux_data_register(uint8 ddiPort, uint8 index);
static uint32 pipe_register(uint32 base, int8 pipe);
static status_t apply_dpms_off(void);
static status_t apply_dpms_on(void);
static bool is_mode_in_list(const display_mode& mode, display_mode* match = NULL);
static uint32 bytes_per_pixel_for_space(color_space space);
static uint32 plane_color_format_for_space(color_space space);
static status_t configure_dp_link(display_mode* mode);
static status_t set_sink_power(uint8 ddiPort, uint8 value);
static status_t read_edid_from_hardware(void);
static status_t read_edid_from_port(uint8 ddiPort, edid1_info& edid);
static status_t read_dpcd_caps_from_port(uint8 ddiPort);
static status_t aux_send_receive(const i2c_bus* bus, uint32 slaveAddress,
	const uint8* writeBuffer, size_t writeLength, uint8* readBuffer,
	size_t readLength);
static ssize_t aux_transfer(uint8 ddiPort, dp_aux_msg* message);
static ssize_t aux_transfer(uint8 ddiPort, uint8* transmitBuffer,
	uint8 transmitSize, uint8* receiveBuffer, uint8 receiveSize);


static bool
operator==(const display_mode& left, const display_mode& right)
{
	return left.space == right.space
		&& left.virtual_width == right.virtual_width
		&& left.virtual_height == right.virtual_height
		&& left.h_display_start == right.h_display_start
		&& left.v_display_start == right.v_display_start;
}


static bool
is_mode_supported(display_mode* mode)
{
	return mode != NULL && *mode == gInfo->shared_info->current_mode;
}


static status_t
create_mode_list(void)
{
	display_mode mode = gInfo->shared_info->current_mode;
	const color_space kSupportedSpaces[] = {
		B_RGB32_LITTLE, B_RGB16_LITTLE, B_CMAP8
	};
	if (mode.virtual_width == 0 || mode.virtual_height == 0
		|| mode.space == B_NO_COLOR_SPACE) {
		mode.virtual_width = 1024;
		mode.virtual_height = 768;
		mode.space = B_RGB32;
		compute_display_timing(mode.virtual_width, mode.virtual_height, 60,
			false, &mode.timing);
		if (gInfo->shared_info->bytes_per_row == 0)
			gInfo->shared_info->bytes_per_row = mode.virtual_width * 4;
	} else {
		compute_display_timing(mode.virtual_width, mode.virtual_height, 60,
			false, &mode.timing);
	}
	gInfo->shared_info->current_mode = mode;
	if (gInfo->has_edid) {
		gInfo->mode_list_area = create_display_modes("intel arc modes",
			&gInfo->edid_info, NULL, 0, kSupportedSpaces,
			sizeof(kSupportedSpaces) / sizeof(kSupportedSpaces[0]), NULL,
			&gInfo->mode_list, &gInfo->shared_info->mode_count);
	} else {
		fill_display_mode(mode.virtual_width, mode.virtual_height, &mode);
		gInfo->mode_list_area = create_display_modes("intel arc modes", NULL,
			&mode, 1, kSupportedSpaces,
			sizeof(kSupportedSpaces) / sizeof(kSupportedSpaces[0]),
			is_mode_supported, &gInfo->mode_list, &gInfo->shared_info->mode_count);
	}
	if (gInfo->mode_list_area < B_OK)
		return gInfo->mode_list_area;

	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
	return B_OK;
}


static status_t
init_common(int device, bool isClone)
{
	gInfo = (accelerant_info*)malloc(sizeof(accelerant_info));
	if (gInfo == NULL)
		return B_NO_MEMORY;
	MemoryDeleter infoDeleter(gInfo);

	memset(gInfo, 0, sizeof(accelerant_info));
	gInfo->device = device;
	gInfo->is_clone = isClone;
	gInfo->shared_info_area = -1;
	gInfo->regs_area = -1;
	gInfo->mode_list_area = -1;
	gInfo->frame_buffer_area = -1;
	gInfo->has_edid = false;

	intel_arc_get_private_data data;
	data.magic = INTEL_ARC_PRIVATE_DATA_MAGIC;
	if (ioctl(device, INTEL_ARC_GET_PRIVATE_DATA, &data, sizeof(data)) != 0)
		return B_ERROR;

	AreaDeleter sharedDeleter(clone_area("intel arc shared info",
		(void**)&gInfo->shared_info, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.shared_info_area));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK)
		return status;

	if (gInfo->shared_info->registers_area >= B_OK) {
		AreaDeleter regsDeleter(clone_area("intel arc regs", (void**)&gInfo->registers,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			gInfo->shared_info->registers_area));
		status = gInfo->regs_area = regsDeleter.Get();
		if (status < B_OK)
			return status;
		regsDeleter.Detach();
	}

	infoDeleter.Detach();
	sharedDeleter.Detach();
	return B_OK;
}


static void
uninit_common(void)
{
	if (gInfo->frame_buffer_area >= B_OK)
		delete_area(gInfo->frame_buffer_area);
	if (gInfo->regs_area >= B_OK)
		delete_area(gInfo->regs_area);
	if (gInfo->shared_info_area >= B_OK)
		delete_area(gInfo->shared_info_area);

	if (gInfo->is_clone)
		close(gInfo->device);

	free(gInfo);
	gInfo = NULL;
}


status_t
intel_arc_init_accelerant(int device)
{
	status_t status = init_common(device, false);
	if (status != B_OK)
		return status;

	read_edid_from_hardware();

	status = create_mode_list();
	if (status != B_OK) {
		uninit_common();
		return status;
	}

	area_info info;
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info,
		sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
	}

	return B_OK;
}


ssize_t
intel_arc_accelerant_clone_info_size(void)
{
	return B_PATH_NAME_LENGTH;
}


void
intel_arc_get_accelerant_clone_info(void* info)
{
	ioctl(gInfo->device, INTEL_ARC_GET_DEVICE_NAME, info, B_PATH_NAME_LENGTH);
}


status_t
intel_arc_clone_accelerant(void* data)
{
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), "/dev/%s", (const char*)data);

	int fd = open(path, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK) {
		close(fd);
		return status;
	}

	read_edid_from_hardware();

	status = gInfo->mode_list_area = clone_area("intel arc cloned modes",
		(void**)&gInfo->mode_list, B_ANY_ADDRESS, B_READ_AREA,
		gInfo->shared_info->mode_list_area);
	if (status < B_OK) {
		uninit_common();
		return status;
	}

	area_info info;
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info,
		sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
	}

	return B_OK;
}


void
intel_arc_uninit_accelerant(void)
{
	if (gInfo->mode_list_area >= B_OK)
		delete_area(gInfo->mode_list_area);
	uninit_common();
}


status_t
intel_arc_get_accelerant_device_info(accelerant_device_info* info)
{
	info->version = B_ACCELERANT_VERSION;
	snprintf(info->name, sizeof(info->name), "Intel ARC");
	snprintf(info->chipset, sizeof(info->chipset), "%s",
		gInfo->shared_info->device_identifier);
	snprintf(info->serial_no, sizeof(info->serial_no), "%04x:%04x",
		gInfo->shared_info->vendor_id, gInfo->shared_info->device_id);
	info->memory = gInfo->shared_info->frame_buffer_size > 0xffffffffULL
		? 0xffffffffU : (uint32)gInfo->shared_info->frame_buffer_size;
	info->dac_speed = 0;
	return B_OK;
}


sem_id
intel_arc_accelerant_retrace_semaphore(void)
{
	return -1;
}


uint32
intel_arc_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_OFF;
}


uint32
intel_arc_dpms_mode(void)
{
	return gInfo->shared_info->dpms_mode;
}


status_t
intel_arc_set_dpms_mode(uint32 mode)
{
	switch (mode) {
		case B_DPMS_ON:
			return apply_dpms_on();
		case B_DPMS_OFF:
			return apply_dpms_off();
		default:
			return B_UNSUPPORTED;
	}
}


uint32
intel_arc_accelerant_mode_count(void)
{
	return gInfo->shared_info->mode_count;
}


status_t
intel_arc_get_mode_list(display_mode* modeList)
{
	if (gInfo->shared_info->mode_count == 0)
		return B_ENTRY_NOT_FOUND;

	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
intel_arc_propose_display_mode(display_mode* target, display_mode* low,
	display_mode* high)
{
	(void)low;
	(void)high;

	display_mode match;
	if (!is_mode_in_list(*target, &match))
		return B_BAD_VALUE;

	*target = match;
	return B_OK;
}


status_t
intel_arc_get_preferred_mode(display_mode* mode)
{
	if (gInfo->shared_info->mode_count == 0)
		return B_ENTRY_NOT_FOUND;

	*mode = gInfo->mode_list[0];
	return B_OK;
}


status_t
intel_arc_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (*mode == gInfo->shared_info->current_mode)
		return B_OK;

	display_mode target = *mode;
	status_t status = intel_arc_propose_display_mode(&target, &target, &target);
	if (status != B_OK)
		return status;

	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	status = apply_dpms_off();
	if (status != B_OK)
		return status;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 bytesPerPixel
		= bytes_per_pixel_for_space((color_space)target.space);
	if (bytesPerPixel == 0)
		return B_BAD_VALUE;

	gInfo->shared_info->current_mode = target;
	gInfo->shared_info->bytes_per_row = target.virtual_width * bytesPerPixel;
	gInfo->shared_info->pipe_h_total[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->pipe_h_blank[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->pipe_h_sync[pipe]
		= ((uint32)(target.timing.h_sync_end - 1) << 16)
			| ((uint32)target.timing.h_sync_start - 1);
	gInfo->shared_info->pipe_v_total[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16)
			| ((uint32)target.timing.v_display - 1);
	gInfo->shared_info->pipe_v_blank[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16)
			| ((uint32)target.timing.v_display - 1);
	gInfo->shared_info->pipe_v_sync[pipe]
		= ((uint32)(target.timing.v_sync_end - 1) << 16)
			| ((uint32)target.timing.v_sync_start - 1);
	gInfo->shared_info->pipe_size[pipe]
		= ((uint32)(target.timing.v_display - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->plane_stride[pipe] = gInfo->shared_info->bytes_per_row;
	gInfo->shared_info->plane_pos[pipe] = 0;
	gInfo->shared_info->plane_image_size[pipe]
		= ((uint32)(target.timing.v_display - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->plane_control[pipe]
		= (gInfo->shared_info->plane_control[pipe]
			& ~INTEL_ARC_DISPLAY_CONTROL_COLOR_MASK_SKY)
		| plane_color_format_for_space((color_space)target.space);

	write_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL + pipeOffset,
		gInfo->shared_info->pipe_h_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HBLANK + pipeOffset,
		gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HSYNC + pipeOffset,
		gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL + pipeOffset,
		gInfo->shared_info->pipe_v_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VBLANK + pipeOffset,
		gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VSYNC + pipeOffset,
		gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_SIZE + pipeOffset,
		gInfo->shared_info->pipe_size[pipe]);

	if ((((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24)
			== INTEL_ARC_PIPE_DDI_MODE_DP_SST)
		|| (((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24)
			== INTEL_ARC_PIPE_DDI_MODE_DP_MST)) {
		status = configure_dp_link(&target);
		if (status != B_OK)
			return status;
	}

	status = apply_dpms_on();
	if (status == B_OK)
		*mode = target;
	return status;
}


status_t
intel_arc_get_display_mode(display_mode* mode)
{
	*mode = gInfo->shared_info->current_mode;
	return B_OK;
}


status_t
intel_arc_get_edid_info(void* info, size_t size, uint32* version)
{
	if (!gInfo->has_edid)
		return B_ERROR;
	if (size < sizeof(edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &gInfo->edid_info, sizeof(edid1_info));
	*version = EDID_VERSION_1;
	return B_OK;
}


status_t
intel_arc_get_frame_buffer_config(frame_buffer_config* config)
{
	if (gInfo->frame_buffer == NULL)
		return B_UNSUPPORTED;

	config->frame_buffer = gInfo->frame_buffer;
	config->frame_buffer_dma = NULL;
	config->bytes_per_row = gInfo->shared_info->bytes_per_row;
	return B_OK;
}


status_t
intel_arc_get_pixel_clock_limits(display_mode* mode, uint32* low, uint32* high)
{
	uint32 totalPixel = (uint32)mode->timing.h_total
		* (uint32)mode->timing.v_total;
	uint32 clockLimit = 2000000;

	*low = totalPixel * 48L / 1000L;
	if (*low > clockLimit)
		return B_ERROR;

	*high = clockLimit;
	return B_OK;
}


extern "C" void*
get_accelerant_hook(uint32 feature, void* /*data*/)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)intel_arc_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)intel_arc_uninit_accelerant;
		case B_CLONE_ACCELERANT:
			return (void*)intel_arc_clone_accelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)intel_arc_accelerant_clone_info_size;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)intel_arc_get_accelerant_clone_info;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)intel_arc_get_accelerant_device_info;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)intel_arc_accelerant_retrace_semaphore;
		case B_DPMS_CAPABILITIES:
			return (void*)intel_arc_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)intel_arc_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)intel_arc_set_dpms_mode;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)intel_arc_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)intel_arc_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)intel_arc_propose_display_mode;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)intel_arc_get_preferred_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)intel_arc_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)intel_arc_get_display_mode;
		case B_GET_EDID_INFO:
			return (void*)intel_arc_get_edid_info;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)intel_arc_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)intel_arc_get_pixel_clock_limits;
	}

	return NULL;
}


static bool
read_register(uint32 offset, uint32& value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return false;
	}

	value = *(volatile uint32*)(gInfo->registers + offset);
	return true;
}


static void
write_register(uint32 offset, uint32 value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return;
	}

	*(volatile uint32*)(gInfo->registers + offset) = value;
}


static status_t
wait_for_clear(uint32 offset, uint32 mask, bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	uint32 value = 0;
	while (system_time() < deadline) {
		if (!read_register(offset, value))
			return B_ERROR;
		if ((value & mask) == 0)
			return B_OK;
		snooze(100);
	}
	return B_TIMED_OUT;
}


static status_t
wait_for_set(uint32 offset, uint32 mask, bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	uint32 value = 0;
	while (system_time() < deadline) {
		if (!read_register(offset, value))
			return B_ERROR;
		if ((value & mask) == mask)
			return B_OK;
		snooze(100);
	}
	return B_TIMED_OUT;
}


static uint32
pipe_register(uint32 base, int8 pipe)
{
	return base + (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
}


static bool
is_mode_in_list(const display_mode& mode, display_mode* match)
{
	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		const display_mode& current = gInfo->mode_list[i];
		if (current.virtual_width != mode.virtual_width
			|| current.virtual_height != mode.virtual_height
			|| current.space != mode.space) {
			continue;
		}

		if (match != NULL)
			*match = current;
		return true;
	}

	return false;
}


static uint32
bytes_per_pixel_for_space(color_space space)
{
	switch (space) {
		case B_CMAP8:
		case B_GRAY8:
			return 1;
		case B_RGB16_LITTLE:
		case B_RGB16_BIG:
		case B_RGB15_LITTLE:
		case B_RGB15_BIG:
			return 2;
		case B_RGB24_LITTLE:
		case B_RGB24_BIG:
			return 3;
		case B_RGB32_LITTLE:
		case B_RGB32_BIG:
		default:
			return 4;
	}
}


static uint32
plane_color_format_for_space(color_space space)
{
	switch (space) {
		case B_CMAP8:
			return INTEL_ARC_DISPLAY_CONTROL_CMAP8_SKY;
		case B_RGB16_LITTLE:
		case B_RGB16_BIG:
			return INTEL_ARC_DISPLAY_CONTROL_RGB16_SKY;
		case B_RGB32_LITTLE:
		case B_RGB32_BIG:
		default:
			return INTEL_ARC_DISPLAY_CONTROL_RGB32_SKY;
	}
}


static status_t
configure_dp_link(display_mode* mode)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 pipeFunc = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
	uint32 bitsPerPixel = 24;
	switch ((pipeFunc & INTEL_ARC_PIPE_DDI_BPC_MASK) >> 20) {
		case 0:
			bitsPerPixel = 24;
			break;
		case 1:
			bitsPerPixel = 30;
			break;
		case 2:
			bitsPerPixel = 18;
			break;
		case 3:
			bitsPerPixel = 36;
			break;
		default:
			break;
	}

	uint32 lanes
		= ((pipeFunc & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			>> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1;
	if (lanes == 0 || lanes > 4)
		lanes = 4;

	uint32 linkBandwidth = 270000;
	if (gInfo->shared_info->has_dpcd && gInfo->shared_info->dpcd_max_link_rate != 0)
		linkBandwidth = dp_decode_link_rate(gInfo->shared_info->dpcd_max_link_rate);

	const uint32 bps = mode->timing.pixel_clock * bitsPerPixel * 21 / 20;
	const uint32 requiredLanes = (bps + (linkBandwidth * 8) - 1)
		/ (linkBandwidth * 8);
	if (requiredLanes > lanes)
		return B_BAD_VALUE;

	uint64 linkSpeed = (uint64)lanes * linkBandwidth * 8;
	uint64 retN = 1;
	while (retN < linkSpeed)
		retN <<= 1;
	if (retN > 0x800000)
		retN = 0x800000;
	uint64 retM = (uint64)mode->timing.pixel_clock * retN * bitsPerPixel
		/ linkSpeed;
	while (retN > 0xffffff || retM > 0xffffff) {
		retN >>= 1;
		retM >>= 1;
	}
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_M + pipeOffset,
		(uint32)retM | INTEL_ARC_DDI_MN_TU_SIZE_MASK);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_N + pipeOffset,
		(uint32)retN);

	linkSpeed = linkBandwidth;
	retN = 1;
	while (retN < linkSpeed)
		retN <<= 1;
	if (retN > 0x800000)
		retN = 0x800000;
	retM = (uint64)mode->timing.pixel_clock * retN / linkSpeed;
	while (retN > 0xffffff || retM > 0xffffff) {
		retN >>= 1;
		retM >>= 1;
	}
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_M + pipeOffset, (uint32)retM);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_N + pipeOffset, (uint32)retN);

	if (gInfo->shared_info->has_dpcd) {
		uint8 value = dp_encode_link_rate(linkBandwidth);
		(void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

		dp_aux_msg message;
		memset(&message, 0, sizeof(message));
		message.address = DP_LINK_RATE;
		message.request = DP_AUX_NATIVE_WRITE;
		message.buffer = &value;
		message.size = 1;
		if (aux_transfer(gInfo->shared_info->active_ddi_port, &message) < B_OK)
			return B_ERROR;

		value = lanes & DP_LANE_COUNT_MASK;
		message.address = DP_LANE_COUNT;
		if (aux_transfer(gInfo->shared_info->active_ddi_port, &message) < B_OK)
			return B_ERROR;

		value = DP_TRAINING_PATTERN_DISABLE;
		message.address = DP_TRAINING_PATTERN_SET;
		if (aux_transfer(gInfo->shared_info->active_ddi_port, &message) < B_OK)
			return B_ERROR;
	}

	return B_OK;
}


static status_t
set_sink_power(uint8 ddiPort, uint8 value)
{
	if (!gInfo->shared_info->has_dpcd || ddiPort == 0)
		return B_OK;

	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	message.address = DP_SET_POWER;
	message.request = DP_AUX_NATIVE_WRITE;
	message.buffer = &value;
	message.size = 1;
	ssize_t result = aux_transfer(ddiPort, &message);
	return result < B_OK ? (status_t)result : B_OK;
}


static status_t
apply_dpms_off(void)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
	const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
	const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);

	write_register(planeControlReg,
		gInfo->shared_info->plane_control[pipe] & ~INTEL_ARC_DISPLAY_CONTROL_ENABLED);
	(void)wait_for_clear(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

	write_register(pipeDdiReg,
		gInfo->shared_info->pipe_ddi_func_ctl[pipe] & ~INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
	(void)wait_for_clear(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);

	write_register(pipeControlReg,
		gInfo->shared_info->pipe_control[pipe] & ~INTEL_ARC_PIPE_ENABLED);
	(void)wait_for_clear(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

	(void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D3);
	gInfo->shared_info->dpms_mode = B_DPMS_OFF;
	return B_OK;
}


static status_t
apply_dpms_on(void)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;

	(void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL, pipe),
		gInfo->shared_info->pipe_h_total[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HBLANK, pipe),
		gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HSYNC, pipe),
		gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL, pipe),
		gInfo->shared_info->pipe_v_total[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VBLANK, pipe),
		gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VSYNC, pipe),
		gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_SIZE, pipe),
		gInfo->shared_info->pipe_size[pipe]);

	const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);
	const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
	const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
	const uint32 planeBaseReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_BASE, pipe);
	const uint32 planeStrideReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_STRIDE, pipe);
	const uint32 planePosReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_POS, pipe);
	const uint32 planeImageReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE, pipe);
	const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);

	write_register(pipeControlReg,
		gInfo->shared_info->pipe_control[pipe] | INTEL_ARC_PIPE_ENABLED);
	(void)wait_for_set(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

	write_register(pipeDdiReg,
		gInfo->shared_info->pipe_ddi_func_ctl[pipe] | INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
	(void)wait_for_set(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);

	write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
	write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
	write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
	write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeBaseReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeControlReg,
		gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
	(void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

	gInfo->shared_info->dpms_mode = B_DPMS_ON;
	return B_OK;
}


static uint32
aux_control_register(uint8 ddiPort)
{
	return INTEL_ARC_MMIO_AUX_CH_CTL_A
		+ ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE;
}


static uint32
aux_data_register(uint8 ddiPort, uint8 index)
{
	return INTEL_ARC_MMIO_AUX_CH_DATA1_A
		+ ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE + index * 4;
}


static status_t
read_edid_from_hardware(void)
{
	if (gInfo->registers == NULL) {
		if (gInfo->shared_info->has_boot_edid) {
			memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid,
				sizeof(edid1_info));
			gInfo->has_edid = true;
			return B_OK;
		}

		gInfo->has_edid = false;
		return B_ENTRY_NOT_FOUND;
	}

	uint8 candidates[7];
	size_t candidateCount = 0;
	if (gInfo->shared_info->active_ddi_port != 0)
		candidates[candidateCount++] = gInfo->shared_info->active_ddi_port;

	for (uint8 port = 1; port <= 3; port++) {
		if ((gInfo->shared_info->detected_port_bits & (1 << port)) == 0)
			continue;

		bool alreadyQueued = false;
		for (size_t i = 0; i < candidateCount; i++) {
			if (candidates[i] == port) {
				alreadyQueued = true;
				break;
			}
		}
		if (!alreadyQueued)
			candidates[candidateCount++] = port;
	}

	for (uint8 port = 1; port <= 6; port++) {
		bool alreadyQueued = false;
		for (size_t i = 0; i < candidateCount; i++) {
			if (candidates[i] == port) {
				alreadyQueued = true;
				break;
			}
		}
		if (!alreadyQueued)
			candidates[candidateCount++] = port;
	}

	for (size_t i = 0; i < candidateCount; i++) {
		if (read_edid_from_port(candidates[i], gInfo->edid_info) == B_OK) {
			read_dpcd_caps_from_port(candidates[i]);
			gInfo->has_edid = true;
			return B_OK;
		}
	}
	if (gInfo->shared_info->has_boot_edid) {
		memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid,
			sizeof(edid1_info));
		gInfo->has_edid = true;
		return B_OK;
	}

	gInfo->has_edid = false;
	return B_ENTRY_NOT_FOUND;
}


static status_t
read_edid_from_port(uint8 ddiPort, edid1_info& edid)
{
	if (ddiPort > 6)
		return B_BAD_VALUE;

	i2c_bus bus;
	ddc2_init_timing(&bus);
	bus.cookie = (void*)(addr_t)ddiPort;
	bus.send_receive = &aux_send_receive;
	bus.set_signals = NULL;
	bus.get_signals = NULL;
	return ddc2_read_edid1(&bus, &edid, NULL, NULL);
}


static status_t
read_dpcd_caps_from_port(uint8 ddiPort)
{
	uint8 buffer[8];
	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	memset(buffer, 0, sizeof(buffer));

	message.address = DP_DPCD_REV;
	message.request = DP_AUX_NATIVE_READ;
	message.buffer = buffer;
	message.size = sizeof(buffer);

	ssize_t result = aux_transfer(ddiPort, &message);
	if (result < (ssize_t)sizeof(buffer))
		return result < B_OK ? (status_t)result : B_ERROR;

	gInfo->shared_info->has_dpcd = true;
	memcpy(gInfo->shared_info->dpcd, buffer, sizeof(buffer));
	gInfo->shared_info->dpcd_revision = buffer[0];
	gInfo->shared_info->dpcd_max_link_rate = buffer[1];
	gInfo->shared_info->dpcd_max_lane_count = buffer[2] & DP_MAX_LANE_COUNT_MASK;
	gInfo->shared_info->dpcd_sink_count = buffer[7] & DP_SINK_COUNT_MASK;
	return B_OK;
}


static status_t
aux_send_receive(const i2c_bus* bus, uint32 slaveAddress,
	const uint8* writeBuffer, size_t writeLength, uint8* readBuffer,
	size_t readLength)
{
	const uint8 ddiPort = (uint8)(addr_t)bus->cookie;
	const size_t transferLength = 16;
	dp_aux_msg message;
	memset(&message, 0, sizeof(message));

	if (writeBuffer != NULL) {
		message.address = slaveAddress;
		message.request = DP_AUX_I2C_WRITE;
		ssize_t result = aux_transfer(ddiPort, &message);
		if (result < B_OK)
			return result;

		for (size_t i = 0; i < writeLength;) {
			message.buffer = (void*)(writeBuffer + i);
			message.size = min_c(transferLength, writeLength - i);
			if (writeLength - i > transferLength)
				message.request |= DP_AUX_I2C_MOT;
			else
				message.request &= ~DP_AUX_I2C_MOT;

			for (int attempt = 0; attempt < 7; attempt++) {
				result = aux_transfer(ddiPort, &message);
				if (result < B_OK)
					return result;

				switch (message.reply & DP_AUX_I2C_REPLY_MASK) {
					case DP_AUX_I2C_REPLY_ACK:
						goto nextWrite;
					case DP_AUX_I2C_REPLY_NACK:
						return B_IO_ERROR;
					case DP_AUX_I2C_REPLY_DEFER:
						snooze(400);
						break;
					default:
						return B_ERROR;
				}
			}
nextWrite:
			i += message.size;
		}
	}

	if (readBuffer != NULL) {
		message.address = slaveAddress;
		message.buffer = NULL;
		message.request = DP_AUX_I2C_READ;
		ssize_t result = aux_transfer(ddiPort, &message);
		if (result < B_OK)
			return result;

		for (size_t i = 0; i < readLength;) {
			message.buffer = readBuffer + i;
			message.size = min_c(transferLength, readLength - i);
			if (readLength - i > transferLength)
				message.request |= DP_AUX_I2C_MOT;
			else
				message.request &= ~DP_AUX_I2C_MOT;

			for (int attempt = 0; attempt < 7; attempt++) {
				result = aux_transfer(ddiPort, &message);
				if (result < B_OK)
					return result;

				switch (message.reply & DP_AUX_I2C_REPLY_MASK) {
					case DP_AUX_I2C_REPLY_ACK:
						goto nextRead;
					case DP_AUX_I2C_REPLY_NACK:
						return B_IO_ERROR;
					case DP_AUX_I2C_REPLY_DEFER:
						snooze(400);
						break;
					default:
						return B_ERROR;
				}
			}
nextRead:
			if (result == 0)
				i += message.size;
			else
				i += min_c((size_t)result, message.size);
		}
	}

	return B_OK;
}


static ssize_t
aux_transfer(uint8 ddiPort, dp_aux_msg* message)
{
	if (message == NULL)
		return B_BAD_VALUE;
	if (message->size > 16)
		return B_BAD_VALUE;

	uint8 receiveBuffer[20];
	uint8 transmitBuffer[20];
	uint8 transmitSize = message->size > 0 ? 4 : 3;
	uint8 receiveSize = 0;

	switch (message->request & ~DP_AUX_I2C_MOT) {
		case DP_AUX_NATIVE_WRITE:
		case DP_AUX_I2C_WRITE:
		case DP_AUX_I2C_WRITE_STATUS_UPDATE:
			transmitSize += message->size;
			break;
	}

	if (message->size > 0 && message->buffer == NULL)
		return B_BAD_VALUE;

	transmitBuffer[0] = (message->request << 4)
		| ((message->address >> 16) & 0x0f);
	transmitBuffer[1] = (message->address >> 8) & 0xff;
	transmitBuffer[2] = message->address & 0xff;
	transmitBuffer[3] = message->size != 0 ? (message->size - 1) : 0;

	for (uint8 retry = 0; retry < 7; retry++) {
		ssize_t result = B_ERROR;

		switch (message->request & ~DP_AUX_I2C_MOT) {
			case DP_AUX_NATIVE_WRITE:
			case DP_AUX_I2C_WRITE:
			case DP_AUX_I2C_WRITE_STATUS_UPDATE:
				receiveSize = 2;
				if (message->buffer != NULL)
					memcpy(transmitBuffer + 4, message->buffer, message->size);
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize,
					receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					result = result > 1 ? min_c((ssize_t)receiveBuffer[1],
						(ssize_t)message->size) : (ssize_t)message->size;
				}
				break;

			case DP_AUX_NATIVE_READ:
			case DP_AUX_I2C_READ:
				receiveSize = message->size + 1;
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize,
					receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					result--;
					if (message->buffer != NULL && result > 0)
						memcpy(message->buffer, receiveBuffer + 1, result);
				}
				break;

			default:
				return B_BAD_VALUE;
		}

		if (result == B_BUSY)
			continue;
		if (result < B_OK)
			return result;

		switch (message->reply & DP_AUX_NATIVE_REPLY_MASK) {
			case DP_AUX_NATIVE_REPLY_ACK:
				return result;
			case DP_AUX_NATIVE_REPLY_NACK:
				return B_IO_ERROR;
			case DP_AUX_NATIVE_REPLY_DEFER:
				snooze(400);
				break;
			default:
				return B_IO_ERROR;
		}
	}

	return B_IO_ERROR;
}


static ssize_t
aux_transfer(uint8 ddiPort, uint8* transmitBuffer, uint8 transmitSize,
	uint8* receiveBuffer, uint8 receiveSize)
{
	const uint32 channelControl = aux_control_register(ddiPort);
	uint32 status = 0;

	for (int tries = 0; tries < 3; tries++) {
		if (!read_register(channelControl, status))
			return B_ERROR;
		if ((status & INTEL_ARC_DP_AUX_CTL_BUSY) == 0)
			break;
		snooze(1000);
		if (tries == 2)
			return B_BUSY;
	}

	if (transmitSize > 20 || receiveSize > 20)
		return E2BIG;

	const uint32 sendControl = INTEL_ARC_DP_AUX_CTL_BUSY
		| INTEL_ARC_DP_AUX_CTL_DONE
		| INTEL_ARC_DP_AUX_CTL_INTERRUPT
		| INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR
		| INTEL_ARC_DP_AUX_CTL_TIMEOUT_1600US
		| INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR
		| ((uint32)transmitSize << INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT)
		| INTEL_ARC_DP_AUX_CTL_FW_SYNC_PULSE_SKL(32)
		| INTEL_ARC_DP_AUX_CTL_SYNC_PULSE_SKL(32);

	for (uint8 retry = 0; retry < 5; retry++) {
		for (uint8 i = 0; i < transmitSize;) {
			uint32 data = ((uint32)transmitBuffer[i++]) << 24;
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 16;
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 8;
			if (i < transmitSize)
				data |= transmitBuffer[i++];
			write_register(aux_data_register(ddiPort, (i - 1) / 4), data);
		}

		write_register(channelControl, sendControl);

		for (int waited = 0; waited < 1000; waited++) {
			if (!read_register(channelControl, status))
				return B_ERROR;
			if ((status & INTEL_ARC_DP_AUX_CTL_BUSY) == 0)
				break;
			snooze(10);
		}

		write_register(channelControl, status | INTEL_ARC_DP_AUX_CTL_DONE
			| INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR
			| INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR);

		if ((status & INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR) != 0)
			continue;
		if ((status & INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR) != 0) {
			snooze(400);
			continue;
		}
		if ((status & INTEL_ARC_DP_AUX_CTL_DONE) != 0)
			break;
	}

	if ((status & INTEL_ARC_DP_AUX_CTL_DONE) == 0)
		return B_BUSY;
	if ((status & INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR) != 0)
		return B_IO_ERROR;
	if ((status & INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR) != 0)
		return B_TIMEOUT;

	uint8 bytes = (status & INTEL_ARC_DP_AUX_CTL_MSG_SIZE_MASK)
		>> INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT;
	if (bytes == 0 || bytes > 20)
		return B_BUSY;
	if (bytes > receiveSize)
		bytes = receiveSize;

	for (uint8 i = 0; i < bytes;) {
		uint32 data = 0;
		if (!read_register(aux_data_register(ddiPort, i / 4), data))
			return B_ERROR;
		receiveBuffer[i++] = data >> 24;
		if (i < bytes)
			receiveBuffer[i++] = data >> 16;
		if (i < bytes)
			receiveBuffer[i++] = data >> 8;
		if (i < bytes)
			receiveBuffer[i++] = data;
	}

	return bytes;
}
