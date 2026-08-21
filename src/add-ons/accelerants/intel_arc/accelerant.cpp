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

#define CALLED() debug_printf("SM750_ACC: CALLED %s\n", __FUNCTION__)
//#define ACC_TRACE(x...) printf("intel_arc.accelerant: " x)
//#define ACC_ERROR(x...) printf("intel_arc.accelerant ERROR: " x)

accelerant_info* gInfo;
static engine_token sEngineToken = {1, 0, NULL};
static uint64 sSyncCounter = 1;



static const arc_mit_buf_trans_entry kDg2SnpsDp14Trans[] = {
	{25, 0, 0}, {32, 0, 6}, {35, 0, 10}, {43, 0, 17}, {35, 0, 0},
	{45, 0, 8}, {48, 0, 14}, {47, 0, 0}, {55, 0, 7}, {62, 0, 0}
};

static const arc_mit_buf_trans_entry kDg2SnpsUhbrTrans[] = {
	{62, 0, 0}, {55, 0, 7}, {50, 0, 12}, {44, 0, 18}, {35, 0, 21},
	{59, 3, 0}, {53, 3, 6}, {48, 3, 11}, {42, 5, 15}, {37, 5, 20},
	{56, 6, 0}, {48, 7, 7}, {45, 7, 10}, {39, 8, 15}, {48, 14, 0},
	{45, 4, 4}
};

static const arc_mit_buf_trans_entry kMtlC10Dp14Trans[] = {
	{26, 0, 0}, {33, 0, 6}, {38, 0, 11}, {43, 0, 19}, {39, 0, 0},
	{45, 0, 7}, {46, 0, 13}, {46, 0, 0}, {55, 0, 7}, {62, 0, 0}
};

static const arc_mit_buf_trans_entry kMtlC20Dp14Trans[] = {
	{20, 0, 0}, {24, 0, 4}, {30, 0, 9}, {34, 0, 14}, {29, 0, 0},
	{34, 0, 5}, {38, 0, 10}, {36, 0, 0}, {40, 0, 6}, {48, 0, 0}
};

static const arc_mit_buf_trans_entry kMtlC20UhbrTrans[] = {
	{48, 0, 0}, {43, 0, 5}, {40, 0, 8}, {37, 0, 11}, {33, 0, 15},
	{46, 2, 0}, {42, 2, 4}, {38, 2, 8}, {35, 2, 11}, {33, 2, 13},
	{44, 4, 0}, {40, 4, 4}, {37, 4, 7}, {33, 4, 11}, {40, 8, 0},
	{30, 2, 2}
};

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
static status_t write_dpcd(uint32 address, const void* buffer, size_t size);
static status_t read_dpcd(uint32 address, void* buffer, size_t size);
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
static status_t program_ddi_buffer(uint8 ddiPort, int8 pipe, uint32 lanes, bool enable);
static status_t configure_dp_link(display_mode* mode);
static status_t handle_hotplug_event(void);
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
static status_t
intel_arc_program_hdmi_dpll(accelerant_info* info, int dpll_id, uint32 pixel_clock_khz);


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

/* in questa versione ereditiamo la modalità dal boot e la salviamo assieme alle modalità
 * ottenute dall'edid. (questo comporta che la modalità ereditata ed errata con h_display a 
 * 1920 x 1080 ma risoluzione virtuale a 1280x1024 sovrascrive la modalità dell'edid nativa 
 * di quella risoluzione (1280x1024). Se provo a cambiare risoluzione scegliendo unda di 
 * quelle elencate il monitor va in out of range. La gestione colori RGB32 e RGB16 funziona
 * CMAP8 va ridefinita visto che i colori passano sul verdino
 */
static status_t
create_mode_list(void)
{
	display_mode mode = gInfo->shared_info->current_mode;
	debug_printf("==================================================\n");
	debug_printf("intel_arc.accelerant: >>> CREATE MODE LIST <<<\n");
	debug_printf("intel_arc.accelerant: >>> PREVIOUS MODE: <<<\n");
	debug_printf("  - Virtual Size : %u x %u\n", mode.virtual_width, mode.virtual_height);
	debug_printf("  - Display Start: (%u, %u)\n", mode.h_display_start, mode.v_display_start);
	debug_printf("  - Color Space  : 0x%08X\n", mode.space);
	debug_printf("  - Flags        : 0x%08X\n", mode.flags);
	debug_printf("  --- Timing Details ---\n");
	debug_printf("  - Pixel Clock  : %u kHz\n", mode.timing.pixel_clock);
	debug_printf("  - Horizontal   : Display=%u, SyncStart=%u, SyncEnd=%u, Total=%u\n",
		mode.timing.h_display, mode.timing.h_sync_start,
		mode.timing.h_sync_end, mode.timing.h_total);
	debug_printf("  - Vertical     : Display=%u, SyncStart=%u, SyncEnd=%u, Total=%u\n",
		mode.timing.v_display, mode.timing.v_sync_start,
		mode.timing.v_sync_end, mode.timing.v_total);
	debug_printf("  - Sync Flags   : 0x%08X\n", mode.timing.flags);
	debug_printf("==================================================\n");
	

	
    const color_space kSupportedSpaces[] = {
        B_RGB32, B_RGB16, B_CMAP8
    };
    const uint32 kNumSupportedSpaces = sizeof(kSupportedSpaces) / sizeof(kSupportedSpaces[0]);

    // 1. Sanificazione della modalità iniziale ricavata dai registri/boot
 	if (mode.virtual_width == 0 || mode.virtual_height == 0) {
		if (gInfo->shared_info->has_boot_info
			&& gInfo->shared_info->boot_width > 0
			&& gInfo->shared_info->boot_height > 0) {
			debug_printf("intel_arc.accelerant: Using boot framebuffer mode for mode list:\n");
			mode.virtual_width = gInfo->shared_info->boot_width;
			mode.virtual_height = gInfo->shared_info->boot_height;
			mode.space = gInfo->shared_info->boot_depth >= 24 ? B_RGB32 : B_RGB16;
			debug_printf("intel_arc.accelerant: mode.virtual_width %u, mode.virtual_height %u\n", mode.virtual_width,mode.virtual_height);
		} else {
			debug_printf("intel_arc.accelerant: Boot mode unavailable, falling back to 1024x768\n");
			memset(&mode, 0, sizeof(mode));
			mode.virtual_width = 1024;
			mode.virtual_height = 768;
			mode.space = B_RGB32;
		}
	}
	mode.h_display_start = 0;
	mode.v_display_start = 0;
	mode.flags = 0;
/*
    // Garantisci timings VESA coerenti per la modalità iniziale
    // temporaneamente? forse compute_display_timing sovrascrive questi due valori:
    mode.timing.h_display = mode.virtual_width;
    mode.timing.v_display = mode.virtual_height;
    compute_display_timing(mode.virtual_width, mode.virtual_height, 60, false, &mode.timing);
*/
    const int8 pipe = gInfo->shared_info->active_pipe;
	if (pipe >= 0) {
		const uint32 hTotal = gInfo->shared_info->pipe_h_total[pipe];
		const uint32 hSync = gInfo->shared_info->pipe_h_sync[pipe];
		const uint32 vTotal = gInfo->shared_info->pipe_v_total[pipe];
		const uint32 vSync = gInfo->shared_info->pipe_v_sync[pipe];

		if (hTotal != 0 && vTotal != 0) {
			mode.timing.h_display = (hTotal & 0xffff) + 1;
			mode.timing.h_total = (hTotal >> 16) + 1;
			mode.timing.h_sync_start = (hSync & 0xffff) + 1;
			mode.timing.h_sync_end = (hSync >> 16) + 1;
			mode.timing.v_display = (vTotal & 0xffff) + 1;
			mode.timing.v_total = (vTotal >> 16) + 1;
			mode.timing.v_sync_start = (vSync & 0xffff) + 1;
			mode.timing.v_sync_end = (vSync >> 16) + 1;
		}
	}
	
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0) {
		compute_display_timing(mode.virtual_width, mode.virtual_height, 60, false,
			&mode.timing);
	}
	
    if (mode.timing.pixel_clock == 0) {
        mode.timing.pixel_clock = ((uint32)mode.timing.h_total
            * (uint32)mode.timing.v_total * 60) / 1000;
    }

    // Preserva lo stride rilevato dall'hardware nel kernel
    const uint32 bytesPerPixel = bytes_per_pixel_for_space((color_space)mode.space);
	if (gInfo->shared_info->bytes_per_row == 0){
		gInfo->shared_info->bytes_per_row  = (mode.virtual_width * bytesPerPixel + 63) & ~63;
	}
	
	debug_printf("intel_arc.accelerant: Bytes Per Row: %u",gInfo->shared_info->bytes_per_row);
	gInfo->shared_info->current_mode = mode;
	debug_printf("intel_arc.accelerant: >>> CREATE MODE LIST <<<\n");
	debug_printf("intel_arc.accelerant: >>> NEW MODE: <<<\n");
	debug_printf("  - Virtual Size : %u x %u\n", mode.virtual_width, mode.virtual_height);
	debug_printf("  - Display Start: (%u, %u)\n", mode.h_display_start, mode.v_display_start);
	debug_printf("  - Color Space  : 0x%08X\n", mode.space);
	debug_printf("  - Flags        : 0x%08X\n", mode.flags);
	debug_printf("  --- Timing Details ---\n");
	debug_printf("  - Pixel Clock  : %u kHz\n", mode.timing.pixel_clock);
	debug_printf("  - Horizontal   : Display=%u, SyncStart=%u, SyncEnd=%u, Total=%u\n",
		mode.timing.h_display, mode.timing.h_sync_start,
		mode.timing.h_sync_end, mode.timing.h_total);
	debug_printf("  - Vertical     : Display=%u, SyncStart=%u, SyncEnd=%u, Total=%u\n",
		mode.timing.v_display, mode.timing.v_sync_start,
		mode.timing.v_sync_end, mode.timing.v_total);
	debug_printf("  - Sync Flags   : 0x%08X\n", mode.timing.flags);
	debug_printf("==================================================\n");

    gInfo->shared_info->current_mode = mode;

    // 2. Controllo presenza EDID (sia Hardware che Bootloader Fallback)
    const bool hasValidEdid = gInfo->has_edid || gInfo->shared_info->has_boot_edid;
    edid1_info* targetEdid = gInfo->has_edid ? &gInfo->edid_info : &gInfo->shared_info->boot_edid;

    if (hasValidEdid) {
        debug_printf("intel_arc.accelerant: EDID available (%s), parsing full mode list\n",
            gInfo->has_edid ? "Hardware" : "Bootloader");

        // Genera tutte le risoluzioni dichiarate dal monitor tramite l'EDID
        gInfo->mode_list_area = create_display_modes("intel arc modes",
            targetEdid, &mode, 1, kSupportedSpaces, kNumSupportedSpaces, //NULL, 0
            NULL , &gInfo->mode_list, &gInfo->shared_info->mode_count);//is_mode_supported
    } else {
        debug_printf("intel_arc.accelerant: No EDID found, generating single active mode fallback\n");

        gInfo->mode_list_area = create_display_modes("intel arc modes",
            NULL, &mode, 1, kSupportedSpaces, kNumSupportedSpaces,
            is_mode_supported, &gInfo->mode_list, &gInfo->shared_info->mode_count);
    }

    if (gInfo->mode_list_area < B_OK) {
        debug_printf("intel_arc.accelerant ERROR: Failed to create display modes area: %s\n",
            strerror(gInfo->mode_list_area));
        return gInfo->mode_list_area;
    }

    gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
    debug_printf("intel_arc.accelerant: Created %u display modes successfully\n",
        gInfo->shared_info->mode_count);

    return B_OK;
}

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
intel_arc_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_OFF;
}

uint32
intel_arc_dpms_mode(void)
{
	(void)handle_hotplug_event();
	return gInfo->shared_info->dpms_mode;
}

status_t
intel_arc_set_dpms_mode(uint32 mode)
{
	debug_printf("intel_arc.accelerant: intel_arc_set_dpms_mode(mode=%u)\n", mode);
	(void)handle_hotplug_event();
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
	(void)handle_hotplug_event();
	return gInfo->shared_info->mode_count;
}

status_t
intel_arc_get_mode_list(display_mode* modeList)
{
	(void)handle_hotplug_event();
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
	CALLED();
	(void)handle_hotplug_event();
	(void)low;
	(void)high;

	display_mode match;
	if (!is_mode_in_list(*target, &match)) {
		debug_printf("intel_arc.accelerant ERROR: Proposed mode %ux%u not found in list\n",
			target->virtual_width, target->virtual_height);
		return B_BAD_VALUE;
	}

	*target = match;
	return B_OK;
}
/*
status_t
intel_arc_get_preferred_mode(display_mode* mode)
{
	(void)handle_hotplug_event();
	if (gInfo->shared_info->mode_count == 0)
		return B_ENTRY_NOT_FOUND;

	*mode = gInfo->mode_list[0];
	return B_OK;
}*/

status_t
intel_arc_get_preferred_mode(display_mode* mode)
{
    (void)handle_hotplug_event();

    if (gInfo->shared_info->mode_count == 0 || gInfo->mode_list == NULL)
        return B_ENTRY_NOT_FOUND;

    if (mode == NULL)
        return B_BAD_VALUE;

    // 1. Se abbiamo le informazioni di boot, cerchiamo la modalità corrispondente
    if (gInfo->shared_info->has_boot_info 
        && gInfo->shared_info->boot_width > 0 
        && gInfo->shared_info->boot_height > 0) {

        const uint32 targetWidth = gInfo->shared_info->boot_width;
        const uint32 targetHeight = gInfo->shared_info->boot_height;
        const color_space targetSpace = (gInfo->shared_info->boot_depth >= 24) 
            ? B_RGB32 : B_RGB16;

        // Cerca prima un match perfetto (Risoluzione + Color Space)
        for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
            if (gInfo->mode_list[i].virtual_width == targetWidth
                && gInfo->mode_list[i].virtual_height == targetHeight
                && gInfo->mode_list[i].space == targetSpace) {
                
                *mode = gInfo->mode_list[i];
                debug_printf("intel_arc.accelerant: Preferred mode matched boot resolution: %ux%u (index %u)\n",
                    targetWidth, targetHeight, i);
                return B_OK;
            }
        }

        // Cerca un match secondario (Solo Risoluzione, se lo spazio colore varia)
        for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
            if (gInfo->mode_list[i].virtual_width == targetWidth
                && gInfo->mode_list[i].virtual_height == targetHeight) {
                
                *mode = gInfo->mode_list[i];
                debug_printf("intel_arc.accelerant: Preferred mode matched boot size: %ux%u\n",
                    targetWidth, targetHeight);
                return B_OK;
            }
        }
    }

    // 2. Fallback: Se la risoluzione di boot non è presente nell'EDID o non è definita,
    // usiamo la prima modalità valida dell'elenco
    *mode = gInfo->mode_list[0];
    debug_printf("intel_arc.accelerant: Preferred mode fallback to mode_list[0]: %ux%u\n",
        mode->virtual_width, mode->virtual_height);

    return B_OK;
}

status_t
intel_arc_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	debug_printf("intel_arc.accelerant: >>> SET_DISPLAY_MODE requested: %ux%u, pixel_clock=%u kHz <<<\n",
		mode->virtual_width, mode->virtual_height, mode->timing.pixel_clock);

	debug_printf("intel_arc.accelerant: Timing: HTotal=%u, HDisplay=%u, HSyncStart=%u, HSyncEnd=%u\n",
		mode->timing.h_total, mode->timing.h_display, mode->timing.h_sync_start, mode->timing.h_sync_end);
	debug_printf("intel_arc.accelerant: Timing: VTotal=%u, VDisplay=%u, VSyncStart=%u, VSyncEnd=%u\n",
		mode->timing.v_total, mode->timing.v_display, mode->timing.v_sync_start, mode->timing.v_sync_end);

	(void)handle_hotplug_event();

	display_mode target = *mode;
	status_t status = intel_arc_propose_display_mode(&target, &target, &target);
	if (status != B_OK) {
		debug_printf("propose display mode failed\n");
		return status;
	}

	if (gInfo->shared_info->active_pipe < 0) {
		debug_printf("intel_arc.accelerant ERROR: No active pipe found in shared info!\n");
		return B_UNSUPPORTED;
	}

	status = apply_dpms_off();
	if (status != B_OK)
		debug_printf("intel_arc.accelerant ERROR: apply_dpms_off() returned %s\n", strerror(status));

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 bytesPerPixel = bytes_per_pixel_for_space((color_space)target.space);
	if (bytesPerPixel == 0)
		return B_BAD_VALUE;
	debug_printf("Arc Driver: app_server pitch = %" B_PRIu32 ", calculated pitch = %" B_PRIu32 "\n",
    	gInfo->shared_info->bytes_per_row, (target.virtual_width * bytesPerPixel + 63) & ~63);

	gInfo->shared_info->current_mode = target;
	//gInfo->shared_info->bytes_per_row = target.virtual_width * bytesPerPixel;
	
	// Regilations Pipe Timings
	gInfo->shared_info->pipe_h_total[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16) | ((uint32)target.timing.h_display - 1);
	debug_printf("intel_arc.accelerant: H_TOTAL impostato a 0x%X\n", gInfo->shared_info->pipe_h_total[pipe]);
	gInfo->shared_info->pipe_h_blank[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16) | ((uint32)target.timing.h_display - 1);
	debug_printf("intel_arc.accelerant: H_BLANK impostato a 0x%X\n", gInfo->shared_info->pipe_h_blank[pipe]);
	gInfo->shared_info->pipe_h_sync[pipe]
		= ((uint32)(target.timing.h_sync_end - 1) << 16) | ((uint32)target.timing.h_sync_start - 1);
	debug_printf("intel_arc.accelerant: H_SYNC impostato a 0x%X\n", gInfo->shared_info->pipe_h_sync[pipe]);
	gInfo->shared_info->pipe_v_total[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16) | ((uint32)target.timing.v_display - 1);
	debug_printf("intel_arc.accelerant: V_TOTAL impostato a 0x%X\n", gInfo->shared_info->pipe_v_total[pipe]);
	gInfo->shared_info->pipe_v_blank[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16) | ((uint32)target.timing.v_display - 1);
	debug_printf("intel_arc.accelerant: V_BLANK impostato a 0x%X\n", gInfo->shared_info->pipe_v_blank[pipe]);
	gInfo->shared_info->pipe_v_sync[pipe]
		= ((uint32)(target.timing.v_sync_end - 1) << 16) | ((uint32)target.timing.v_sync_start - 1);
	debug_printf("intel_arc.accelerant: V_SYNC impostato a 0x%X\n", gInfo->shared_info->pipe_v_sync[pipe]);
	//gInfo->shared_info->pipe_size[pipe]
	//	= ((uint32)(target.timing.v_display - 1) << 16) | ((uint32)target.timing.h_display - 1);
	
	// modifica applicata:
	//gInfo->shared_info->pipe_size[pipe]
    //    = ((uint32)(target.timing.h_display - 1) << 16) | ((uint32)target.timing.v_display - 1);
    //-------------------
    //modifiche da stride
	//gInfo->shared_info->plane_stride[pipe] = gInfo->shared_info->bytes_per_row;
	//debug_printf("intel_arc.accelerant: scritto in gInfo->shared_info->plane_stride[pipe] %u\n",gInfo->shared_info->bytes_per_row);
	// modifica applicata:
	const uint32 bytesPerRow = (target.virtual_width * bytesPerPixel + 63) & ~63;//(mode->timing.h_display * bytesPerPixel + 63) & ~63;
	gInfo->shared_info->bytes_per_row = bytesPerRow;
	gInfo->shared_info->plane_stride[pipe] = bytesPerRow / 64;
	gInfo->shared_info->fbc.bytes_per_row = bytesPerRow;
	
    //-------------------
	gInfo->shared_info->plane_pos[pipe] = 0;
	//modifiche da stride
	//gInfo->shared_info->plane_image_size[pipe]
	//	= ((uint32)(target.timing.v_display - 1) << 16)
	//		| ((uint32)target.timing.h_display - 1);
	// modifica applicata:
	//gInfo->shared_info->plane_image_size[pipe]
    //    = ((uint32)(target.timing.h_display - 1) << 16) | ((uint32)target.timing.v_display - 1);
	//gInfo->shared_info->plane_image_size[pipe]
	//    = ((uint32)(target.virtual_height - 1) << 16) | ((uint32)(target.virtual_width - 1));
	debug_printf("intel_arc.accelerant: PLANE_CONTROL prima di modificare i parametri: 0x%X\n",gInfo->shared_info->plane_control[pipe]);
    gInfo->shared_info->plane_control[pipe] &= ~INTEL_ARC_PLANE_TILED_MASK;
	gInfo->shared_info->plane_control[pipe] |= INTEL_ARC_PLANE_LINEAR;
		debug_printf("intel_arc.accelerant: PLANE_CONTROL dopo modifica parametri TILED_MASK e LINEAR: 0x%X\n",gInfo->shared_info->plane_control[pipe]);
    //-------------------
	gInfo->shared_info->plane_control[pipe]
		= (gInfo->shared_info->plane_control[pipe]
			& ~INTEL_ARC_DISPLAY_CONTROL_COLOR_MASK_SKY)
		| plane_color_format_for_space((color_space)target.space);
	// A. Disabilita lo Scaler
	write_register(INTEL_ARC_MMIO_PS_CTRL_A + (pipe * 0x1000), 0); //disattiva scaler hardware
	//const uint32 hDisplay = target.timing.h_display; // 1280
	//const uint32 vDisplay = target.timing.v_display; // 1024
	//debug_printf("intel_arc.accelerant: imposto pipe_size e plane_image_size con virtual_width e virtual_height invece che timing.h_display e v_display\n");
	const uint32 hDisplay = target.virtual_width; // 1280
	const uint32 vDisplay = target.virtual_height; // 1024
	//const uint32 thDisplay = target.timing.h_display; // 1280
	//const uint32 tvDisplay = target.timing.v_display; // 1024
	

	const uint32 nativeSize = ((vDisplay - 1) << 16) | (hDisplay - 1);
	const uint32 nativeSizeforPipe = ((hDisplay - 1) << 16) | (vDisplay - 1);
	//const uint32 nativeSize = ((tvDisplay - 1) << 16) | (thDisplay - 1);

	// La Pipe e il Piano devono avere LA STESSA dimensione fisica
	gInfo->shared_info->pipe_size[pipe] = nativeSizeforPipe;
	debug_printf("intel_arc.accelerant: PIPE_SIZE impostato a 0x%X\n", gInfo->shared_info->pipe_size[pipe]);
	gInfo->shared_info->plane_image_size[pipe] = nativeSize;
	debug_printf("intel_arc.accelerant: PLANE_IMAGE_SIZE impostato a 0x%X\n", gInfo->shared_info->plane_image_size[pipe]);
	
	//gInfo->shared_info->plane_control[pipe] |= INTEL_ARC_PLANE_ENABLE; // Attiva PLANE_CTL_ENABLE
/*
	write_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL + pipeOffset, gInfo->shared_info->pipe_h_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HBLANK + pipeOffset, gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HSYNC + pipeOffset, gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL + pipeOffset, gInfo->shared_info->pipe_v_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VBLANK + pipeOffset, gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VSYNC + pipeOffset, gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_SIZE + pipeOffset, gInfo->shared_info->pipe_size[pipe]);
	//modifiche da stride
	write_register(INTEL_ARC_MMIO_PLANE_A_STRIDE + pipeOffset, gInfo->shared_info->plane_stride[pipe]);
    write_register(INTEL_ARC_MMIO_PLANE_A_POS + pipeOffset, gInfo->shared_info->plane_pos[pipe]);
    write_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + pipeOffset, gInfo->shared_info->plane_image_size[pipe]);
    write_register(INTEL_ARC_MMIO_PLANE_A_CONTROL + pipeOffset, gInfo->shared_info->plane_control[pipe]);
	*/
	// B. Scrivi i Timing della Pipe (1280x1024 VESA)
	write_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL + pipeOffset, gInfo->shared_info->pipe_h_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HBLANK + pipeOffset, gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HSYNC + pipeOffset, gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL + pipeOffset, gInfo->shared_info->pipe_v_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VBLANK + pipeOffset, gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VSYNC + pipeOffset, gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_SIZE + pipeOffset, gInfo->shared_info->pipe_size[pipe]);
	// C. Scrivi la superficie del Piano (1280x1024)
	write_register(INTEL_ARC_MMIO_PLANE_A_POS + pipeOffset, 0);
	write_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + pipeOffset, gInfo->shared_info->plane_image_size[pipe]);
	write_register(INTEL_ARC_MMIO_PLANE_A_STRIDE + pipeOffset, gInfo->shared_info->plane_stride[pipe]);
	write_register(INTEL_ARC_MMIO_PLANE_A_OFFSET + pipeOffset, 0);
	write_register(INTEL_ARC_MMIO_PLANE_A_CONTROL + pipeOffset, gInfo->shared_info->plane_control[pipe] | (1U << 31));
    
    uint32 fbAddress = gInfo->shared_info->frame_buffer_base + gInfo->shared_info->frame_buffer_offset;
    write_register(INTEL_ARC_MMIO_PLANE_A_SURFACE + pipeOffset, fbAddress);
    
    uint32 ddiFuncCtl = gInfo->shared_info->pipe_ddi_func_ctl[pipe];

    if ((target.timing.flags & B_POSITIVE_HSYNC) != 0)
        ddiFuncCtl |= INTEL_ARC_DDI_HSYNC_POLARITY_POSITIVE;
    else
        ddiFuncCtl &= ~INTEL_ARC_DDI_HSYNC_POLARITY_POSITIVE;

    if ((target.timing.flags & B_POSITIVE_VSYNC) != 0)
        ddiFuncCtl |= INTEL_ARC_DDI_VSYNC_POLARITY_POSITIVE;
    else
        ddiFuncCtl &= ~INTEL_ARC_DDI_VSYNC_POLARITY_POSITIVE;

    // Salva lo stato aggiornato nella struttura condivisa e scrivi il registro MMIO
    gInfo->shared_info->pipe_ddi_func_ctl[pipe] = ddiFuncCtl;
    write_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + pipeOffset, ddiFuncCtl);
    //-------------------

	debug_printf("intel_arc.accelerant: Configuring link for Active Pipe %d (FuncCtl: 0x%08X)\n",
		pipe, gInfo->shared_info->pipe_ddi_func_ctl[pipe]);

	uint32 modeSel = (gInfo->shared_info->pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
	debug_printf("intel_arc.accelerant: Detected DDI ModeSel: %u\n", modeSel);

	if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
		debug_printf("intel_arc.accelerant: Mode is DP, calling configure_dp_link()\n");
		status = configure_dp_link(&target);
		if (status != B_OK) {
			debug_printf("intel_arc.accelerant ERROR: configure_dp_link failed: %s\n", strerror(status));
			return status;
		}
	} else {
		debug_printf("intel_arc.accelerant: Mode is NOT DisplayPort (likely HDMI/DVI), skipping DP link training\n");
		status = intel_arc_program_hdmi_dpll(gInfo, pipe, target.timing.pixel_clock);
    	if (status != B_OK) {
        	debug_printf("intel_arc.accelerant ERROR: intel_arc_program_hdmi_dpll failed: %s\n", strerror(status));
        	return status;
    	}
	}

	status = apply_dpms_on();
	if (status == B_OK) {
		*mode = target;
		gInfo->shared_info->fbc.frame_buffer = (void*)gInfo->shared_info->frame_buffer;
    	//gInfo->shared_info->fbc.bytes_per_row = gInfo->shared_info->bytes_per_row; // fatto prima
    	gInfo->shared_info->fbc.frame_buffer_dma = (void *)(gInfo->shared_info->frame_buffer_base 
    + gInfo->shared_info->frame_buffer_offset);
		debug_printf("intel_arc.accelerant: SET_DISPLAY_MODE completed successfully!\n");
	} else {
		debug_printf("intel_arc.accelerant ERROR: apply_dpms_on() failed: %s\n", strerror(status));
	}
	return status;
}
/* old
status_t
intel_arc_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	(void)handle_hotplug_event();
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
	if (status == B_OK){
		*mode = target;
		gInfo->shared_info->fbc.frame_buffer = (void*)gInfo->shared_info->frame_buffer;
		gInfo->shared_info->fbc.bytes_per_row = gInfo->shared_info->bytes_per_row;
		gInfo->shared_info->fbc.frame_buffer_dma = (void *)(gInfo->shared_info->frame_buffer_base 
			+ gInfo->shared_info->frame_buffer_offset);
	}
	return status;
}*/

status_t
intel_arc_get_display_mode(display_mode* mode)
{
	(void)handle_hotplug_event();
	*mode = gInfo->shared_info->current_mode;
	return B_OK;
}

status_t
intel_arc_get_edid_info(void* info, size_t size, uint32* version)
{
	(void)handle_hotplug_event();
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
	(void)handle_hotplug_event();
	if (!config) return B_BAD_VALUE;
	if (gInfo->frame_buffer == NULL)
		return B_UNSUPPORTED;
	*config = gInfo->shared_info->fbc;
	//config->frame_buffer = gInfo->frame_buffer;
	//config->frame_buffer_dma = NULL;
	//config->bytes_per_row = gInfo->shared_info->bytes_per_row;
	return B_OK;
}

status_t
intel_arc_get_pixel_clock_limits(display_mode* mode, uint32* low, uint32* high)
{
	(void)handle_hotplug_event();
	uint32 totalPixel = (uint32)mode->timing.h_total * (uint32)mode->timing.v_total;
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
	debug_printf("intel_arc.accelerant ERROR: wait_for_clear timed out on reg 0x%08X (mask 0x%08X)\n", offset, mask);
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
	debug_printf("intel_arc.accelerant ERROR: wait_for_set timed out on reg 0x%08X (mask 0x%08X)\n", offset, mask);
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
	debug_printf("intel_arc.accelerant: configure_dp_link() entering for mode %ux%u@%uHz\n",
		mode->virtual_width, mode->virtual_height, mode->timing.pixel_clock);

	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 pipeFunc = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
	uint32 bitsPerPixel = 24;
	switch ((pipeFunc & INTEL_ARC_PIPE_DDI_BPC_MASK) >> 20) {
		case 0: bitsPerPixel = 24; break;
		case 1: bitsPerPixel = 30; break;
		case 2: bitsPerPixel = 18; break;
		case 3: bitsPerPixel = 36; break;
		default: break;
	}

	uint32 maxLanes = ((pipeFunc & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK) >> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1;
	if (gInfo->shared_info->has_dpcd && gInfo->shared_info->dpcd_max_lane_count != 0)
		maxLanes = min_c(maxLanes, (uint32)gInfo->shared_info->dpcd_max_lane_count);
	if (maxLanes == 0 || maxLanes > 4)
		maxLanes = 4;

	uint32 maxLinkRate = 270000;
	if (gInfo->shared_info->has_dpcd && gInfo->shared_info->dpcd_max_link_rate != 0)
		maxLinkRate = decode_link_rate(gInfo->shared_info->dpcd_max_link_rate);

	debug_printf("intel_arc.accelerant: DP Config: maxLanes=%u, maxLinkRate=%u kHz, bpp=%u\n", maxLanes, maxLinkRate, bitsPerPixel);

	const uint32 candidateRates[] = {810000, 540000, 270000, 162000};
	uint32 linkBandwidth = 0;
	uint32 lanes = 0;
	const uint32 bps = mode->timing.pixel_clock * bitsPerPixel * 21 / 20;

	for (size_t i = 0; i < sizeof(candidateRates) / sizeof(candidateRates[0]); i++) {
		if (candidateRates[i] > maxLinkRate)
			continue;
		for (uint32 candidateLanes = 1; candidateLanes <= 4; candidateLanes <<= 1) {
			if (candidateLanes > maxLanes)
				continue;
			if (bps <= candidateRates[i] * candidateLanes * 8) {
				linkBandwidth = candidateRates[i];
				lanes = candidateLanes;
				break;
			}
		}
		if (lanes != 0)
			break;
	}
	if (lanes == 0) {
		debug_printf("intel_arc.accelerant ERROR: Could not compute valid DP lanes and link bandwidth for bps=%u\n", bps);
		return B_BAD_VALUE;
	}

	debug_printf("intel_arc.accelerant: Selected DP link Bandwidth: %u kHz, Lanes: %u\n", linkBandwidth, lanes);

	uint64 linkSpeed = (uint64)lanes * linkBandwidth * 8;
	uint64 retN = 1;
	while (retN < linkSpeed)
		retN <<= 1;
	if (retN > 0x800000)
		retN = 0x800000;
	uint64 retM = (uint64)mode->timing.pixel_clock * retN * bitsPerPixel / linkSpeed;
	while (retN > 0xffffff || retM > 0xffffff) {
		retN >>= 1;
		retM >>= 1;
	}
	
	debug_printf("intel_arc.accelerant: Writing Data M/N: M=0x%" B_PRIx64 ", N=0x%" B_PRIx64 "\n", retM, retN);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_M + pipeOffset, (uint32)retM | INTEL_ARC_DDI_MN_TU_SIZE_MASK);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_N + pipeOffset, (uint32)retN);

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
	
	debug_printf("intel_arc.accelerant: Writing Link M/N: M=0x%" B_PRIx64 ", N=0x%" B_PRIx64 "\n", retM, retN);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_M + pipeOffset, (uint32)retM);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_N + pipeOffset, (uint32)retN);

	if (gInfo->shared_info->has_dpcd) {
		debug_printf("intel_arc.accelerant: Has DPCD -> Programming Port DPLL and Link Training\n");
		status_t status = program_port_dpll(gInfo->shared_info->active_ddi_port);
		if (status != B_OK) {
			debug_printf("intel_arc.accelerant ERROR: program_port_dpll failed: %s\n", strerror(status));
			return status;
		}

		status = program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, lanes, true);
		if (status != B_OK) {
			debug_printf("intel_arc.accelerant ERROR: program_ddi_buffer failed: %s\n", strerror(status));
			return status;
		}

		status = perform_dp_link_training(linkBandwidth, lanes);
		if (status != B_OK) {
			debug_printf("intel_arc.accelerant ERROR: perform_dp_link_training failed: %s\n", strerror(status));
			return status;
		}

		gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			= (gInfo->shared_info->pipe_ddi_func_ctl[pipe] & ~INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			| ((lanes - 1) << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT);
	}

	return B_OK;
}

static status_t
handle_hotplug_event(void)
{
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return B_NO_INIT;

	if (gInfo->last_hotplug_event_count == gInfo->shared_info->hotplug_event_count)
		return B_OK;

	debug_printf("intel_arc.accelerant: Hotplug event detected (count=%u)\n", gInfo->shared_info->hotplug_event_count);
	gInfo->last_hotplug_event_count = gInfo->shared_info->hotplug_event_count;

	if (gInfo->shared_info->active_pipe < 0)
		return B_OK;

	(void)read_edid_from_hardware();

	const uint8 activePort = gInfo->shared_info->active_ddi_port;
	if (activePort == 0)
		return B_OK;

	if (activePort <= 4 && (gInfo->shared_info->detected_port_bits & (1 << activePort)) == 0) {
		gInfo->has_edid = false;
		return B_OK;
	}

	if (gInfo->shared_info->current_mode.virtual_width == 0
		|| gInfo->shared_info->current_mode.virtual_height == 0) {
		return B_OK;
	}

	return configure_dp_link(&gInfo->shared_info->current_mode);
}

static status_t
write_dpcd(uint32 address, const void* buffer, size_t size)
{
	if (!gInfo->shared_info->has_dpcd || gInfo->shared_info->active_ddi_port == 0)
		return B_UNSUPPORTED;

	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	message.address = address;
	message.request = DP_AUX_NATIVE_WRITE;
	message.buffer = const_cast<void*>(buffer);
	message.size = size;
	ssize_t result = aux_transfer(gInfo->shared_info->active_ddi_port, &message);
	return result < B_OK ? (status_t)result : B_OK;
}

static status_t
read_dpcd(uint32 address, void* buffer, size_t size)
{
	if (!gInfo->shared_info->has_dpcd || gInfo->shared_info->active_ddi_port == 0)
		return B_UNSUPPORTED;

	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	message.address = address;
	message.request = DP_AUX_NATIVE_READ;
	message.buffer = buffer;
	message.size = size;
	ssize_t result = aux_transfer(gInfo->shared_info->active_ddi_port, &message);
	return result < B_OK ? (status_t)result : B_OK;
}

static bool
dp_clock_recovery_ok(const uint8* status, uint32 lanes)
{
	for (uint32 lane = 0; lane < lanes; lane++) {
		const uint8 value = status[lane / 2];
		const bool ok = (lane & 1) == 0
			? (value & DP_LANE_STATUS_CR_DONE_A) != 0
			: (value & DP_LANE_STATUS_CR_DONE_B) != 0;
		if (!ok)
			return false;
	}
	return true;
}

static bool
dp_channel_eq_ok(const uint8* status, uint32 lanes)
{
	for (uint32 lane = 0; lane < lanes; lane++) {
		const uint8 value = status[lane / 2];
		const bool ok = (lane & 1) == 0
			? (value & DP_LANE_STATUS_EQUALIZED_A) == DP_LANE_STATUS_EQUALIZED_A
			: (value & DP_LANE_STATUS_EQUALIZED_B) == DP_LANE_STATUS_EQUALIZED_B;
		if (!ok)
			return false;
	}
	return (status[2] & DP_INTERLANE_ALIGN_DONE) != 0;
}

static void
dp_get_adjust_request(const uint8* status, uint8 lane, uint8* voltage,
	uint8* emphasis)
{
	const uint8 request = status[4 + lane / 2];
	if ((lane & 1) == 0) {
		*voltage = (request & DP_ADJ_VCC_SWING_LANEA_MASK) >> DP_ADJ_VCC_SWING_LANEA_SHIFT;
		*emphasis = (request & DP_ADJ_PRE_EMPHASIS_LANEA_MASK) >> DP_ADJ_PRE_EMPHASIS_LANEA_SHIFT;
	} else {
		*voltage = (request & DP_ADJ_VCC_SRING_LANEB_MASK) >> DP_ADJ_VCC_SWING_LANEB_SHIFT;
		*emphasis = (request & DP_ADJ_PRE_EMPHASIS_LANEB_MASK) >> DP_ADJ_PRE_EMPHASIS_LANEB_SHIFT;
	}
}

static uint32
decode_link_rate(uint8 rawLinkRate)
{
	switch (rawLinkRate) {
		case DP_LINK_RATE_162: return 162000;
		case DP_LINK_RATE_270: return 270000;
		case DP_LINK_RATE_540: return 540000;
		case INTEL_ARC_DP_LINK_RATE_810: return 810000;
		case INTEL_ARC_DP_LINK_RATE_1350: return 1350000;
		case INTEL_ARC_DP_LINK_RATE_2000: return 2000000;
		default: return 162000;
	}
}

static uint8
encode_link_rate(uint32 linkRate)
{
	switch (linkRate) {
		case 162000: return DP_LINK_RATE_162;
		case 270000: return DP_LINK_RATE_270;
		case 540000: return DP_LINK_RATE_540;
		case 810000: return INTEL_ARC_DP_LINK_RATE_810;
		case 1350000: return INTEL_ARC_DP_LINK_RATE_1350;
		case 2000000: return INTEL_ARC_DP_LINK_RATE_2000;
		default: return DP_LINK_RATE_162;
	}
}

static uint8
c20_dp_rate(uint32 linkRate)
{
	switch (linkRate) {
		case 162000: return 0;
		case 270000: return 1;
		case 540000: return 2;
		case 810000: return 3;
		case 1000000: return 8;
		case 1350000: return 9;
		case 2000000: return 10;
		default: return 0;
	}
}

static uint8
c20_custom_width(uint32 linkRate)
{
	if (linkRate == 1000000 || linkRate == 1350000 || linkRate == 2000000)
		return 2;
	return 0;
}

static int
dp14_level_index(uint8 voltage, uint8 emphasis)
{
	static const int8 kLevelMap[4][4] = {
		{0, 1, 2, 3},
		{4, 5, 6, -1},
		{7, 8, -1, -1},
		{9, -1, -1, -1},
	};
	if (voltage > 3 || emphasis > 3)
		return -1;
	return kLevelMap[voltage][emphasis];
}

static status_t
apply_snps_phy_levels(uint8 ddiPort, const uint8* laneSettings, uint32 lanes,
	bool uhbr)
{
	debug_printf("intel_arc.accelerant: apply_snps_phy_levels(ddiPort=%u, lanes=%u, uhbr=%d)\n", ddiPort, lanes, uhbr);
	const arc_mit_buf_trans_entry* table = uhbr ? kDg2SnpsUhbrTrans : kDg2SnpsDp14Trans;
	const size_t tableCount = uhbr ? B_COUNT_OF(kDg2SnpsUhbrTrans) : B_COUNT_OF(kDg2SnpsDp14Trans);

	const uint32 phyBase = ddiPort <= 2
		? INTEL_ARC_MMIO_SNPS_PHY_A_BASE : INTEL_ARC_MMIO_SNPS_PHY_B_BASE;

	for (uint32 lane = 0; lane < lanes && lane < 4; lane++) {
		uint8 voltage = laneSettings[lane] & 0x3;
		uint8 emphasis = (laneSettings[lane] >> 3) & 0x3;
		int level = dp14_level_index(voltage, emphasis);
		if (level < 0 || (size_t)level >= tableCount)
			return B_BAD_VALUE;

		uint32 value = ((uint32)table[level].main << 18)
			| ((uint32)table[level].post << 10)
			| ((uint32)table[level].pre << 2);
		write_register(INTEL_ARC_MMIO_SNPS_PHY_TX_EQ(phyBase, lane), value);
	}

	return B_OK;
}

static uint32
cx0_port_base(uint8 ddiPort, uint8 lane, bool timer)
{
	static const uint32 kCtlBase[] = {
		INTEL_ARC_CX0_M2P_CTL_A_LN0,
		INTEL_ARC_CX0_M2P_CTL_B_LN0,
		INTEL_ARC_CX0_M2P_CTL_USBC1_LN0,
		INTEL_ARC_CX0_M2P_CTL_USBC2_LN0
	};
	static const uint32 kTimerBase[] = {
		INTEL_ARC_CX0_TIMER_A_LN0,
		INTEL_ARC_CX0_TIMER_B_LN0,
		INTEL_ARC_CX0_TIMER_USBC1_LN0,
		INTEL_ARC_CX0_TIMER_USBC2_LN0
	};

	if (ddiPort < 1 || ddiPort > 4)
		return 0;
	return (timer ? kTimerBase[ddiPort - 1] : kCtlBase[ddiPort - 1]) + lane * 4;
}

static status_t
cx0_write(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 data, bool committed)
{
	for (uint8 lane = 0; lane < 2; lane++) {
		if ((laneMask & (1 << lane)) == 0)
			continue;

		const uint32 ctlReg = cx0_port_base(ddiPort, lane, false);
		const uint32 statusReg = ctlReg + INTEL_ARC_CX0_P2M_STATUS_OFFSET;
		const uint32 timerReg = cx0_port_base(ddiPort, lane, true);
		if (ctlReg == 0 || timerReg == 0)
			return B_BAD_VALUE;

		write_register(timerReg, INTEL_ARC_CX0_TIMER_VALUE);
		if (wait_for_clear(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING,
				INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
			write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_RESET);
			continue;
		}

		write_register(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY | INTEL_ARC_CX0_P2M_ERROR_SET);

		write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING
			| (committed ? INTEL_ARC_CX0_M2P_COMMAND_WRITE_COMMITTED : INTEL_ARC_CX0_M2P_COMMAND_WRITE_UNCOMMITTED)
			| INTEL_ARC_CX0_M2P_DATA(data)
			| INTEL_ARC_CX0_M2P_ADDRESS(addr));

		if (wait_for_clear(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING,
				INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
			write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_RESET);
			return B_TIMED_OUT;
		}

		if (committed) {
			uint32 status = 0;
			if (wait_for_set(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY, INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK)
				return B_TIMED_OUT;
			if (!read_register(statusReg, status))
				return B_ERROR;
			if ((status & INTEL_ARC_CX0_P2M_ERROR_SET) != 0
				|| ((status & INTEL_ARC_CX0_P2M_COMMAND_TYPE_MASK) >> 27) != INTEL_ARC_CX0_P2M_COMMAND_WRITE_ACK) {
				return B_ERROR;
			}
		}
	}

	return B_OK;
}

static status_t
perform_dp_link_training(uint32 linkRate, uint32 lanes)
{
	debug_printf("intel_arc.accelerant: perform_dp_link_training(linkRate=%u, lanes=%u) start\n", linkRate, lanes);
	uint8 laneSettings[4] = {0, 0, 0, 0};
	uint8 status[DP_LINK_STATUS_SIZE];

	uint8 value = encode_link_rate(linkRate);
	status_t result = write_dpcd(DP_LINK_RATE, &value, 1);
	if (result != B_OK) {
		debug_printf("intel_arc.accelerant ERROR: write_dpcd DP_LINK_RATE failed: %s\n", strerror(result));
		return result;
	}

	value = lanes & DP_LANE_COUNT_MASK;
	result = write_dpcd(DP_LANE_COUNT, &value, 1);
	if (result != B_OK)
		return result;

	value = DP_TRAINING_PATTERN_1;
	result = write_dpcd(DP_TRAINING_PATTERN_SET, &value, 1);
	if (result != B_OK)
		return result;

	result = write_dpcd(DP_TRAINING_LANE0_SET, laneSettings, lanes);
	if (result != B_OK)
		return result;
	result = apply_ddi_source_levels(gInfo->shared_info->active_ddi_port,
		gInfo->shared_info->active_pipe, lanes, laneSettings, linkRate);
	if (result != B_OK)
		return result;

	debug_printf("intel_arc.accelerant: DP Link Training: Phase 1 (Clock Recovery)\n");
	for (int attempt = 0; attempt < 5; attempt++) {
		snooze(400);
		result = read_dpcd(DP_LANE_STATUS_0_1, status, DP_LINK_STATUS_SIZE);
		if (result != B_OK)
			return result;
		if (dp_clock_recovery_ok(status, lanes)) {
			debug_printf("intel_arc.accelerant: Clock Recovery SUCCESS on attempt %d\n", attempt + 1);
			break;
		}

		for (uint32 lane = 0; lane < lanes; lane++) {
			uint8 voltage, emphasis;
			dp_get_adjust_request(status, lane, &voltage, &emphasis);
			laneSettings[lane] = (voltage & 0x3) | ((emphasis & 0x3) << 3);
			if (voltage == 3) laneSettings[lane] |= DP_TRAIN_MAX_SWING_EN;
			if (emphasis == 3) laneSettings[lane] |= DP_TRAIN_MAX_EMPHASIS_EN;
		}

		result = write_dpcd(DP_TRAINING_LANE0_SET, laneSettings, lanes);
		if (result != B_OK) return result;
		result = apply_ddi_source_levels(gInfo->shared_info->active_ddi_port,
			gInfo->shared_info->active_pipe, lanes, laneSettings, linkRate);
		if (result != B_OK) return result;

		if (attempt == 4) {
			debug_printf("intel_arc.accelerant ERROR: Clock Recovery FAILED after 5 attempts!\n");
			return B_ERROR;
		}
	}

	debug_printf("intel_arc.accelerant: DP Link Training: Phase 2 (Channel Equalization)\n");
	value = DP_TRAINING_PATTERN_2;
	result = write_dpcd(DP_TRAINING_PATTERN_SET, &value, 1);
	if (result != B_OK)
		return result;

	for (int attempt = 0; attempt < 5; attempt++) {
		snooze(400);
		result = read_dpcd(DP_LANE_STATUS_0_1, status, DP_LINK_STATUS_SIZE);
		if (result != B_OK)
			return result;
		if (dp_channel_eq_ok(status, lanes)) {
			debug_printf("intel_arc.accelerant: Channel Equalization SUCCESS on attempt %d\n", attempt + 1);
			break;
		}

		for (uint32 lane = 0; lane < lanes; lane++) {
			uint8 voltage, emphasis;
			dp_get_adjust_request(status, lane, &voltage, &emphasis);
			laneSettings[lane] = (voltage & 0x3) | ((emphasis & 0x3) << 3);
			if (voltage == 3) laneSettings[lane] |= DP_TRAIN_MAX_SWING_EN;
			if (emphasis == 3) laneSettings[lane] |= DP_TRAIN_MAX_EMPHASIS_EN;
		}

		result = write_dpcd(DP_TRAINING_LANE0_SET, laneSettings, lanes);
		if (result != B_OK) return result;
		result = apply_ddi_source_levels(gInfo->shared_info->active_ddi_port,
			gInfo->shared_info->active_pipe, lanes, laneSettings, linkRate);
		if (result != B_OK) return result;

		if (attempt == 4) {
			debug_printf("intel_arc.accelerant ERROR: Channel Equalization FAILED after 5 attempts!\n");
			return B_ERROR;
		}
	}

	value = DP_TRAINING_PATTERN_DISABLE;
	result = write_dpcd(DP_TRAINING_PATTERN_SET, &value, 1);
	debug_printf("intel_arc.accelerant: DP Link Training COMPLETED SUCCESSFULLY!\n");
	return result;
}

static bool
compute_displayport_dpll(int* pDiv, int* qDiv, int* kDiv, float* dco)
{
	*pDiv = 3;
	*qDiv = 1;
	*kDiv = 2;
	*dco = 8090.0f;
	debug_printf("intel_arc.accelerant: compute_displayport_dpll(): P=%d, Q=%d, K=%d, DCO=%.1f\n", *pDiv, *qDiv, *kDiv, *dco);
	return true;
}

static status_t
cx0_rmw(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 clear, uint8 set,
	bool committed)
{
	for (uint8 lane = 0; lane < 2; lane++) {
		if ((laneMask & (1 << lane)) == 0)
			continue;

		const uint32 ctlReg = cx0_port_base(ddiPort, lane, false);
		const uint32 statusReg = ctlReg + INTEL_ARC_CX0_P2M_STATUS_OFFSET;
		uint8 oldValue = 0;
		if (ctlReg == 0)
			return B_BAD_VALUE;
		if (wait_for_clear(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING, INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK)
			return B_TIMED_OUT;

		write_register(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY | INTEL_ARC_CX0_P2M_ERROR_SET);
		write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING
			| INTEL_ARC_CX0_M2P_COMMAND_READ
			| INTEL_ARC_CX0_M2P_ADDRESS(addr));
		if (wait_for_set(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY, INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK)
			return B_TIMED_OUT;

		uint32 status = 0;
		if (!read_register(statusReg, status))
			return B_ERROR;
		if ((status & INTEL_ARC_CX0_P2M_ERROR_SET) != 0
			|| ((status & INTEL_ARC_CX0_P2M_COMMAND_TYPE_MASK) >> 27) != INTEL_ARC_CX0_P2M_COMMAND_READ_ACK) {
			return B_ERROR;
		}
		oldValue = (status & INTEL_ARC_CX0_P2M_DATA_MASK) >> 16;

		status_t result = cx0_write(ddiPort, 1 << lane, addr, (oldValue & ~clear) | set, committed);
		if (result != B_OK)
			return result;
	}
	return B_OK;
}

static status_t
apply_c10_phy_levels(uint8 ddiPort, uint32 linkRate, const uint8* laneSettings,
	uint32 lanes)
{
	const uint8 laneMask = lanes > 2 ? 0x3 : 0x1;

	status_t status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CONTROL(1),
		0, INTEL_ARC_C10_VDR_CTRL_MSGBUS_ACCESS, true);
	if (status != B_OK) return status;

	const uint8 txVboost = (linkRate == 540000 || linkRate == 810000) ? 5 : 4;
	const uint8 termCtl = (linkRate == 540000 || linkRate == 810000) ? 5 : 2;
	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CMN(3),
		INTEL_ARC_C10_CMN3_TXVBOOST_MASK, INTEL_ARC_C10_CMN3_TXVBOOST(txVboost), false);
	if (status != B_OK) return status;

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_TX(1),
		INTEL_ARC_C10_TX1_TERMCTL_MASK, INTEL_ARC_C10_TX1_TERMCTL(termCtl), true);
	if (status != B_OK) return status;

	for (uint32 ln = 0; ln < lanes; ln++) {
		const uint8 voltage = laneSettings[ln] & 0x3;
		const uint8 emphasis = (laneSettings[ln] >> 3) & 0x3;
		const int level = dp14_level_index(voltage, emphasis);
		if (level < 0 || level >= (int)B_COUNT_OF(kMtlC10Dp14Trans))
			return B_BAD_VALUE;

		const int lane = ln / 2;
		const int tx = ln % 2;
		const uint8 singleLaneMask = lane == 0 ? 0x1 : 0x2;
		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 0),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].pre), true);
		if (status != B_OK) return status;

		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 1),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].main), true);
		if (status != B_OK) return status;

		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 2),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].post), true);
		if (status != B_OK) return status;
	}

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_OVRD, 0,
		INTEL_ARC_C10_VDR_OVRD_TX1 | INTEL_ARC_C10_VDR_OVRD_TX2, true);
	if (status != B_OK) return status;

	return cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CONTROL(1), 0,
		INTEL_ARC_C10_VDR_CTRL_UPDATE_CFG, true);
}

static status_t
apply_c20_phy_levels(uint8 ddiPort, uint32 linkRate, const uint8* laneSettings,
	uint32 lanes)
{
	const uint8 laneMask = lanes > 2 ? 0x3 : 0x1;
	const arc_mit_buf_trans_entry* table = linkRate > 810000
		? kMtlC20UhbrTrans : kMtlC20Dp14Trans;
	const size_t tableCount = linkRate > 810000 ? B_COUNT_OF(kMtlC20UhbrTrans)
		: B_COUNT_OF(kMtlC20Dp14Trans);

	status_t status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_PHY_C20_VDR_CUSTOM_WIDTH, 0x3,
		INTEL_ARC_PHY_C20_CUSTOM_WIDTH(c20_custom_width(linkRate)), true);
	if (status != B_OK) return status;

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_PHY_C20_VDR_CUSTOM_SERDES_RATE,
		INTEL_ARC_PHY_C20_IS_DP | (0xf << 1),
		INTEL_ARC_PHY_C20_IS_DP | INTEL_ARC_PHY_C20_DP_RATE(c20_dp_rate(linkRate)), true);
	if (status != B_OK) return status;

	for (uint32 ln = 0; ln < lanes; ln++) {
		const uint8 voltage = laneSettings[ln] & 0x3;
		const uint8 emphasis = (laneSettings[ln] >> 3) & 0x3;
		const int level = dp14_level_index(voltage, emphasis);
		if (level < 0 || (size_t)level >= tableCount)
			return B_BAD_VALUE;

		const int lane = ln / 2;
		const int tx = ln % 2;
		const uint8 singleLaneMask = lane == 0 ? 0x1 : 0x2;
		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 0),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].pre), true);
		if (status != B_OK) return status;

		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 1),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].main), true);
		if (status != B_OK) return status;

		status = cx0_rmw(ddiPort, singleLaneMask, INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 2),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK, INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].post), true);
		if (status != B_OK) return status;
	}

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_OVRD, 0,
		INTEL_ARC_C10_VDR_OVRD_TX1 | INTEL_ARC_C10_VDR_OVRD_TX2, true);
	if (status != B_OK) return status;

	return cx0_rmw(ddiPort, laneMask, INTEL_ARC_PHY_C20_VDR_CUSTOM_SERDES_RATE,
		INTEL_ARC_PHY_C20_CONTEXT_TOGGLE, INTEL_ARC_PHY_C20_CONTEXT_TOGGLE, true);
}

static status_t
apply_ddi_source_levels(uint8 ddiPort, int8 pipe, uint32 lanes,
	const uint8* laneSettings, uint32 linkRate)
{
	(void)pipe;
	debug_printf("intel_arc.accelerant: apply_ddi_source_levels(ddiPort=%u, family=%u)\n",
		ddiPort, gInfo->shared_info->family);

	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_ALCHEMIST)
		return apply_snps_phy_levels(ddiPort, laneSettings, lanes, linkRate > 810000);
	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_BATTLEMAGE)
		return linkRate > 810000
			? apply_c20_phy_levels(ddiPort, linkRate, laneSettings, lanes)
			: apply_c10_phy_levels(ddiPort, linkRate, laneSettings, lanes);

	return B_UNSUPPORTED;
}

static status_t
program_port_dpll(uint8 ddiPort)
{
	debug_printf("intel_arc.accelerant: program_port_dpll(ddiPort=%u) entering\n", ddiPort);
	if (ddiPort < 1 || ddiPort > 3) {
		debug_printf("intel_arc.accelerant: Port %u does not use standard TGL DPLL registers\n", ddiPort);
		return B_OK;
	}

	int pDiv, qDiv, kDiv;
	float dco;
	if (!compute_displayport_dpll(&pDiv, &qDiv, &kDiv, &dco))
		return B_ERROR;

	uint32 cfg0 = 0, cfg1 = 0, enable = 0, ssc = 0, dpllIndex = 0;
	switch (ddiPort) {
		case 1:
			dpllIndex = 0;
			cfg0 = INTEL_ARC_TGL_DPLL0_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL0_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL0_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL0_SPREAD_SPECTRUM;
			break;
		case 2:
			dpllIndex = 1;
			cfg0 = INTEL_ARC_TGL_DPLL1_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL1_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL1_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL1_SPREAD_SPECTRUM;
			break;
		case 3:
			dpllIndex = 4;
			cfg0 = INTEL_ARC_TGL_DPLL4_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL4_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL4_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL4_SPREAD_SPECTRUM;
			break;
	}

	uint32 refKHz = 19200;
	uint32 dcoInt = (uint32)floorf(dco / (refKHz / 1000.0f));
	uint32 dcoFrac = (uint32)ceilf((dco / (refKHz / 1000.0f) - dcoInt) * (1 << 15));
	uint32 dcoReg = dcoInt | (dcoFrac << INTEL_ARC_TGL_DPLL_DCO_FRACTION_SHIFT);

	uint32 dividers = 0;
	switch (pDiv) {
		case 2: dividers |= INTEL_ARC_TGL_DPLL_PDIV_2; break;
		case 3: dividers |= INTEL_ARC_TGL_DPLL_PDIV_3; break;
		case 5: dividers |= INTEL_ARC_TGL_DPLL_PDIV_5; break;
		case 7: dividers |= INTEL_ARC_TGL_DPLL_PDIV_7; break;
		default: return B_BAD_VALUE;
	}
	switch (kDiv) {
		case 1: dividers |= INTEL_ARC_TGL_DPLL_KDIV_1; break;
		case 2: dividers |= INTEL_ARC_TGL_DPLL_KDIV_2; break;
		case 3: dividers |= INTEL_ARC_TGL_DPLL_KDIV_3; break;
		default: return B_BAD_VALUE;
	}
	if (qDiv != 1)
		dividers |= ((uint32)qDiv << INTEL_ARC_TGL_DPLL_QDIV_RATIO_SHIFT) | INTEL_ARC_TGL_DPLL_QDIV_ENABLE;

	debug_printf("intel_arc.accelerant: Programming DPLL %u: dcoReg=0x%08X, dividers=0x%08X\n", dpllIndex, dcoReg, dividers);

	uint32 value = 0;
	read_register(enable, value);
	write_register(enable, value & ~INTEL_ARC_TGL_DPLL_ENABLE);
	(void)wait_for_clear(enable, INTEL_ARC_TGL_DPLL_LOCK, 50000);

	read_register(enable, value);
	write_register(enable, value | INTEL_ARC_TGL_DPLL_POWER_ENABLE);
	(void)wait_for_set(enable, INTEL_ARC_TGL_DPLL_POWER_STATE, 50000);

	read_register(ssc, value);
	write_register(ssc, value & ~INTEL_ARC_TGL_DPLL_SSC_ENABLE);

	write_register(cfg0, dcoReg);
	write_register(cfg1, dividers);

	read_register(enable, value);
	write_register(enable, value | INTEL_ARC_TGL_DPLL_ENABLE);
	status_t status = wait_for_set(enable, INTEL_ARC_TGL_DPLL_LOCK, 50000);
	if (status != B_OK) {
		debug_printf("intel_arc.accelerant ERROR: DPLL %u failed to lock!\n", dpllIndex);
		return status;
	}

	debug_printf("intel_arc.accelerant: DPLL %u locked successfully!\n", dpllIndex);

	read_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
	switch (ddiPort) {
		case 1:
			value |= INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_SELECT;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF;
			break;
		case 2:
			value |= INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT;
			value |= 1 << INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT_SHIFT;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF;
			break;
		case 3:
			value |= INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_SELECT;
			value |= 2 << 4;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF;
			break;
	}
	write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
	return B_OK;
}

static status_t
program_ddi_buffer(uint8 ddiPort, int8 pipe, uint32 lanes, bool enable)
{
	debug_printf("intel_arc.accelerant: program_ddi_buffer(ddiPort=%u, pipe=%d, lanes=%u, enable=%d)\n",
		ddiPort, pipe, lanes, enable);
	if (ddiPort > 6)
		return B_BAD_VALUE;

	const uint32 reg = INTEL_ARC_MMIO_DDI_BUF_CTL_A + ddiPort * 0x100;
	uint32 value = 0;
	read_register(reg, value);
	value &= ~INTEL_ARC_DDI_PORT_WIDTH(4);
	value &= ~INTEL_ARC_DDI_BUF_TRANS_SELECT(0x7);
	value |= INTEL_ARC_DDI_BUF_TRANS_SELECT(pipe);
	value |= INTEL_ARC_DDI_PORT_WIDTH(lanes);
	if (enable)
		value |= INTEL_ARC_DDI_BUF_CTL_ENABLE;
	else
		value &= ~INTEL_ARC_DDI_BUF_CTL_ENABLE;

	write_register(reg, value);
	if (ddiPort > 0 && ddiPort <= 4)
		gInfo->shared_info->port_state[ddiPort - 1] = value;

	if (enable)
		return wait_for_clear(reg, INTEL_ARC_DDI_BUF_IS_IDLE, 50000);

	return B_OK;
}

static status_t
set_sink_power(uint8 ddiPort, uint8 value)
{
	if (!gInfo->shared_info->has_dpcd || ddiPort == 0)
		return B_OK;

	debug_printf("intel_arc.accelerant: set_sink_power(ddiPort=%u, value=0x%02x)\n", ddiPort, value);
	return write_dpcd(DP_SET_POWER, &value, 1);
}
/* original
static status_t
apply_dpms_off(void)
{
	debug_printf("intel_arc.accelerant: apply_dpms_off() entering\n");
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
	(void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, 4, false);

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
	debug_printf("intel_arc.accelerant: apply_dpms_on() entering\n");
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
	(void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe,
		((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			>> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1, true);

	write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
	write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
	write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
	write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeBaseReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeControlReg,
		gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
	(void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

	gInfo->shared_info->dpms_mode = B_DPMS_ON;
	debug_printf("intel_arc.accelerant: apply_dpms_on() finished successfully\n");
	return B_OK;
}*/
static status_t
apply_dpms_off(void)
{
    debug_printf("intel_arc.accelerant: apply_dpms_off() entering\n");
    if (gInfo->shared_info->active_pipe < 0)
        return B_UNSUPPORTED;

    const int8 pipe = gInfo->shared_info->active_pipe;
    const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
    const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);
    const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
    const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);

    // 1. Disabilita il Piano e forza il latch con la scrittura di SURFACE
    write_register(planeControlReg,
        gInfo->shared_info->plane_control[pipe] & ~INTEL_ARC_DISPLAY_CONTROL_ENABLED);
    write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);
    (void)wait_for_clear(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

    // 2. Disabilita la funzione DDI del Transcoder e il Buffer PHY
    write_register(pipeDdiReg,
        gInfo->shared_info->pipe_ddi_func_ctl[pipe] & ~INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
    (void)wait_for_clear(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);
    (void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, 4, false);

    // 3. Disabilita la Pipe
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
    debug_printf("intel_arc.accelerant: apply_dpms_on() entering\n");
    if (gInfo->shared_info->active_pipe < 0)
        return B_UNSUPPORTED;

    const int8 pipe = gInfo->shared_info->active_pipe;

    (void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

    // 1. Timing della Pipe
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
    const uint32 planeStrideReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_STRIDE, pipe);
    const uint32 planePosReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_POS, pipe);
    const uint32 planeImageReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE, pipe);
    const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);

    // 2. Abilita Pipe
    write_register(pipeControlReg,
        gInfo->shared_info->pipe_control[pipe] | INTEL_ARC_PIPE_ENABLED);
    (void)wait_for_set(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

    // 3. Abilita Transcoder DDI
    const uint32 ddiFuncCtl = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
    write_register(pipeDdiReg, ddiFuncCtl | INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
    (void)wait_for_set(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);

    // 4. Determina le Lane PHY: 4 fisse per HDMI/TMDS, dinamiche per DisplayPort
    uint8 lanes = 4; // HDMI richiede sempre 4 lane (3 dati + 1 clock)
    const uint32 modeSel = (ddiFuncCtl & INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
    if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
        lanes = ((ddiFuncCtl & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
            >> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1;
    }

    (void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, lanes, true);

    // 5. Configura e Abilita il Piano (SURFACE va scritto PER ULTIMO come trigger di Latch)
    write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
    write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
    write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
    write_register(planeControlReg,
        gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
    
    // La scrittura del registro SURFACE esegue l'arm/latch hardware dei registri del piano
    write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);

    (void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

    gInfo->shared_info->dpms_mode = B_DPMS_ON;
    debug_printf("intel_arc.accelerant: apply_dpms_on() finished successfully\n");
    return B_OK;
}

static uint32
aux_control_register(uint8 ddiPort)
{
	return INTEL_ARC_MMIO_AUX_CH_CTL_A + ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE;
}

static uint32
aux_data_register(uint8 ddiPort, uint8 index)
{
	return INTEL_ARC_MMIO_AUX_CH_DATA1_A + ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE + index * 4;
}

static status_t
read_edid_from_hardware(void)
{
	debug_printf("intel_arc.accelerant: read_edid_from_hardware() entering\n");
	if (gInfo->registers == NULL) {
		if (gInfo->shared_info->has_boot_edid) {
			debug_printf("intel_arc.accelerant: Using boot EDID\n");
			memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid, sizeof(edid1_info));
			gInfo->has_edid = true;
			return B_OK;
		}

		gInfo->has_edid = false;
		return B_ENTRY_NOT_FOUND;
	}

	size_t candidateCount = 0;
	uint8 candidates[4];
	
	if (gInfo->shared_info->active_pipe >= 0 && gInfo->shared_info->active_ddi_port < 4)
		candidates[candidateCount++] = gInfo->shared_info->active_ddi_port;

	for (uint8 port = 0; port < 4; port++) {
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

	for (size_t i = 0; i < candidateCount; i++) {
		debug_printf("intel_arc.accelerant: Attempting EDID read on candidate DDI Port %u\n", candidates[i]);
		if (read_edid_from_port(candidates[i], gInfo->edid_info) == B_OK) {
			debug_printf("intel_arc.accelerant: Successfully read EDID from DDI Port %u\n", candidates[i]);
			read_dpcd_caps_from_port(candidates[i]);
			gInfo->has_edid = true;
			return B_OK;
		}
	}

	if (gInfo->shared_info->has_boot_edid) {
		debug_printf("intel_arc.accelerant: Hardware EDID read failed, falling back to boot EDID\n");
		memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid, sizeof(edid1_info));
		gInfo->has_edid = true;
		return B_OK;
	}

	debug_printf("intel_arc.accelerant ERROR: Could not read EDID from any port\n");
	gInfo->has_edid = false;
	return B_ENTRY_NOT_FOUND;
}

static status_t
read_edid_from_port(uint8 ddiPort, edid1_info& edid)
{
	if (ddiPort > 6)
		return B_BAD_VALUE;
		
	debug_printf("intel_arc.accelerant: read_edid_from_port(ddiPort=%u)\n", ddiPort);

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
	debug_printf("intel_arc.accelerant: read_dpcd_caps_from_port(ddiPort=%u)\n", ddiPort);
	uint8 buffer[8];
	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	memset(buffer, 0, sizeof(buffer));

	message.address = DP_DPCD_REV;
	message.request = DP_AUX_NATIVE_READ;
	message.buffer = buffer;
	message.size = sizeof(buffer);

	ssize_t result = aux_transfer(ddiPort, &message);
	if (result < (ssize_t)sizeof(buffer)) {
		debug_printf("intel_arc.accelerant ERROR: aux_transfer DPCD read failed: %s\n", strerror(result < B_OK ? (status_t)result : B_ERROR));
		return result < B_OK ? (status_t)result : B_ERROR;
	}

	gInfo->shared_info->has_dpcd = true;
	memcpy(gInfo->shared_info->dpcd, buffer, sizeof(buffer));
	gInfo->shared_info->dpcd_revision = buffer[0];
	gInfo->shared_info->dpcd_max_link_rate = buffer[1];
	gInfo->shared_info->dpcd_max_lane_count = buffer[2] & DP_MAX_LANE_COUNT_MASK;
	gInfo->shared_info->dpcd_sink_count = buffer[7] & DP_SINK_COUNT_MASK;

	debug_printf("intel_arc.accelerant: DPCD Caps: Rev=0x%02x, MaxLinkRate=0x%02x, MaxLanes=%u\n",
		gInfo->shared_info->dpcd_revision,
		gInfo->shared_info->dpcd_max_link_rate,
		gInfo->shared_info->dpcd_max_lane_count);

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

	transmitBuffer[0] = (message->request << 4) | ((message->address >> 16) & 0x0f);
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
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize, receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					result = result > 1 ? min_c((ssize_t)receiveBuffer[1], (ssize_t)message->size) : (ssize_t)message->size;
				}
				break;

			case DP_AUX_NATIVE_READ:
			case DP_AUX_I2C_READ:
				receiveSize = message->size + 1;
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize, receiveBuffer, receiveSize);
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
			if (i < transmitSize) data |= ((uint32)transmitBuffer[i++]) << 16;
			if (i < transmitSize) data |= ((uint32)transmitBuffer[i++]) << 8;
			if (i < transmitSize) data |= transmitBuffer[i++];
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

	uint8 bytes = (status & INTEL_ARC_DP_AUX_CTL_MSG_SIZE_MASK) >> INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT;
	if (bytes == 0 || bytes > 20)
		return B_BUSY;
	if (bytes > receiveSize)
		bytes = receiveSize;

	for (uint8 i = 0; i < bytes;) {
		uint32 data = 0;
		if (!read_register(aux_data_register(ddiPort, i / 4), data))
			return B_ERROR;
		receiveBuffer[i++] = data >> 24;
		if (i < bytes) receiveBuffer[i++] = data >> 16;
		if (i < bytes) receiveBuffer[i++] = data >> 8;
		if (i < bytes) receiveBuffer[i++] = data;
	}

	return bytes;
}


static inline uint32
read32(accelerant_info* info, uint32 offset)
{
	return *(volatile uint32*)(info->registers + offset);
}

static inline void
write32(accelerant_info* info, uint32 offset, uint32 value)
{
	*(volatile uint32*)(info->registers + offset) = value;
}

static status_t
intel_arc_program_hdmi_dpll(accelerant_info* info, int dpll_id, uint32 pixel_clock_khz)
{
	uint32 cfgcr0_reg, cfgcr1_reg, enable_reg;

	switch (dpll_id) {
		case 0:
			cfgcr0_reg = INTEL_ARC_TGL_DPLL0_CFGCR0;
			cfgcr1_reg = INTEL_ARC_TGL_DPLL0_CFGCR1;
			enable_reg = INTEL_ARC_TGL_DPLL0_ENABLE;
			break;
		case 1:
			cfgcr0_reg = INTEL_ARC_TGL_DPLL1_CFGCR0;
			cfgcr1_reg = INTEL_ARC_TGL_DPLL1_CFGCR1;
			enable_reg = INTEL_ARC_TGL_DPLL1_ENABLE;
			break;
		case 4:
			cfgcr0_reg = INTEL_ARC_TGL_DPLL4_CFGCR0;
			cfgcr1_reg = INTEL_ARC_TGL_DPLL4_CFGCR1;
			enable_reg = INTEL_ARC_TGL_DPLL4_ENABLE;
			break;
		default:
			return B_BAD_VALUE;
	}

	// 1. Disabilita il DPLL se attivo per riconfigurarlo
	uint32 val = read32(info, enable_reg);
	if ((val & INTEL_ARC_TGL_DPLL_ENABLE) != 0) {
		val &= ~INTEL_ARC_TGL_DPLL_ENABLE;
		write32(info, enable_reg, val);
		snooze(10);
	}

	// 2. Abilita la potenza del DPLL e attendi lo stato OK
	val |= INTEL_ARC_TGL_DPLL_POWER_ENABLE;
	write32(info, enable_reg, val);

	int timeout = 1000;
	while (--timeout > 0) {
		if ((read32(info, enable_reg) & INTEL_ARC_TGL_DPLL_POWER_STATE) != 0)
			break;
		snooze(1);
	}
	if (timeout == 0)
		return B_TIMED_OUT;

	// 3. Calcolo divisori e frequenza DCO (RefClk = 24 MHz)
	// Per HDMI/TMDS: F_dco = PixelClock * 5 * P * K * Q
	uint32 pdiv = INTEL_ARC_TGL_DPLL_PDIV_2;
	uint32 kdiv = INTEL_ARC_TGL_DPLL_KDIV_1;
	uint32 qdiv = 1;
	uint32 p_val = 2;
	uint32 k_val = 1;

	if (pixel_clock_khz < 50000) {
		pdiv = INTEL_ARC_TGL_DPLL_PDIV_7;
		p_val = 7;
		kdiv = INTEL_ARC_TGL_DPLL_KDIV_2;
		k_val = 2;
	} else if (pixel_clock_khz < 100000) {
		pdiv = INTEL_ARC_TGL_DPLL_PDIV_5;
		p_val = 5;
		kdiv = INTEL_ARC_TGL_DPLL_KDIV_1;
		k_val = 1;
	} else if (pixel_clock_khz < 150000) {
		pdiv = INTEL_ARC_TGL_DPLL_PDIV_3;
		p_val = 3;
		kdiv = INTEL_ARC_TGL_DPLL_KDIV_1;
		k_val = 1;
	}

	const uint32 ref_clk_khz = 24000;
	uint64 dco_freq_khz = (uint64)pixel_clock_khz * 5 * p_val * k_val * qdiv;

	uint32 dco_int = (uint32)(dco_freq_khz / ref_clk_khz);
	uint32 dco_frac = (uint32)(((dco_freq_khz % ref_clk_khz) << INTEL_ARC_TGL_DPLL_DCO_FRACTION_SHIFT) / ref_clk_khz);

	// 4. Scrittura registri CFGCR0 e CFGCR1
	uint32 cfgcr0 = (dco_int & 0x1FF) | (dco_frac << INTEL_ARC_TGL_DPLL_DCO_FRACTION_SHIFT);
	uint32 cfgcr1 = pdiv | kdiv | (qdiv << INTEL_ARC_TGL_DPLL_QDIV_RATIO_SHIFT) | INTEL_ARC_TGL_DPLL_QDIV_ENABLE;

	write32(info, cfgcr0_reg, cfgcr0);
	write32(info, cfgcr1_reg, cfgcr1);

	// 5. Abilita il DPLL e attendi il Lock del PLL
	val = read32(info, enable_reg);
	val |= INTEL_ARC_TGL_DPLL_ENABLE;
	write32(info, enable_reg, val);

	timeout = 1000;
	while (--timeout > 0) {
		if ((read32(info, enable_reg) & INTEL_ARC_TGL_DPLL_LOCK) != 0)
			break;
		snooze(1);
	}
	if (timeout == 0)
		return B_TIMED_OUT;

	// 6. Instradamento clock su DPCLKA_CFGCR0 (sblocco DDI A/B/C)
	uint32 dpclka = read32(info, INTEL_ARC_TGL_DPCLKA_CFGCR0);
	dpclka &= ~(INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF
		| INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF
		| INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF);
	write32(info, INTEL_ARC_TGL_DPCLKA_CFGCR0, dpclka);

	return B_OK;
}
