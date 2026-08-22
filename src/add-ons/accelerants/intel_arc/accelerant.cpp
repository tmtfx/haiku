/*
 * Copyright 2026, Haiku and the Pirati Del Frico contributors.
 * Distributed under the terms of the MIT License.
 *
 * This accelerant is intentionally minimal but functional. Structure and
 * API usage were derived from MIT-compatible Haiku sources and MIT-licensed
 * upstream Intel sources:
 *  - src/add-ons/accelerants/framebuffer/accelerant.cpp
 *  - src/add-ons/accelerants/framebuffer/hooks.cpp
 *  - src/add-ons/accelerants/framebuffer/mode.cpp
 *  - src/add-ons/accelerants/intel_extreme/accelerant.cpp
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy_regs.h
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h
 *
 */
#include <OS.h>

#include "accelerant_protos.h"
#include "accelerant.h"

#include <compute_display_timing.h>
#include <create_display_modes.h>
#include <ddc.h>
#include <dp.h>

#include <edid.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AutoDeleterOS.h>

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)
//#define ACC_TRACE(x...) printf("intel_arc.accelerant: " x)
//#define ACC_ERROR(x...) printf("intel_arc.accelerant ERROR: " x)

accelerant_info* gInfo;
static engine_token sEngineToken = {1, 0, NULL};
static uint64 sSyncCounter = 1;


/*
static bool is_mode_in_list(const display_mode& mode, display_mode* match = NULL);
static uint32 bytes_per_pixel_for_space(color_space space);
static uint32 plane_color_format_for_space(color_space space);

static uint32 decode_link_rate(uint8 rawLinkRate);
static uint8 encode_link_rate(uint32 linkRate);
static uint8 c20_dp_rate(uint32 linkRate);
static uint8 c20_custom_width(uint32 linkRate);
static bool dp_clock_recovery_ok(const uint8* status, uint32 lanes);
static bool dp_channel_eq_ok(const uint8* status, uint32 lanes);
static void dp_get_adjust_request(const uint8* status, uint8 lane,
	uint8* voltage, uint8* emphasis);
static int dp14_level_index(uint8 voltage, uint8 emphasis);
static status_t apply_snps_phy_levels(uint8 ddiPort, const uint8* laneSettings,
	uint32 lanes, bool uhbr);
static uint32 cx0_port_base(uint8 ddiPort, uint8 lane, bool timer);
static status_t cx0_write(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 data,
	bool committed);
static status_t apply_c20_phy_levels(uint8 ddiPort, uint32 linkRate,
	const uint8* laneSettings, uint32 lanes);
static status_t cx0_rmw(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 clear,
	uint8 set, bool committed);
static status_t apply_c10_phy_levels(uint8 ddiPort, uint32 linkRate,
	const uint8* laneSettings, uint32 lanes);
static status_t apply_ddi_source_levels(uint8 ddiPort, int8 pipe, uint32 lanes,
	const uint8* laneSettings, uint32 linkRate);
static status_t perform_dp_link_training(uint32 linkRate, uint32 lanes);
static bool compute_displayport_dpll(int* pDiv, int* qDiv, int* kDiv, float* dco);
static status_t program_port_dpll(uint8 ddiPort);
static status_t configure_dp_link(display_mode* mode);
static status_t apply_hdmi_phy_levels(uint8 ddiPort, int8 pipe);
static status_t read_edid_from_hardware(void);
static status_t read_edid_from_port(uint8 ddiPort, edid1_info& edid);
static status_t read_dpcd_caps_from_port(uint8 ddiPort);
static status_t aux_send_receive(const i2c_bus* bus, uint32 slaveAddress,
	const uint8* writeBuffer, size_t writeLength, uint8* readBuffer,
	size_t readLength);
static ssize_t aux_transfer(uint8 ddiPort, dp_aux_msg* message);
static ssize_t aux_transfer(uint8 ddiPort, uint8* transmitBuffer,
	uint8 transmitSize, uint8* receiveBuffer, uint8 receiveSize);
static status_t
intel_arc_program_hdmi_dpll(accelerant_info* info, uint8 ddiPort, uint32 pixel_clock_khz);
static bool mode_matches_exactly(const display_mode& left, const display_mode& right);
static uint32 refresh_rate_for_mode(const display_mode& mode);
static void sanitize_mode_geometry(display_mode& mode, const char* origin);
static void log_pipe_plane_state(const char* origin, int8 pipe);
static uint32 scaler_control_register(int8 pipe, uint32 scalerIndex);
static uint32 scaler_window_pos_register(int8 pipe, uint32 scalerIndex);
static uint32 scaler_window_size_register(int8 pipe, uint32 scalerIndex);
static status_t get_combo_dpll_registers(uint8 ddiPort, uint32& cfg0, uint32& cfg1,
	uint32& enable, uint32& ssc, uint32& clockOffMask, uint32& clockSelectMask,
	uint32& clockSelectValue, uint32& dpllId);
static uint32 snps_phy_base_for_ddi_port(uint8 ddiPort);
static uint32 snps_phy_enable_reg_for_ddi_port(uint8 ddiPort);
*/



static status_t
init_common(int device, bool isClone)
{
	debug_printf("intel_arc.accelerant: init_common(device=%d, isClone=%d)\n", device, isClone);
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
	gInfo->overlay_mem_mgr = NULL;
	gInfo->has_edid = false;
	gInfo->last_hotplug_event_count = 0;

	intel_arc_get_private_data data;
	data.magic = INTEL_ARC_PRIVATE_DATA_MAGIC;
	if (ioctl(device, INTEL_ARC_GET_PRIVATE_DATA, &data, sizeof(data)) != 0) {
		debug_printf("intel_arc.accelerant ERROR: IOCTL INTEL_ARC_GET_PRIVATE_DATA failed\n");
		return B_ERROR;
	}

	AreaDeleter sharedDeleter(clone_area("intel arc shared info",
		(void**)&gInfo->shared_info, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.shared_info_area));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK) {
		debug_printf("intel_arc.accelerant ERROR: Failed to clone shared info area: %s\n", strerror(status));
		return status;
	}

	if (gInfo->shared_info->registers_area >= B_OK) {
		AreaDeleter regsDeleter(clone_area("intel arc regs", (void**)&gInfo->registers,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			gInfo->shared_info->registers_area));
		status = gInfo->regs_area = regsDeleter.Get();
		if (status < B_OK) {
			debug_printf("intel_arc.accelerant ERROR: Failed to clone registers area: %s\n", strerror(status));
			return status;
		}
		regsDeleter.Detach();
	}

	if (gInfo->shared_info != NULL)
		gInfo->last_hotplug_event_count = gInfo->shared_info->hotplug_event_count;

	infoDeleter.Detach();
	sharedDeleter.Detach();
	return B_OK;
}

static void
uninit_common(void)
{
	debug_printf("intel_arc.accelerant: uninit_common()\n");
	if (gInfo->overlay_mem_mgr != NULL) {
		mem_destroy(gInfo->overlay_mem_mgr);
		gInfo->overlay_mem_mgr = NULL;
	}
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
	debug_printf("intel_arc.accelerant: intel_arc_init_accelerant() start\n");
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
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info, sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
		debug_printf("intel_arc.accelerant: Cloned Framebuffer area: %" B_PRId32 " at %p\n", info.area, info.address);
		status_t overlayStatus = init_overlay_memory_manager();
		if (overlayStatus != B_OK)
			debug_printf("intel_arc.accelerant: overlay VRAM heap unavailable: %s\n",
				strerror(overlayStatus));
	} else {
		debug_printf("intel_arc.accelerant ERROR: Failed to clone framebuffer: %s\n", strerror(status));
	}
	
	debug_printf("intel_arc.accelerant: Summary: Pipe=%d, DDI Port=%u, Detected Ports Mask=0x%02x\n",
		gInfo->shared_info->active_pipe,
		gInfo->shared_info->active_ddi_port,
		gInfo->shared_info->detected_port_bits);

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
	debug_printf("intel_arc.accelerant: intel_arc_clone_accelerant()\n");
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
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info, sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
		status_t overlayStatus = init_overlay_memory_manager();
		if (overlayStatus != B_OK)
			debug_printf("intel_arc.accelerant: overlay VRAM heap unavailable in clone: %s\n",
				strerror(overlayStatus));
	}

	return B_OK;
}

void
intel_arc_uninit_accelerant(void)
{
	debug_printf("intel_arc.accelerant: intel_arc_uninit_accelerant()\n");
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
	return gInfo->shared_info != NULL ? gInfo->shared_info->vblank_sem : -1;
}

uint32
intel_arc_accelerant_engine_count(void)
{
	return 1;
}

status_t
intel_arc_acquire_engine(uint32 capabilities, uint32 maxWait,
	sync_token* syncToken, engine_token** engineToken)
{
	(void)capabilities;
	(void)maxWait;

	if (syncToken != NULL)
		intel_arc_sync_to_token(syncToken);

	*engineToken = &sEngineToken;
	return B_OK;
}

status_t
intel_arc_release_engine(engine_token* engineToken, sync_token* syncToken)
{
	if (syncToken != NULL)
		return intel_arc_get_sync_token(engineToken, syncToken);

	return B_OK;
}

void
intel_arc_wait_engine_idle(void)
{
}

status_t
intel_arc_get_sync_token(engine_token* engineToken, sync_token* syncToken)
{
	if (engineToken == NULL || syncToken == NULL)
		return B_BAD_VALUE;

	syncToken->engine_id = engineToken->engine_id;
	syncToken->counter = sSyncCounter++;
	memset(syncToken->opaque, 0, sizeof(syncToken->opaque));
	return B_OK;
}

status_t
intel_arc_sync_to_token(sync_token* syncToken)
{
	(void)syncToken;
	intel_arc_wait_engine_idle();
	return B_OK;
}



uint32
intel_arc_accelerant_mode_count(void)
{
	(void)handle_hotplug_event();
	return gInfo->shared_info->mode_count;
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
		case B_ACCELERANT_ENGINE_COUNT:
			return (void*)intel_arc_accelerant_engine_count;
		case B_ACQUIRE_ENGINE:
			return (void*)intel_arc_acquire_engine;
		case B_RELEASE_ENGINE:
			return (void*)intel_arc_release_engine;
		case B_WAIT_ENGINE_IDLE:
			return (void*)intel_arc_wait_engine_idle;
		case B_GET_SYNC_TOKEN:
			return (void*)intel_arc_get_sync_token;
		case B_SYNC_TO_TOKEN:
			return (void*)intel_arc_sync_to_token;
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
		case B_OVERLAY_COUNT:
			return (void*)intel_arc_overlay_count;
		case B_OVERLAY_SUPPORTED_SPACES:
			return (void*)intel_arc_overlay_supported_spaces;
		case B_OVERLAY_SUPPORTED_FEATURES:
			return (void*)intel_arc_overlay_supported_features;
		case B_ALLOCATE_OVERLAY_BUFFER:
			return (void*)intel_arc_allocate_overlay_buffer;
		case B_RELEASE_OVERLAY_BUFFER:
			return (void*)intel_arc_release_overlay_buffer;
		case B_GET_OVERLAY_CONSTRAINTS:
			return (void*)intel_arc_get_overlay_constraints;
		case B_ALLOCATE_OVERLAY:
			return (void*)intel_arc_allocate_overlay;
		case B_RELEASE_OVERLAY:
			return (void*)intel_arc_release_overlay;
		case B_CONFIGURE_OVERLAY:
			return (void*)intel_arc_configure_overlay;
		case B_SET_INDEXED_COLORS:
			return (void*)intel_arc_set_indexed_colors;
	}

	return NULL;
}

