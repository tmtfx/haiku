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

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)
extern accelerant_info* gInfo;

static const arc_mit_buf_trans_entry kDg2SnpsDp14Trans[] = {
	{25, 0, 0}, {32, 0, 6}, {35, 0, 10}, {43, 0, 17}, {35, 0, 0},
	{45, 0, 8}, {48, 0, 14}, {47, 0, 0}, {55, 0, 7}, {62, 0, 0}
};

static const arc_mit_buf_trans_entry kDg2SnpsHdmiDefaultTrans = {62, 0, 0};

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
mode_matches_exactly(const display_mode& left, const display_mode& right)
{
	return left == right
		&& left.flags == right.flags
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

static const snps_hdmi_table_entry kDg2HdmiPllTable[] = {
	{65000, {0x0e801cf8, 0x2b000060, 0x00009048, 0x4000ffff, 0x00000000, 0x40000000, 0x00000000}},
	{106500, {0x0c801cf8, 0x28000460, 0x0000908a, 0xc000ffff, 0x33333333, 0x40000000, 0x00000000}},
	{108000, {0x0c801cf8, 0x28000460, 0x0000908c, 0xc000ffff, 0x66666666, 0x40000000, 0x00000000}},
};

static uint64
divide_round_up_u64(uint64 value, uint64 divisor)
{
	return (value + divisor - 1) / divisor;
}

static uint64
divide_round_closest_u64(uint64 value, uint64 divisor)
{
	return (value + divisor / 2) / divisor;
}

static uint64
interpolate_u64(uint64 x, uint64 x1, uint64 x2, uint64 y1, uint64 y2)
{
	const uint64 dydx = divide_round_up_u64((y2 - y1) * 100000ULL, x2 - x1);
	return y1 + divide_round_up_u64(dydx * (x - x1), 100000ULL);
}

static uint32
snps_phy_base_for_ddi_port(uint8 ddiPort)
{
	return INTEL_ARC_MMIO_SNPS_PHY_A_BASE + ddiPort * 0x1000;
}

static uint8
c20_custom_width(uint32 linkRate)
{
	if (linkRate == 1000000 || linkRate == 1350000 || linkRate == 2000000)
		return 2;
	return 0;
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

// ************** BATTLEMAGE ****************
// Parametri C10 PHY PLL per frequenze DisplayPort standard
struct intel_c10_dp_pll_state {
    uint8 tx0;
    uint8 tx1;
    uint8 pll0;
    uint8 pll1;
    uint8 cmn0;
};

// Tabelle dei coefficienti C10 per le rate standard DP
static bool
get_c10_dp_mpllb_state(uint32 linkRateKhz, intel_c10_dp_pll_state* state)
{
    switch (linkRateKhz) {
        case 162000: // RBR 1.62 Gbps
            *state = { .tx0 = 0x9A, .tx1 = 0x00, .pll0 = 0xC4, .pll1 = 0x21, .cmn0 = 0x05 };
            return true;
        case 270000: // HBR 2.70 Gbps
            *state = { .tx0 = 0x9A, .tx1 = 0x00, .pll0 = 0x48, .pll1 = 0x21, .cmn0 = 0x05 };
            return true;
        case 540000: // HBR2 5.40 Gbps
            *state = { .tx0 = 0x9A, .tx1 = 0x00, .pll0 = 0x48, .pll1 = 0x21, .cmn0 = 0x05 };
            return true;
        case 810000: // HBR3 8.10 Gbps
            *state = { .tx0 = 0x9A, .tx1 = 0x00, .pll0 = 0x48, .pll1 = 0x21, .cmn0 = 0x05 };
            return true;
        default:
            return false;
    }
}
// ******************************************

static uint32
refresh_rate_for_mode(const display_mode& mode)
{
	if (mode.timing.pixel_clock == 0 || mode.timing.h_total == 0
		|| mode.timing.v_total == 0) {
		return 60;
	}

	const uint64 totalPixels = (uint64)mode.timing.h_total
		* (uint64)mode.timing.v_total;
	if (totalPixels == 0)
		return 60;

	uint32 refresh = (uint32)(((uint64)mode.timing.pixel_clock * 1000ULL
		+ totalPixels / 2) / totalPixels);
	if (refresh < 25 || refresh > 240)
		return 60;
	return refresh;
}

static void
sanitize_mode_geometry(display_mode& mode, const char* origin)
{
	if (mode.virtual_width == 0 || mode.virtual_height == 0)
		return;

	const bool missingTiming = mode.timing.h_display == 0
		|| mode.timing.v_display == 0
		|| mode.timing.h_total == 0
		|| mode.timing.v_total == 0;
	const bool mismatchedGeometry = mode.timing.h_display != 0
		&& mode.timing.v_display != 0
		&& (mode.virtual_width != mode.timing.h_display
			|| mode.virtual_height != mode.timing.v_display);

	if (!missingTiming && !mismatchedGeometry)
		return;

	if (mismatchedGeometry) {
		debug_printf("intel_arc.accelerant: %s: timing %ux%u disagrees with virtual %ux%u, recomputing timings\n",
			origin, mode.timing.h_display, mode.timing.v_display,
			mode.virtual_width, mode.virtual_height);
	}

	const uint32 refresh = mismatchedGeometry ? 60 : refresh_rate_for_mode(mode);
	compute_display_timing(mode.virtual_width, mode.virtual_height, refresh, false,
		&mode.timing);
}

static uint32
scaler_control_register(int8 pipe, uint32 scalerIndex)
{
	switch (pipe) {
		case 0:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2A_CTRL : INTEL_ARC_MMIO_PS_1A_CTRL;
		case 1:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2B_CTRL : INTEL_ARC_MMIO_PS_1B_CTRL;
		case 2:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2C_CTRL : INTEL_ARC_MMIO_PS_1C_CTRL;
		default:
			return 0;
	}
}

static uint32
scaler_window_pos_register(int8 pipe, uint32 scalerIndex)
{
	switch (pipe) {
		case 0:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2A_WIN_POS : INTEL_ARC_MMIO_PS_1A_WIN_POS;
		case 1:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2B_WIN_POS : INTEL_ARC_MMIO_PS_1B_WIN_POS;
		case 2:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2C_WIN_POS : INTEL_ARC_MMIO_PS_1C_WIN_POS;
		default:
			return 0;
	}
}

static uint32
scaler_window_size_register(int8 pipe, uint32 scalerIndex)
{
	switch (pipe) {
		case 0:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2A_WIN_SIZE : INTEL_ARC_MMIO_PS_1A_WIN_SIZE;
		case 1:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2B_WIN_SIZE : INTEL_ARC_MMIO_PS_1B_WIN_SIZE;
		case 2:
			return scalerIndex == 2
				? INTEL_ARC_MMIO_PS_2C_WIN_SIZE : INTEL_ARC_MMIO_PS_1C_WIN_SIZE;
		default:
			return 0;
	}
}

static bool
is_mode_supported(display_mode* mode)
{
	return mode != NULL && *mode == gInfo->shared_info->current_mode;
}

static void
log_pipe_plane_state(const char* origin, int8 pipe)
{
	if (pipe < 0)
		return;

	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	struct register_log {
		const char*	name;
		uint32		offset;
	} registers[] = {
		{"PIPE_HTOTAL", INTEL_ARC_MMIO_PIPE_A_HTOTAL + pipeOffset},
		{"PIPE_HSYNC", INTEL_ARC_MMIO_PIPE_A_HSYNC + pipeOffset},
		{"PIPE_VTOTAL", INTEL_ARC_MMIO_PIPE_A_VTOTAL + pipeOffset},
		{"PIPE_VSYNC", INTEL_ARC_MMIO_PIPE_A_VSYNC + pipeOffset},
		{"PIPE_SIZE", INTEL_ARC_MMIO_PIPE_A_SIZE + pipeOffset},
		{"PIPE_DDI_FUNC_CTL", INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + pipeOffset},
		{"PLANE_CTL", INTEL_ARC_MMIO_PLANE_A_CONTROL + pipeOffset},
		{"PLANE_STRIDE", INTEL_ARC_MMIO_PLANE_A_STRIDE + pipeOffset},
		{"PLANE_POS", INTEL_ARC_MMIO_PLANE_A_POS + pipeOffset},
		{"PLANE_SIZE", INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + pipeOffset},
		{"PLANE_SURFACE", INTEL_ARC_MMIO_PLANE_A_SURFACE + pipeOffset},
		{"PS_1_CTRL", scaler_control_register(pipe, 1)},
		{"PS_1_WIN_POS", scaler_window_pos_register(pipe, 1)},
		{"PS_1_WIN_SIZE", scaler_window_size_register(pipe, 1)},
		{"PS_2_CTRL", scaler_control_register(pipe, 2)},
		{"PS_2_WIN_POS", scaler_window_pos_register(pipe, 2)},
		{"PS_2_WIN_SIZE", scaler_window_size_register(pipe, 2)}
	};

	debug_printf("intel_arc.accelerant: %s: register snapshot for pipe %" B_PRId8 "\n",
		origin, pipe);
	for (size_t i = 0; i < B_COUNT_OF(registers); i++) {
		uint32 value = 0;
		if (read_register(registers[i].offset, value)) {
			debug_printf("  - %s [0x%05" B_PRIx32 "] = 0x%08" B_PRIx32 "\n",
				registers[i].name, registers[i].offset, value);
		} else {
			debug_printf("  - %s [0x%05" B_PRIx32 "] = <read failed>\n",
				registers[i].name, registers[i].offset);
		}
	}
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

status_t
create_mode_list(void)
{
	display_mode mode = gInfo->shared_info->current_mode;
	sanitize_mode_geometry(mode, "create_mode_list");
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

    const int8 pipe = gInfo->shared_info->active_pipe;
	if (pipe >= 0) {
		const uint32 hTotal = gInfo->shared_info->pipe_h_total[pipe];
		const uint32 hSync = gInfo->shared_info->pipe_h_sync[pipe];
		const uint32 vTotal = gInfo->shared_info->pipe_v_total[pipe];
		const uint32 vSync = gInfo->shared_info->pipe_v_sync[pipe];
		log_pipe_plane_state("create_mode_list inherited state", pipe);

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
	sanitize_mode_geometry(mode, "create_mode_list inherited registers");
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0) {
		compute_display_timing(mode.virtual_width, mode.virtual_height,
			refresh_rate_for_mode(mode), false,
			&mode.timing);
	}
	
    if (mode.timing.pixel_clock == 0) {
        mode.timing.pixel_clock = ((uint32)mode.timing.h_total
            * (uint32)mode.timing.v_total * refresh_rate_for_mode(mode)) / 1000;
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
        // senza reintrodurre la modalità ereditata dal GOP come seed: se il
        // seed ha timing incoerenti può sovrascrivere la vera mode EDID con la
        // stessa risoluzione ma pixel clock differente.
        gInfo->mode_list_area = create_display_modes("intel arc modes",
            targetEdid, NULL, 0, kSupportedSpaces, kNumSupportedSpaces,
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

static bool
is_mode_in_list(const display_mode& mode, display_mode* match)
{
	const display_mode* fallback = NULL;

	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		const display_mode& current = gInfo->mode_list[i];
		if (current.virtual_width != mode.virtual_width
			|| current.virtual_height != mode.virtual_height
			|| current.space != mode.space) {
			continue;
		}

		if (mode_matches_exactly(current, mode)) {
			if (match != NULL)
				*match = current;
			return true;
		}

		if (fallback == NULL)
			fallback = &current;
	}

	if (fallback != NULL) {
		if (match != NULL)
			*match = *fallback;
		return true;
	}

	return false;
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

/*
 * Note: We cannot use Haiku's standard dp_decode_link_rate() from <dp.h>
 * because it currently only supports rates up to 5.4 GHz (HBR2).
 * Modern Intel Arc (Alchemist/Battlemage) requires HBR3 (8.1 GHz)
 * and DP 2.0 UHBR rates (13.5 GHz and 20.0 GHz), which are supported here.
 */
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

static status_t
program_battlemage_cx0_dpll(uint8 ddiPort, uint32 linkRateKhz)
{
    intel_c10_dp_pll_state state = {};
    if (!get_c10_dp_mpllb_state(linkRateKhz, &state)) {
        debug_printf("intel_arc.accelerant ERROR: BMG Unsupported DP link rate %u kHz\n", linkRateKhz);
        return B_BAD_VALUE;
    }

    const uint32 portClkCtl = INTEL_ARC_BMG_PORT_CLOCK_CTL(ddiPort);
    const uint32 cx0Base = INTEL_ARC_BMG_CX0_LN0_PHY_BASE(ddiPort);

    // 1. Spegni il clock di porta e disabilita la PHY CX0 per riconfigurazione
    uint32 val = read32(gInfo, portClkCtl);
    val &= ~(BMG_PORT_CLOCK_CTL_ENABLE | BMG_PORT_CLOCK_CTL_LINK_CLK_EN);
    write32(gInfo, portClkCtl, val);
    (void)wait_for_clear(portClkCtl, BMG_PORT_CLOCK_CTL_LOCK, 5000);

    // 2. Scrittura dei registri C10 PHY per le lane
    write32(gInfo, cx0Base + INTEL_ARC_BMG_CX0_C10_PLL0, state.pll0);
    write32(gInfo, cx0Base + INTEL_ARC_BMG_CX0_C10_PLL1, state.pll1);
    write32(gInfo, cx0Base + INTEL_ARC_BMG_CX0_C10_TX0,  state.tx0);
    write32(gInfo, cx0Base + INTEL_ARC_BMG_CX0_C10_TX1,  state.tx1);
    write32(gInfo, cx0Base + INTEL_ARC_BMG_CX0_C10_CMN0, state.cmn0);

    // 3. Abilita la PHY in modalità C10
    val = read32(gInfo, portClkCtl);
    val |= BMG_PORT_CLOCK_CTL_ENABLE | BMG_PORT_CLOCK_CTL_MODE_C10;
    write32(gInfo, portClkCtl, val);

    // 4. Attendi il Lock della PLL
    status_t status = wait_for_set(portClkCtl, BMG_PORT_CLOCK_CTL_LOCK, 10000);
    if (status != B_OK) {
        debug_printf("intel_arc.accelerant ERROR: BMG CX0 PHY failed to lock on port %u!\n", ddiPort);
        return status;
    }

    // 5. Abilita l'uscita del clock verso il controller DDI
    val = read32(gInfo, portClkCtl);
    val |= BMG_PORT_CLOCK_CTL_LINK_CLK_EN;
    write32(gInfo, portClkCtl, val);

    return B_OK;
}

static bool
compute_snps_dp_mpllb(uint32 linkRateKhz, snps_mpllb_state* state)
{
    if (state == nullptr)
        return false;

    memset(state, 0, sizeof(snps_mpllb_state));

    switch (linkRateKhz) {
        case 162000: // RBR (1.62 Gbps)
            state->mpllb_cp       = 0x00002008;
            state->mpllb_div      = 0x0000A000;
            state->mpllb_div2     = 0x00000000;
            state->mpllb_sscen    = 0x00000000;
            state->mpllb_sscstep  = 0x00000000;
            state->mpllb_fracn1   = 0x00000000;
            state->mpllb_fracn2   = 0x00000000;
            return true;

        case 270000: // HBR (2.70 Gbps)
            state->mpllb_cp       = 0x00002008;
            state->mpllb_div      = 0x0000A001;
            state->mpllb_div2     = 0x00000000;
            state->mpllb_sscen    = 0x00000000;
            state->mpllb_sscstep  = 0x00000000;
            state->mpllb_fracn1   = 0x00000000;
            state->mpllb_fracn2   = 0x00000000;
            return true;

        case 540000: // HBR2 (5.40 Gbps)
            state->mpllb_cp       = 0x00002008;
            state->mpllb_div      = 0x0000A002;
            state->mpllb_div2     = 0x00000000;
            state->mpllb_sscen    = 0x00000000;
            state->mpllb_sscstep  = 0x00000000;
            state->mpllb_fracn1   = 0x00000000;
            state->mpllb_fracn2   = 0x00000000;
            return true;

        case 810000: // HBR3 (8.10 Gbps)
            state->mpllb_cp       = 0x00002008;
            state->mpllb_div      = 0x0000A003;
            state->mpllb_div2     = 0x00000000;
            state->mpllb_sscen    = 0x00000000;
            state->mpllb_sscstep  = 0x00000000;
            state->mpllb_fracn1   = 0x00000000;
            state->mpllb_fracn2   = 0x00000000;
            return true;

        default:
            return false;
    }
}

static uint32
snps_phy_enable_reg_for_ddi_port(uint8 ddiPort)
{
	switch (ddiPort) {
		case 0:
			return INTEL_ARC_TGL_DPLL0_ENABLE;
		case 1:
			return INTEL_ARC_TGL_DPLL1_ENABLE;
		case 2:
			return INTEL_ARC_TGL_DPLL4_ENABLE;
		default:
			return INTEL_ARC_TGL_DPLL1_ENABLE;
	}
}


static status_t
program_port_dpll(uint8 ddiPort, uint32 linkRateKhz)
{
    debug_printf("intel_arc.accelerant: program_port_dpll(ddiPort=%u, linkRate=%u kHz)\n",
        ddiPort, linkRateKhz);

    // --- 1. ARCHITETTURA ALCHEMIST (DG2 - SNPS PHY) ---
    if (gInfo->shared_info->family == INTEL_ARC_FAMILY_ALCHEMIST) {
        const uint32 phyBase = snps_phy_base_for_ddi_port(ddiPort);
        const uint32 enableReg = snps_phy_enable_reg_for_ddi_port(ddiPort);

        snps_mpllb_state state = {};
        if (!compute_snps_dp_mpllb(linkRateKhz, &state)) {
            debug_printf("intel_arc.accelerant ERROR: Invalid DP link rate %u kHz\n", linkRateKhz);
            return B_BAD_VALUE;
        }

        uint32 enableValue = read32(gInfo, enableReg);
        write32(gInfo, enableReg, enableValue & ~INTEL_ARC_TGL_DPLL_ENABLE);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase),
            state.mpllb_div & ~INTEL_ARC_SNPS_PHY_MPLLB_FORCE_EN);
        (void)wait_for_clear(enableReg, INTEL_ARC_TGL_DPLL_LOCK, 5000);

        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_CP(phyBase), state.mpllb_cp);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase), state.mpllb_div);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV2(phyBase), state.mpllb_div2);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_SSCEN(phyBase), state.mpllb_sscen);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_SSCSTEP(phyBase), state.mpllb_sscstep);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN1(phyBase), state.mpllb_fracn1);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN2(phyBase), state.mpllb_fracn2);

        enableValue = read32(gInfo, enableReg) | INTEL_ARC_TGL_DPLL_ENABLE;
        write32(gInfo, enableReg, enableValue);
        write32(gInfo, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase),
            state.mpllb_div | INTEL_ARC_SNPS_PHY_MPLLB_FORCE_EN);

        status_t status = wait_for_set(enableReg, INTEL_ARC_TGL_DPLL_LOCK, 5000);
        if (status != B_OK) {
            debug_printf("intel_arc.accelerant ERROR: SNPS PHY MPLLB failed to lock!\n");
            return status;
        }

        uint32 dpclka = read32(gInfo, INTEL_ARC_TGL_DPCLKA_CFGCR0);
        dpclka &= ~(1U << (ddiPort + 16));
        write32(gInfo, INTEL_ARC_TGL_DPCLKA_CFGCR0, dpclka);

        return B_OK;
    }

    // --- 2. ARCHITETTURA BATTLEMAGE (Xe2 - CX0 PHY) ---
    if (gInfo->shared_info->family == INTEL_ARC_FAMILY_BATTLEMAGE) {
        return program_battlemage_cx0_dpll(ddiPort, linkRateKhz);
    }

    return B_BAD_TYPE;
}

/*
 * Note: We cannot use Haiku's standard dp_encode_link_rate() from <dp.h>
 * because it currently only supports rates up to 5.4 GHz (HBR2).
 * Modern Intel Arc (Alchemist/Battlemage) requires HBR3 (8.1 GHz)
 * and DP 2.0 UHBR rates (13.5 GHz and 20.0 GHz), which are supported here.
 */
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

	const uint32 phyBase = snps_phy_base_for_ddi_port(ddiPort);

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
		status_t status = program_port_dpll(gInfo->shared_info->active_ddi_port, linkBandwidth);
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

static uint32
clamp_u32(uint32 value, uint32 low, uint32 high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static void
compute_snps_hdmi_mpllb(uint32 pixelClockKHz, snps_mpllb_state& state)
{
	for (size_t i = 0; i < B_COUNT_OF(kDg2HdmiPllTable); i++) {
		const uint32 tableClock = kDg2HdmiPllTable[i].clockKHz;
		const uint32 delta = pixelClockKHz > tableClock
			? pixelClockKHz - tableClock : tableClock - pixelClockKHz;
		if (delta <= 1000) {
			state = kDg2HdmiPllTable[i].state;
			debug_printf("intel_arc.accelerant: compute_snps_hdmi_mpllb(): snapped %" B_PRIu32 " kHz to upstream DG2 HDMI table %" B_PRIu32 " kHz\n",
				pixelClockKHz, tableClock);
			return;
		}
	}

	static const uint64 dg2CurveFreqHz[2][8] = {
		{2500000000ULL, 3000000000ULL, 3000000000ULL, 3500000000ULL, 3500000000ULL,
			4000000000ULL, 4000000000ULL, 5000000000ULL},
		{4000000000ULL, 4600000000ULL, 4601000000ULL, 5400000000ULL, 5401000000ULL,
			6600000000ULL, 6601000000ULL, 8001000000ULL}
	};
	static const uint64 dg2Curve0[2][8] = {
		{34149871ULL, 39803269ULL, 36034544ULL, 40601014ULL, 35646940ULL, 40016109ULL, 35127987ULL, 41889522ULL},
		{70000000ULL, 78770454ULL, 70451838ULL, 80427119ULL, 70991400ULL, 84230173ULL, 72945921ULL, 87064218ULL}
	};
	static const uint64 dg2Curve1[2][8] = {
		{85177000000000ULL, 79385227160000ULL, 95672603580000ULL, 88857207160000ULL,
			109379790900000ULL, 103528193900000ULL, 131941242400000ULL, 117279000000000ULL},
		{60255000000000ULL, 55569000000000ULL, 72036000000000ULL, 69509000000000ULL,
			81785000000000ULL, 731030000000000ULL, 96591000000000ULL, 69077000000000ULL}
	};
	static const uint64 dg2Curve2[2][8] = {
		{2186930000ULL, 2835287134ULL, 2395395343ULL, 2932270687ULL, 2351887545ULL, 2861031697ULL, 2294149152ULL, 3091730000ULL},
		{4560000000ULL, 5570000000ULL, 4610000000ULL, 5770000000ULL, 4670000000ULL, 6240000000ULL, 4890000000ULL, 6600000000ULL}
	};

	const uint64 pixelClockHz = (uint64)pixelClockKHz * 1000ULL;
	const uint64 dataRate = pixelClockHz * 10ULL;

	uint32 mpllAnaV2i;
	uint32 txClkDiv;
	if (dataRate <= INTEL_SNPS_PHY_HDMI_9999MHZ) {
		mpllAnaV2i = 2;
		txClkDiv = (uint32)floor(log2((double)INTEL_SNPS_PHY_HDMI_9999MHZ / (double)dataRate));
	} else {
		mpllAnaV2i = 3;
		txClkDiv = (uint32)floor(log2((double)INTEL_SNPS_PHY_HDMI_16GHZ / (double)dataRate));
	}

	uint64 vcoClock = (dataRate << txClkDiv) >> 1;
	const uint32 refClk = 100000000;
	const uint32 refClkDiv = 1;
	const uint32 refClkPostscalar = refClk >> refClkDiv;
	const uint32 vcoDivRefclkInteger = (uint32)(vcoClock / refClkPostscalar);
	const uint64 vcoClockRemainder = vcoClock % refClkPostscalar;
	const uint32 vcoDivRefclkFracn = (uint32)((vcoClockRemainder << 32) / refClkPostscalar);
	uint32 fracnQuot = vcoDivRefclkFracn >> 16;
	uint32 fracnRem = vcoDivRefclkFracn & 0xffff;
	fracnRem = fracnRem - (fracnRem >> 15);
	const uint32 fracnDen = 0xffff;
	const bool fracnEn = fracnQuot != 0 || fracnRem != 0;
	const bool pmixEn = fracnEn;
	const uint32 multiplier = (vcoDivRefclkInteger - 16) * 2;

	const int curveSet = (int)mpllAnaV2i - 2;
	int segment = 0;
	int anaFreqVco = 0;
	for (int i = 0; i < 8; i += 2) {
		if (vcoClock <= dg2CurveFreqHz[curveSet][i + 1]) {
			segment = i;
			anaFreqVco = 3 - (i >> 1);
			break;
		}
	}

	const uint64 curve0 = interpolate_u64(vcoClock,
		dg2CurveFreqHz[curveSet][segment], dg2CurveFreqHz[curveSet][segment + 1],
		dg2Curve0[curveSet][segment], dg2Curve0[curveSet][segment + 1]);
	const uint64 curve2 = interpolate_u64(vcoClock,
		dg2CurveFreqHz[curveSet][segment], dg2CurveFreqHz[curveSet][segment + 1],
		dg2Curve2[curveSet][segment], dg2Curve2[curveSet][segment + 1]);
	uint64 curve1 = interpolate_u64(vcoClock,
		dg2CurveFreqHz[curveSet][segment], dg2CurveFreqHz[curveSet][segment + 1],
		dg2Curve1[curveSet][segment], dg2Curve1[curveSet][segment + 1]);
	curve1 /= 100ULL;

	const uint64 vcoDivRefclkFloat = vcoClock * (1000000000000ULL / refClkPostscalar);
	const uint64 curve2Scaled1 = (curve2 * (4 - mpllAnaV2i)) / 16000ULL;
	const uint64 curve2Scaled2 = (curve2 * (4 - mpllAnaV2i)) / 160ULL;
	const uint64 scaledVcoDivRefclk1 = 112008301ULL * (vcoDivRefclkFloat / 100000ULL);
	const uint64 adjustedVcoClock1 = 1000000000000ULL
		* divide_round_up_u64(scaledVcoDivRefclk1,
			curve0 * divide_round_up_u64(curve1, 1000000000ULL));
	const uint32 anaCpInt = clamp_u32((uint32)divide_round_closest_u64(
		adjustedVcoClock1 / curve2Scaled1, 1000000000000ULL), 1, 127);

	const uint64 curve2ScaledInt = curve2Scaled1 * anaCpInt;
	const uint64 interpolatedProduct = curve1
		* (curve2ScaledInt * (curve0 / 1000000000ULL));
	const uint64 scaledInterpolatedSqrt = (uint64)sqrtl(
		(long double)(divide_round_up_u64(interpolatedProduct, vcoDivRefclkFloat)
			* (1000000000000ULL / 55ULL)));
	const uint64 scaledVcoDivRefclk2 = divide_round_up_u64(vcoDivRefclkFloat, 1000000ULL);
	const uint64 adjustedVcoClock2 = 1460281ULL * divide_round_up_u64(
		scaledInterpolatedSqrt * scaledVcoDivRefclk2, curve1);
	const uint32 anaCpProp = clamp_u32((uint32)divide_round_up_u64(
		adjustedVcoClock2, curve2Scaled2), 1, 127);
	const uint32 anaCpIntGs = 64;
	const uint32 anaCpPropGs = 124;

	state.mpllb_cp
		= ((anaCpInt & 0x7f) << 25)
		| ((anaCpIntGs & 0x7f) << 17)
		| ((anaCpProp & 0x7f) << 9)
		| ((anaCpPropGs & 0x7f) << 1);
	state.mpllb_div
		= INTEL_ARC_SNPS_PHY_MPLLB_DIV5_CLK_EN
		| ((mpllAnaV2i & 0x3) << INTEL_ARC_SNPS_PHY_MPLLB_V2I_SHIFT)
		| ((anaFreqVco & 0x3) << INTEL_ARC_SNPS_PHY_MPLLB_FREQ_VCO_SHIFT)
		| ((txClkDiv & 0x7) << INTEL_ARC_SNPS_PHY_MPLLB_TX_CLK_DIV_SHIFT)
		| (pmixEn ? INTEL_ARC_SNPS_PHY_MPLLB_PMIX_EN : 0);
	state.mpllb_div2
		= ((1U & 0x7) << 15)
		| ((refClkDiv & 0x7) << 12)
		| (multiplier & 0xfff);
	state.mpllb_fracn1
		= INTEL_ARC_SNPS_PHY_MPLLB_FRACN_CGG_UPDATE_EN
		| (fracnEn ? INTEL_ARC_SNPS_PHY_MPLLB_FRACN_EN : 0)
		| (fracnDen & 0xffff);
	state.mpllb_fracn2
		= ((fracnRem & 0xffff) << 16)
		| (fracnQuot & 0xffff);
	state.mpllb_sscen = INTEL_ARC_SNPS_PHY_MPLLB_SSC_UP_SPREAD;
	state.mpllb_sscstep = 0;
}

static status_t
intel_arc_program_hdmi_dpll(accelerant_info* info, uint8 ddiPort, uint32 pixel_clock_khz)
{
	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_BATTLEMAGE) {
		debug_printf("intel_arc.accelerant: Battlemage does not use Combo DPLL registers, DPLL programmed by driver\n");
		return B_OK;
	}

	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_ALCHEMIST) {
		const uint32 phyBase = snps_phy_base_for_ddi_port(ddiPort);
		const uint32 enableReg = snps_phy_enable_reg_for_ddi_port(ddiPort);
		snps_mpllb_state state = {};
		compute_snps_hdmi_mpllb(pixel_clock_khz, state);

		debug_printf("intel_arc.accelerant: intel_arc_program_hdmi_dpll(): SNPS MPLLB path ddiPort=%u phyBase=0x%05" B_PRIx32 " pixel_clock=%" B_PRIu32 " kHz\n",
			ddiPort, phyBase, pixel_clock_khz);

		uint32 enableValue = read32(info, enableReg);
		write32(info, enableReg, enableValue & ~INTEL_ARC_TGL_DPLL_ENABLE);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase),
			state.mpllb_div & ~INTEL_ARC_SNPS_PHY_MPLLB_FORCE_EN);
		(void)wait_for_clear(enableReg, INTEL_ARC_TGL_DPLL_LOCK, 5000);

		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_CP(phyBase), state.mpllb_cp);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase), state.mpllb_div);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV2(phyBase), state.mpllb_div2);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_SSCEN(phyBase), state.mpllb_sscen);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_SSCSTEP(phyBase), state.mpllb_sscstep);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN1(phyBase), state.mpllb_fracn1);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN2(phyBase), state.mpllb_fracn2);

		enableValue = read32(info, enableReg) | INTEL_ARC_TGL_DPLL_ENABLE;
		write32(info, enableReg, enableValue);
		write32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase),
			state.mpllb_div | INTEL_ARC_SNPS_PHY_MPLLB_FORCE_EN);

		status_t status = wait_for_set(enableReg, INTEL_ARC_TGL_DPLL_LOCK, 5000);
		debug_printf("intel_arc.accelerant: HDMI MPLLB regs: CP=0x%08" B_PRIx32 ", DIV=0x%08" B_PRIx32 ", DIV2=0x%08" B_PRIx32 ", FRACN1=0x%08" B_PRIx32 ", FRACN2=0x%08" B_PRIx32 ", SSCEN=0x%08" B_PRIx32 ", ENABLE=0x%08" B_PRIx32 "\n",
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_CP(phyBase)),
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV(phyBase)),
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_DIV2(phyBase)),
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN1(phyBase)),
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_FRACN2(phyBase)),
			read32(info, INTEL_ARC_MMIO_SNPS_PHY_MPLLB_SSCEN(phyBase)),
			read32(info, enableReg));
		return status;
	}

	return B_UNSUPPORTED;
}

static status_t
apply_hdmi_phy_levels(uint8 ddiPort, int8 pipe)
{
	const uint32 modeSel = (gInfo->shared_info->pipe_ddi_func_ctl[pipe]
		& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
	if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST
		|| modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
		return B_OK;
	}

	if (gInfo->shared_info->family != INTEL_ARC_FAMILY_ALCHEMIST)
		return B_OK;

	const uint32 phyBase = snps_phy_base_for_ddi_port(ddiPort);
	const uint32 value = ((uint32)kDg2SnpsHdmiDefaultTrans.main << 18)
		| ((uint32)kDg2SnpsHdmiDefaultTrans.post << 10)
		| ((uint32)kDg2SnpsHdmiDefaultTrans.pre << 2);
	for (uint32 lane = 0; lane < 4; lane++)
		write_register(INTEL_ARC_MMIO_SNPS_PHY_TX_EQ(phyBase, lane), value);

	debug_printf("intel_arc.accelerant: HDMI PHY levels applied: phyBase=0x%05" B_PRIx32 ", txeq=0x%08" B_PRIx32 "\n",
		phyBase, value);
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
	sanitize_mode_geometry(target, "intel_arc_set_display_mode");

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
	write_register(scaler_control_register(pipe, 1), 0);
	write_register(scaler_control_register(pipe, 2), 0);
	//const uint32 hDisplay = target.timing.h_display; // 1280
	//const uint32 vDisplay = target.timing.v_display; // 1024
	//debug_printf("intel_arc.accelerant: imposto pipe_size e plane_image_size con virtual_width e virtual_height invece che timing.h_display e v_display\n");
	const uint32 hDisplay = target.timing.h_display;
	const uint32 vDisplay = target.timing.v_display;
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
	log_pipe_plane_state("intel_arc_set_display_mode before MMIO writes", pipe);
	
	//gInfo->shared_info->plane_control[pipe] |= INTEL_ARC_PLANE_ENABLE; // Attiva PLANE_CTL_ENABLE
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
    
    const uint32 gammaModeReg = INTEL_ARC_GAMMA_MODE_BASE + (pipe * 0x1000);
    uint32 gammaValue = 0;

    // Legge il valore attuale del registro per preservare gli altri flag hardware
    if (read_register(gammaModeReg, gammaValue)) {
        if (target.space == B_COLOR_8_BIT) {
        	debug_printf("intel_arc.accelerant: CLEARING GAMMAMODE for 8-bits color space\n");
            // Bit [1:0] = 00b -> Forza la Pipe in Legacy Palette 8-bit Mode
            gammaValue &= ~3U;
        } else {
            // Per 16/24/32 bit rispristina/imposta il bypass o Direct Gamma
            // (Bit [1:0] = 01b o valore di default per colori diretti)
            debug_printf("intel_arc.accelerant: DIRECT GAMMA for NON-8bits color space\n");
            gammaValue = (gammaValue & ~3U) | 1U;
        }
        write_register(gammaModeReg, gammaValue);
    } else {
        // Fallback in caso di fallimento della lettura
        if (target.space == B_COLOR_8_BIT)
            write_register(gammaModeReg, 0);
    }
    
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
	log_pipe_plane_state("intel_arc_set_display_mode after MMIO writes", pipe);
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
		status = intel_arc_program_hdmi_dpll(gInfo,
			gInfo->shared_info->active_ddi_port, target.timing.pixel_clock);
    	if (status != B_OK) {
        	debug_printf("intel_arc.accelerant ERROR: intel_arc_program_hdmi_dpll failed: %s\n", strerror(status));
        	return status;
    	}

		status = apply_hdmi_phy_levels(gInfo->shared_info->active_ddi_port, pipe);
		if (status != B_OK) {
			debug_printf("intel_arc.accelerant ERROR: apply_hdmi_phy_levels failed: %s\n", strerror(status));
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

status_t
intel_arc_set_indexed_colors(uint32 count, uint8 first,
    uint8* color_data, uint32 flags)
{
    if (color_data == NULL)
        return B_BAD_VALUE;

    if (first + count > 256)
        return B_BAD_VALUE;

    // Recupera la pipe attualmente attiva per lo schermo
    uint32 pipe = gInfo->shared_info->active_pipe;

    // Indirizzo base dei registri LGC_PALETTE per la pipe attiva
    // Offset: 0x4A000 + (pipe * 0x400)
    uint32 paletteBase = INTEL_ARC_LGC_PALETTE_BASE + (pipe * 0x400);

    for (uint32 i = 0; i < count; i++) {
        uint8 index = first + i;
        
        if (index > 255)
            break;

        // L'array color_data contiene triplette consecutive R, G, B
        uint8 r = color_data[i * 3 + 0];
        uint8 g = color_data[i * 3 + 1];
        uint8 b = color_data[i * 3 + 2];

        // Formato registro Intel LGC_PALETTE: [23:16] Red, [15:8] Green, [7:0] Blue
        uint32 colorValue = ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;

        // Scrittura del registro della tavolozza (4 byte per voce)
        write_register(paletteBase + (index * 4), colorValue);
    }

    return B_OK;
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

status_t
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
	uint8 candidates[6];
	
	if (gInfo->shared_info->active_pipe >= 0
		&& gInfo->shared_info->active_ddi_port > 0
		&& gInfo->shared_info->active_ddi_port <= 4) {
		candidates[candidateCount++] = gInfo->shared_info->active_ddi_port;
	}

	for (uint8 port = 1; port <= 4; port++) {
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

//static 
status_t
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

	const uint8 activePort = gInfo->shared_info->active_ddi_port;
	if (activePort == 0) {
		gInfo->has_edid = false;
		return B_OK;
	}

	(void)read_edid_from_hardware();

	if (activePort <= 4 && (gInfo->shared_info->detected_port_bits & (1 << activePort)) == 0) {
		gInfo->has_edid = false;
		return B_OK;
	}

	if (gInfo->shared_info->current_mode.virtual_width == 0
		|| gInfo->shared_info->current_mode.virtual_height == 0) {
		return B_OK;
	}

	if (gInfo->shared_info->has_dpcd) {
		return configure_dp_link(&gInfo->shared_info->current_mode);
	}

	return B_OK;
}

//static 
status_t
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

//static 
status_t
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

//static
status_t
program_ddi_buffer(uint8 ddiPort, int8 pipe, uint32 lanes, bool enable)
{
	debug_printf("intel_arc.accelerant: program_ddi_buffer(ddiPort=%u, pipe=%d, lanes=%u, enable=%d)\n",
		ddiPort, pipe, lanes, enable);
	if (ddiPort > 6)
		return B_BAD_VALUE;

	const uint32 reg = INTEL_ARC_MMIO_DDI_BUF_CTL_A + ddiPort * 0x100;
	uint32 value = 0;
	read_register(reg, value);
	const uint32 modeSel = (gInfo->shared_info->pipe_ddi_func_ctl[pipe]
		& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
	const bool isDp = modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST
		|| modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST;
	const uint32 transSelect = isDp ? 0 : 9;
	value &= ~INTEL_ARC_DDI_PORT_WIDTH(4);
	value &= ~INTEL_ARC_DDI_BUF_TRANS_SELECT(0x7);
	value |= INTEL_ARC_DDI_BUF_TRANS_SELECT(transSelect);
	value |= INTEL_ARC_DDI_PORT_WIDTH(lanes);
	if (enable)
		value |= INTEL_ARC_DDI_BUF_CTL_ENABLE;
	else
		value &= ~INTEL_ARC_DDI_BUF_CTL_ENABLE;

	write_register(reg, value);
	debug_printf("intel_arc.accelerant: DDI_BUF_CTL[%u] = 0x%08" B_PRIx32 " (modeSel=%" B_PRIu32 ", trans=%" B_PRIu32 ")\n",
		ddiPort, value, modeSel, transSelect);
	if (ddiPort > 0 && ddiPort <= 4)
		gInfo->shared_info->port_state[ddiPort - 1] = value;

	if (enable)
		return wait_for_clear(reg, INTEL_ARC_DDI_BUF_IS_IDLE, 50000);

	return B_OK;
}

