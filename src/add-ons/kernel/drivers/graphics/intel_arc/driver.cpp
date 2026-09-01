/*
 * Copyright 2026, Haiku and the Pirati Del Frico contributors.
 * Distributed under the terms of the MIT License.
 *
 * This file was implemented for Haiku by reusing only MIT-compatible
 * in-tree references for structure and API usage. In particular:
 *  - src/add-ons/kernel/drivers/graphics/framebuffer/device.cpp
 *  - src/add-ons/kernel/drivers/graphics/framebuffer/framebuffer.cpp
 *  - src/add-ons/kernel/drivers/graphics/intel_extreme/driver.cpp
 *  - src/add-ons/kernel/drivers/graphics/intel_extreme/device.cpp
 *  - src/add-ons/kernel/drivers/graphics/intel_extreme/intel_extreme.cpp
 *
 * No Linux/GPL code was copied. BAR selection and ARC-specific policy here are
 * original heuristics written for this driver.
 */

#include "intel_arc.h"


#include <boot_item.h>
#include <frame_buffer_console.h>
#include <graphic_driver.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <vm/vm.h>
#include <util/AreaKeeper.h>
#include <vesa_info.h>

#include <driver_settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef IS_PIRATI_BUILD
#include "intel_arc_logo.h"
#endif


/*
 * Raw display/MMIO offsets below are a minimal, MIT-compatible reinterpretation
 * of register definitions for Intel Display Engine.
 */

#define TRACE_INTEL_ARC
#ifdef TRACE_INTEL_ARC
#	define TRACE(x...) dprintf("intel_arc: " x)
#else
#	define TRACE(x...) do {} while (false)
#endif
#define ERROR(x...) dprintf("intel_arc: " x)

#define MAX_DEVICES 8
#define MAX_CLONED_FRAMEBUFFER_SIZE (256 * 1024 * 1024)
#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))



static intel_arc_settings current_settings = {    
    "intel_arc.accelerant",	// accelerant filename
    false,					// dumprom, function still not integrated
    0,						// memory, override builtin memory size detection in MB
    true,					// hardcursor, if true use on-chip hardware cursor
    32,						// cursorbits, number of bits used to draw bitmap cursor
};

struct supported_device {
	uint16		device_id;
	uint16		family;
	const char*	name;
};

struct pci_bar_info {
	int32		index;
	uint32		flags;
	phys_addr_t	base;
	uint64		size;
	int32		consumed;
};

struct intel_arc_info {
	int32					open_count;
	int32					id;
	const supported_device*	device;
	pci_info				pci;

	area_id					shared_area;
	intel_arc_shared_info*	shared_info;

	area_id					registers_area;
	uint8*					registers;

	area_id					frame_buffer_area;
	uint8*					frame_buffer;
	uint32					irq;
	bool					irq_installed;
};


static status_t open_hook(const char* name, uint32 flags, void** cookie);
static status_t close_hook(void* cookie);
static status_t free_hook(void* cookie);
static status_t control_hook(void* cookie, uint32 msg, void* buf, size_t len);
static status_t read_hook(void* cookie, off_t pos, void* buffer, size_t* len);
static status_t write_hook(void* cookie, off_t pos, const void* buffer,
	size_t* len);

static status_t get_next_supported_device(int32* cookie, pci_info& info,
	const supported_device** _device);
static bool get_bar_info(const pci_info& info, int32 index, pci_bar_info& bar);
static bool select_bars(const pci_info& info, pci_bar_info& mmioBar,
	pci_bar_info& frameBufferBar);
static status_t init_device(intel_arc_info& info);
static void uninit_device(intel_arc_info& info);
static color_space get_color_space_for_depth(uint32 depth);
static bool read32(const intel_arc_info& info, uint32 offset, uint32& value);
static bool write32(const intel_arc_info& info, uint32 offset, uint32 value);
static void probe_display_state(intel_arc_info& info);
static int32 arc_interrupt_handler(void* data);
static int32 release_vblank_sem(intel_arc_info& info);
static void enable_interrupts(intel_arc_info& info, bool enable);
extern "C" void uninit_driver(void);


static device_hooks sDeviceHooks = {
	open_hook,
	close_hook,
	free_hook,
	control_hook,
	read_hook,
	write_hook,
	NULL,
	NULL,
	NULL,
	NULL
};


static const supported_device kSupportedDevices[] = {
	{ 0x5690, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A770M" },
	{ 0x5691, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A730M" },
	{ 0x5692, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A550M" },
	{ 0x5693, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A370M" },
	{ 0x5694, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A350M" },
	{ 0x5695, INTEL_ARC_FAMILY_ALCHEMIST, "Iris Xe MAX A200M" },
	{ 0x5696, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A570M" },
	{ 0x5697, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A530M" },
	{ 0x5698, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56A0, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A770" },
	{ 0x56A1, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A750" },
	{ 0x56A2, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A580" },
	{ 0x56A3, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56A4, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56A5, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A380" },
	{ 0x56A6, INTEL_ARC_FAMILY_ALCHEMIST, "Arc A310" },
	{ 0x56A7, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56A8, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56A9, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Xe Graphics" },
	{ 0x56B0, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Pro A30M" },
	{ 0x56B1, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Pro A40/A50" },
	{ 0x56B2, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Pro A60M" },
	{ 0x56B3, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Pro A60" },
	{ 0x56BA, INTEL_ARC_FAMILY_ALCHEMIST, "Intel Graphics" },
	{ 0x56BB, INTEL_ARC_FAMILY_ALCHEMIST, "Intel Graphics" },
	{ 0x56BC, INTEL_ARC_FAMILY_ALCHEMIST, "Intel Graphics" },
	{ 0x56BD, INTEL_ARC_FAMILY_ALCHEMIST, "Intel Graphics" },
	{ 0x56BE, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Graphics A750E" },
	{ 0x56BF, INTEL_ARC_FAMILY_ALCHEMIST, "Arc Graphics A580E" },
	{ 0x56C0, INTEL_ARC_FAMILY_ALCHEMIST, "Data Center GPU Flex 170" },
	{ 0x56C1, INTEL_ARC_FAMILY_ALCHEMIST, "Data Center GPU Flex 140" },

	{ 0xE202, INTEL_ARC_FAMILY_BATTLEMAGE, "Battlemage G21" },
	{ 0xE20B, INTEL_ARC_FAMILY_BATTLEMAGE, "Battlemage G21" },
	{ 0xE20C, INTEL_ARC_FAMILY_BATTLEMAGE, "Battlemage G21" },
	{ 0xE20D, INTEL_ARC_FAMILY_BATTLEMAGE, "Battlemage G21" },
	{ 0xE210, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage" },
	{ 0xE212, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Pro B50 / Battlemage" },
	{ 0xE215, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage" },
	{ 0xE216, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage" },
	{ 0xE220, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage G31" },
	{ 0xE221, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage G31" },
	{ 0xE222, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage G31" },
	{ 0xE223, INTEL_ARC_FAMILY_BATTLEMAGE, "Arc Battlemage G31" },
};


int32 api_version = B_CUR_DRIVER_API_VERSION;

static pci_module_info* gPCI;
static mutex gLock;
static char* gDeviceNames[MAX_DEVICES + 1];
static intel_arc_info* gDeviceInfo[MAX_DEVICES];

static void
load_settings(void)
{
    void* handle = load_driver_settings("intel_arc.settings");
    if (handle != NULL) {
        
        current_settings.hardcursor = get_driver_boolean_parameter(
            handle, "hardcursor", current_settings.hardcursor, current_settings.hardcursor);

        const char* value_str = get_driver_parameter(handle, "cursorbits", "32", "32"); //default HC bits on intel_arc
        if (value_str != nullptr) {
            current_settings.cursorbits = (uint32)atoi(value_str);
            dprintf("INTEL_ARC: driver settings read %d cursor bits\n",current_settings.cursorbits);
        }
        
        unload_driver_settings(handle);
    } else {
    	dprintf("INTEL_ARC: driver settings file not found\n");
    }
}

static inline uint32
get_pci_config(pci_info* info, uint8 offset, uint8 size)
{
	return gPCI->read_pci_config(info->bus, info->device, info->function,
		offset, size);
}


static inline void
set_pci_config(pci_info* info, uint8 offset, uint8 size, uint32 value)
{
	gPCI->write_pci_config(info->bus, info->device, info->function, offset,
		size, value);
}


static color_space
get_color_space_for_depth(uint32 depth)
{
	switch (depth) {
		case 1:
			return B_GRAY1;
		case 4:
			return B_GRAY8;
		case 8:
			return B_CMAP8;
		case 15:
			return B_RGB15;
		case 16:
			return B_RGB16;
		case 24:
			return B_RGB24;
		case 30:
			return B_RGB30;
		case 32:
			return B_RGB32;
		default:
			return B_NO_COLOR_SPACE;
	}
}


static bool
read32(const intel_arc_info& info, uint32 offset, uint32& value)
{
	if (info.registers == NULL
		|| offset + sizeof(uint32) > info.shared_info->registers_size) {
		return false;
	}

	value = *(volatile uint32*)(info.registers + offset);
	// Controllo protezione bus fault / D3cold power down State
	if (value == 0xFFFFFFFF)
		return false;

	return true;
}


static bool
write32(const intel_arc_info& info, uint32 offset, uint32 value)
{
	if (info.registers == NULL
		|| offset + sizeof(uint32) > info.shared_info->registers_size) {
		return false;
	}

	*(volatile uint32*)(info.registers + offset) = value;
	return true;
}


static int32
release_vblank_sem(intel_arc_info& info)
{
	int32 count = 0;
	if (info.shared_info->vblank_sem >= B_OK
		&& get_sem_count(info.shared_info->vblank_sem, &count) == B_OK
		&& count < 0) {
		release_sem_etc(info.shared_info->vblank_sem, -count,
			B_DO_NOT_RESCHEDULE);
		return B_INVOKE_SCHEDULER;
	}

	return B_HANDLED_INTERRUPT;
}


static void
enable_interrupts(intel_arc_info& info, bool enable)
{
	if (info.shared_info == NULL || info.shared_info->active_pipe < 0)
		return;

	const uint32 pipe = (uint32)info.shared_info->active_pipe + 1;
	const uint32 value = enable ? INTEL_ARC_PIPE_INT_VBLANK : 0;
	write32(info, INTEL_ARC_MMIO_PIPE_INT_IDENTITY(pipe), ~0U);
	write32(info, INTEL_ARC_MMIO_PIPE_INT_ENABLE(pipe), value);
	write32(info, INTEL_ARC_MMIO_PIPE_INT_MASK(pipe), ~value);

	if (enable) {
		write32(info, INTEL_ARC_MMIO_PCH_MASTER_INT_CTL,
			INTEL_ARC_MASTER_INT_GLOBAL);
	} else {
		write32(info, INTEL_ARC_MMIO_PCH_MASTER_INT_CTL, 0);
	}
}


static int32
arc_interrupt_handler(void* data)
{
	intel_arc_info& info = *(intel_arc_info*)data;
	if (info.shared_info == NULL || info.shared_info->active_pipe < 0)
		return B_UNHANDLED_INTERRUPT;

	uint32 interrupt = 0;
	if (!read32(info, INTEL_ARC_MMIO_PCH_MASTER_INT_CTL, interrupt))
		return B_UNHANDLED_INTERRUPT;
	if ((interrupt & INTEL_ARC_MASTER_INT_GLOBAL) == 0)
		return B_UNHANDLED_INTERRUPT;

	int32 handled = B_HANDLED_INTERRUPT;
	const uint32 pipe = (uint32)info.shared_info->active_pipe + 1;
	if ((interrupt & INTEL_ARC_MASTER_INT_PIPE_PENDING(pipe)) != 0) {
		uint32 identity = 0;
		if (read32(info, INTEL_ARC_MMIO_PIPE_INT_IDENTITY(pipe), identity)
			&& (identity & INTEL_ARC_PIPE_INT_VBLANK) != 0) {
			handled = release_vblank_sem(info);
			write32(info, INTEL_ARC_MMIO_PIPE_INT_IDENTITY(pipe),
				identity | INTEL_ARC_PIPE_INT_VBLANK);
		}
	}

	return handled;
}

/* orig
static void
probe_display_state(intel_arc_info& info)
{
	intel_arc_shared_info& shared = *info.shared_info;
	shared.pipe_count = 4;
	shared.active_pipe = -1;
	shared.active_ddi_port = 0;
	shared.active_ddi_mode = 0;
	shared.dpms_mode = B_DPMS_ON;

	for (uint32 pipe = 0; pipe < shared.pipe_count; pipe++) {
		const uint32 stride = pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
		read32(info, INTEL_ARC_MMIO_PIPE_A_HTOTAL + stride, shared.pipe_h_total[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_HBLANK + stride, shared.pipe_h_blank[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_HSYNC + stride, shared.pipe_h_sync[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VTOTAL + stride, shared.pipe_v_total[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VBLANK + stride, shared.pipe_v_blank[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VSYNC + stride, shared.pipe_v_sync[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_CONTROL + stride, shared.pipe_control[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_SIZE + stride, shared.pipe_size[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + stride, shared.pipe_ddi_func_ctl[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_CONTROL + stride, shared.plane_control[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_STRIDE + stride, shared.plane_stride[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_POS + stride, shared.plane_pos[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + stride, shared.plane_image_size[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_SURFACE + stride, shared.plane_surface[pipe]);

		if (shared.active_pipe >= 0)
			continue;

		if ((shared.pipe_control[pipe] & INTEL_ARC_PIPE_ENABLED) == 0)
			continue;
		if (shared.pipe_size[pipe] == 0)
			continue;

		shared.active_pipe = pipe;
		shared.active_ddi_port
			= (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_SELECT_MASK)
				>> INTEL_ARC_PIPE_DDI_SELECT_SHIFT;
		shared.active_ddi_mode
			= (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODE_MASK)
				>> INTEL_ARC_PIPE_DDI_MODE_SHIFT;

		const uint32 width = (shared.pipe_size[pipe] & 0xffff) + 1;
		const uint32 height = (shared.pipe_size[pipe] >> 16) + 1;
		if (width != 0 && height != 0) {
			shared.current_mode.virtual_width = width;
			shared.current_mode.virtual_height = height;
			if (shared.current_mode.space == B_NO_COLOR_SPACE)
				shared.current_mode.space = B_RGB32;
			if (shared.bytes_per_row == 0)
				shared.bytes_per_row = width * 4;
		}
	}

	static const uint32 kPortRegisters[4] = {
		INTEL_ARC_MMIO_PORT_A,
		INTEL_ARC_MMIO_PORT_B,
		INTEL_ARC_MMIO_PORT_C,
		INTEL_ARC_MMIO_PORT_D
	};

	shared.detected_port_bits = 0;
	for (uint32 port = 0; port < 4; port++) {
		if (!read32(info, kPortRegisters[port], shared.port_state[port]))
			continue;

		if ((shared.port_state[port] & (INTEL_ARC_PORT_ENABLED
				| INTEL_ARC_PORT_DETECTED)) != 0) {
			shared.detected_port_bits |= (1 << port);
		}
	}
}
*/
/* orig con logs */
static void
probe_display_state(intel_arc_info& info)
{
	// 1. LOG INIZIALIZZAZIONE
    dprintf("DEBUG: Entering probe_display_state.\n");

	intel_arc_shared_info& shared = *info.shared_info;
	shared.pipe_count = 4;
	shared.active_pipe = -1;
	shared.active_ddi_port = 0;
	shared.active_ddi_mode = 0;
	shared.dpms_mode = B_DPMS_ON;

    // Reset e log delle variabili chiave prima del ciclo
    dprintf("DEBUG: Initializing shared info fields.\n");


	for (uint32 pipe = 0; pipe < shared.pipe_count; pipe++) {
        // LOG INIZIO PIPE LOOP
		dprintf("DEBUG: Processing Pipe %u\n", pipe);

		const uint32 stride = pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
        
        // 2. LEGGERE I REGISTRI E LOGGARLI
        read32(info, INTEL_ARC_MMIO_PIPE_A_HTOTAL + stride, shared.pipe_h_total[pipe]);
		dprintf("DEBUG: Pipe %u HTotal read: 0x%X\n", pipe, shared.pipe_h_total[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_HBLANK + stride, shared.pipe_h_blank[pipe]);
		dprintf("DEBUG: Pipe %u HBlank read: 0x%X\n", pipe, shared.pipe_h_blank[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_HSYNC + stride, shared.pipe_h_sync[pipe]);
		dprintf("DEBUG: Pipe %u HSync read: 0x%X\n", pipe, shared.pipe_h_sync[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VTOTAL + stride, shared.pipe_v_total[pipe]);
		dprintf("DEBUG: Pipe %u VTotal read: 0x%X\n", pipe, shared.pipe_v_total[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VBLANK + stride, shared.pipe_v_blank[pipe]);
		dprintf("DEBUG: Pipe %u VBlank read: 0x%X\n", pipe, shared.pipe_v_blank[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VSYNC + stride, shared.pipe_v_sync[pipe]);
		dprintf("DEBUG: Pipe %u VSync read: 0x%X\n", pipe, shared.pipe_v_sync[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_CONTROL + stride, shared.pipe_control[pipe]);
		dprintf("DEBUG: Pipe %u Control read: 0x%X (Enabled: %d)\n", pipe, shared.pipe_control[pipe], (shared.pipe_control[pipe] & INTEL_ARC_PIPE_ENABLED) != 0);

        read32(info, INTEL_ARC_MMIO_PIPE_A_SIZE + stride, shared.pipe_size[pipe]);
		dprintf("DEBUG: Pipe %u Size read: 0x%X\n", pipe, shared.pipe_size[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + stride, shared.pipe_ddi_func_ctl[pipe]);
		dprintf("DEBUG: Pipe %u DDI FuncCtl read: 0x%X\n", pipe, shared.pipe_ddi_func_ctl[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_CONTROL + stride, shared.plane_control[pipe]);
		dprintf("DEBUG: Pipe %u Plane Control read: 0x%X\n", pipe, shared.plane_control[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_STRIDE + stride, shared.plane_stride[pipe]);
		dprintf("DEBUG: Pipe %u Plane Stride read: 0x%X\n", pipe, shared.plane_stride[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_POS + stride, shared.plane_pos[pipe]);
		dprintf("DEBUG: Pipe %u Plane Pos read: 0x%X\n", pipe, shared.plane_pos[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + stride, shared.plane_image_size[pipe]);
		dprintf("DEBUG: Pipe %u Image Size read: 0x%X\n", pipe, shared.plane_image_size[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_SURFACE + stride, shared.plane_surface[pipe]);
		dprintf("DEBUG: Pipe %u Surface read: 0x%X\n", pipe, shared.plane_surface[pipe]);

        // LOGIC CHECKING START
		if (shared.active_pipe >= 0) {
            dprintf("DEBUG: Skipping Pipe %u because an active pipe was already found.\n", pipe);
			continue;
		}

		if ((shared.pipe_control[pipe] & INTEL_ARC_PIPE_ENABLED) == 0) {
            dprintf("DEBUG: Skipping Pipe %u because the PIPE_ENABLED bit is clear (Control: 0x%X).\n", pipe, shared.pipe_control[pipe]);
			continue;
		}

		if (shared.pipe_size[pipe] == 0) {
            dprintf("DEBUG: Skipping Pipe %u because pipe size is zero (Size: 0x%X).\n", pipe, shared.pipe_size[pipe]);
			continue;
		}

        // LOGIC SUCCESS PATH
		shared.active_pipe = pipe;
		shared.active_ddi_port
			= (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_SELECT_MASK)
				>> INTEL_ARC_PIPE_DDI_SELECT_SHIFT;
		shared.active_ddi_mode
			= (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODE_MASK)
				>> INTEL_ARC_PIPE_DDI_MODE_SHIFT;

        dprintf("DEBUG: Pipe %u identified as ACTIVE PIPE.\n", pipe);
		dprintf("DEBUG: Active DDI Port detected: %u\n", shared.active_ddi_port);
		dprintf("DEBUG: Active DDI Mode detected: %u\n", shared.active_ddi_mode);

        // Calcolo e Log risoluzione
        // 1. Estrazione corretta secondo il layout hardware Intel (Display IP 13/14)
        // Height si trova nei 16 bit bassi [15:0], Width nei 16 bit alti [31:16]
        const uint32 height = (shared.pipe_size[pipe] & 0xffff) + 1;
        const uint32 width = ((shared.pipe_size[pipe] >> 16) & 0xffff) + 1;
		if (width != 0 && height != 0) {
			shared.current_mode.virtual_width = width;
			shared.current_mode.virtual_height = height;
			dprintf("DEBUG: Calculated Resolution for Pipe %u: %u x %u\n", pipe, width, height);

            if (shared.current_mode.space == B_NO_COLOR_SPACE) {
                shared.current_mode.space = B_RGB32;
                dprintf("DEBUG: Color space corrected to RGB32.\n");
            }
			if (shared.bytes_per_row == 0) {
				shared.bytes_per_row = width * 4;
                dprintf("DEBUG: Bytes per row set to %u (Width * 4).\n", shared.bytes_per_row);
			}
		} else {
            dprintf("WARNING: Pipe %u failed resolution check (W=%u, H=%u).\n", pipe, width, height);
        }
        	// Estrazione lane dallo stato hardware al boot
        	// Verificare da manuali se il lane width è ai bit 3:1 o 21:19
		const uint32 ddiWidth = (shared.pipe_ddi_func_ctl[pipe] >> 1) & 0x7; // bit 3:1
		//const uint32 ddiWidth = (shared.pipe_ddi_func_ctl[pipe] >> 19) & 0x7;

		if (ddiWidth == 0) {
			dprintf("DEBUG: Display Port Lanes set to 1\n");
			shared.dp_lanes[pipe] = 1;
		} else if (ddiWidth == 1) {
			dprintf("DEBUG: Display Port Lanes set to 2\n");
			shared.dp_lanes[pipe] = 2;
		} else if (ddiWidth == 3) {
			dprintf("DEBUG: Display Port Lanes set to 4\n");
			shared.dp_lanes[pipe] = 4;
		} else {
			dprintf("DEBUG: dp_lanes set to fallback (2)\n");
			shared.dp_lanes[pipe] = 2;
		}
	} // FINE PIPE LOOP

    // PORT LOGIC START
    dprintf("\nDEBUG: Starting Port Detection Loop.\n");

	static const uint32 kPortRegisters[4] = {
		INTEL_ARC_MMIO_PORT_A,
		INTEL_ARC_MMIO_PORT_B,
		INTEL_ARC_MMIO_PORT_C,
		INTEL_ARC_MMIO_PORT_D
	};

	shared.detected_port_bits = 0;
	for (uint32 port = 0; port < 4; port++) {
        // LOG INIZIO PORT LOOP
        dprintf("DEBUG: Checking Port %u...\n", port);

		if (!read32(info, kPortRegisters[port], shared.port_state[port])) {
            dprintf("WARNING: Failed to read state for Port %u.\n", port);
			continue;
		}
        
        // LOG STADO REGISTRO PORTA
        dprintf("DEBUG: State register for Port %u: 0x%X\n", port, shared.port_state[port]);


		if ((shared.port_state[port] & (INTEL_ARC_PORT_ENABLED | INTEL_ARC_PORT_DETECTED)) != 0) {
            dprintf("SUCCESS: Port %u is detected and enabled.\n", port);
			shared.detected_port_bits |= (1 << port);
		} else {
            dprintf("INFO: Port %u is inactive or undetected.\n", port);
        }
	} // FINE PORT LOOP

    dprintf("\nDEBUG: probe_display_state finished execution.\n");
}
/* gestione colore 
static void
probe_display_state(intel_arc_info& info)
{
    // 1. LOG INIZIALIZZAZIONE
    dprintf("DEBUG: Entering probe_display_state.\n");

    intel_arc_shared_info& shared = *info.shared_info;
    shared.pipe_count = 4;
    shared.active_pipe = -1;
    shared.active_ddi_port = 0;
    shared.active_ddi_mode = 0;
    shared.dpms_mode = B_DPMS_ON;

    // Reset e log delle variabili chiave prima del ciclo
    dprintf("DEBUG: Initializing shared info fields.\n");

    for (uint32 pipe = 0; pipe < shared.pipe_count; pipe++) {
        // LOG INIZIO PIPE LOOP
        dprintf("DEBUG: Processing Pipe %u\n", pipe);

        const uint32 stride = pipe * INTEL_ARC_MMIO_PIPE_OFFSET;

        // 2. LEGGERE I REGISTRI E LOGGARLI
        read32(info, INTEL_ARC_MMIO_PIPE_A_HTOTAL + stride, shared.pipe_h_total[pipe]);
        dprintf("DEBUG: Pipe %u HTotal read: 0x%X\n", pipe, shared.pipe_h_total[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_HBLANK + stride, shared.pipe_h_blank[pipe]);
        dprintf("DEBUG: Pipe %u HBlank read: 0x%X\n", pipe, shared.pipe_h_blank[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_HSYNC + stride, shared.pipe_h_sync[pipe]);
        dprintf("DEBUG: Pipe %u HSync read: 0x%X\n", pipe, shared.pipe_h_sync[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VTOTAL + stride, shared.pipe_v_total[pipe]);
        dprintf("DEBUG: Pipe %u VTotal read: 0x%X\n", pipe, shared.pipe_v_total[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VBLANK + stride, shared.pipe_v_blank[pipe]);
        dprintf("DEBUG: Pipe %u VBlank read: 0x%X\n", pipe, shared.pipe_v_blank[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_VSYNC + stride, shared.pipe_v_sync[pipe]);
        dprintf("DEBUG: Pipe %u VSync read: 0x%X\n", pipe, shared.pipe_v_sync[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_CONTROL + stride, shared.pipe_control[pipe]);
        dprintf("DEBUG: Pipe %u Control read: 0x%X (Enabled: %d)\n", pipe,
            shared.pipe_control[pipe], (shared.pipe_control[pipe] & INTEL_ARC_PIPE_ENABLED) != 0);

        read32(info, INTEL_ARC_MMIO_PIPE_A_SIZE + stride, shared.pipe_size[pipe]);
        dprintf("DEBUG: Pipe %u Size read: 0x%X\n", pipe, shared.pipe_size[pipe]);

        read32(info, INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + stride, shared.pipe_ddi_func_ctl[pipe]);
        dprintf("DEBUG: Pipe %u DDI FuncCtl read: 0x%X\n", pipe, shared.pipe_ddi_func_ctl[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_CONTROL + stride, shared.plane_control[pipe]);
        dprintf("DEBUG: Pipe %u Plane Control read: 0x%X\n", pipe, shared.plane_control[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_STRIDE + stride, shared.plane_stride[pipe]);
        dprintf("DEBUG: Pipe %u Plane Stride read: 0x%X\n", pipe, shared.plane_stride[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_POS + stride, shared.plane_pos[pipe]);
        dprintf("DEBUG: Pipe %u Plane Pos read: 0x%X\n", pipe, shared.plane_pos[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + stride, shared.plane_image_size[pipe]);
        dprintf("DEBUG: Pipe %u Image Size read: 0x%X\n", pipe, shared.plane_image_size[pipe]);

        read32(info, INTEL_ARC_MMIO_PLANE_A_SURFACE + stride, shared.plane_surface[pipe]);
        dprintf("DEBUG: Pipe %u Surface read: 0x%X\n", pipe, shared.plane_surface[pipe]);
        
        read32(info, INTEL_ARC_MMIO_PLANE_A_COLOR_CTL + stride, shared.plane_surface[pipe]);
        dprintf("DEBUG: Pipe %u Plane A Color control read: 0x%X\n", pipe, shared.plane_surface[pipe]);

        // LOGIC CHECKING START
        if (shared.active_pipe >= 0) {
            dprintf("DEBUG: Skipping Pipe %u because an active pipe was already found.\n", pipe);
            continue;
        }

        if ((shared.pipe_control[pipe] & INTEL_ARC_PIPE_ENABLED) == 0) {
            dprintf("DEBUG: Skipping Pipe %u because the PIPE_ENABLED bit is clear (Control: 0x%X).\n",
                pipe, shared.pipe_control[pipe]);
            continue;
        }

        if (shared.pipe_size[pipe] == 0) {
            dprintf("DEBUG: Skipping Pipe %u because pipe size is zero (Size: 0x%X).\n",
                pipe, shared.pipe_size[pipe]);
            continue;
        }

        // LOGIC SUCCESS PATH
        shared.active_pipe = pipe;
        shared.active_ddi_port
            = (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_SELECT_MASK)
                >> INTEL_ARC_PIPE_DDI_SELECT_SHIFT;
        shared.active_ddi_mode
            = (shared.pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODE_MASK)
                >> INTEL_ARC_PIPE_DDI_MODE_SHIFT;

        dprintf("DEBUG: Pipe %u identified as ACTIVE PIPE.\n", pipe);
        dprintf("DEBUG: Active DDI Port detected: %u\n", shared.active_ddi_port);
        dprintf("DEBUG: Active DDI Mode detected: %u\n", shared.active_ddi_mode);

        // Calcolo e Log risoluzione (Layout hardware Intel Xe: Height [15:0], Width [31:16])
        const uint32 height = (shared.pipe_size[pipe] & 0xffff) + 1;
        const uint32 width = ((shared.pipe_size[pipe] >> 16) & 0xffff) + 1;

        if (width != 0 && height != 0) {
            shared.current_mode.virtual_width = width;
            shared.current_mode.virtual_height = height;
            
            const uint32 hTotal = shared.pipe_h_total[pipe];
		    const uint32 hSync = shared.pipe_h_sync[pipe];
			const uint32 vTotal = shared.pipe_v_total[pipe];
			const uint32 vSync = shared.pipe_v_sync[pipe];

			if (hTotal != 0 && vTotal != 0) {
				shared.current_mode.timing.h_display = (hTotal & 0xffff) + 1;
				shared.current_mode.timing.h_total = (hTotal >> 16) + 1;
				shared.current_mode.timing.h_sync_start = (hSync & 0xffff) + 1;
				shared.current_mode.timing.h_sync_end = (hSync >> 16) + 1;
				shared.current_mode.timing.v_display = (vTotal & 0xffff) + 1;
				shared.current_mode.timing.v_total = (vTotal >> 16) + 1;
				shared.current_mode.timing.v_sync_start = (vSync & 0xffff) + 1;
				shared.current_mode.timing.v_sync_end = (vSync >> 16) + 1;
			}
			if (shared.current_mode.timing.pixel_clock == 0) {
				shared.current_mode.timing.pixel_clock = ((uint32)shared.current_mode.timing.h_total
					* (uint32)shared.current_mode.timing.v_total * 60) / 1000;
			}
            dprintf("DEBUG: Calculated Resolution for Pipe %u: %u x %u\n", pipe, width, height);

            // Decodifica dello Spazio Colore da PLANE_CTL (bit 27:24)
            const uint32 planeCtl = shared.plane_control[pipe];
            const uint32 pixelFormat = (planeCtl >> 24) & 0x0F;
            color_space hwColorSpace = B_NO_COLOR_SPACE;

            switch (pixelFormat) {
                case 0x4:
                case 0x6:
                    hwColorSpace = B_RGB32;
                    break;
                case 0x8:
                    hwColorSpace = B_RGB32_BIG;
                    break;
                case 0xC:
                    hwColorSpace = B_RGBA32;
                    break;
                default:
                    break;
            }

            if (hwColorSpace != B_NO_COLOR_SPACE) {
                shared.current_mode.space = hwColorSpace;
                dprintf("DEBUG: Color space read from PLANE_CTL (0x%X): %d\n", pixelFormat, hwColorSpace);
            } else if (shared.current_mode.space == B_NO_COLOR_SPACE) {
                shared.current_mode.space = B_RGB32;
                dprintf("DEBUG: Color space fallback set to RGB32.\n");
            }

            // Calcolo/Verifica Stride (Bytes Per Row) da PLANE_STRIDE
            const uint32 planeStrideVal = shared.plane_stride[pipe];
            if (planeStrideVal > 0) {
                // Su Intel Xe, PLANE_STRIDE è in unità di 64 byte (tile stride)
                shared.bytes_per_row = planeStrideVal * 64;
                dprintf("DEBUG: Bytes per row read from PLANE_STRIDE: %u\n", shared.bytes_per_row);
            } else if (shared.bytes_per_row == 0) {
                shared.bytes_per_row = width * 4;
                dprintf("DEBUG: Bytes per row fallback set to %u (Width * 4).\n", shared.bytes_per_row);
            }
        } else {
            dprintf("WARNING: Pipe %u failed resolution check (W=%u, H=%u).\n", pipe, width, height);
        }
    } // FINE PIPE LOOP

    // PORT LOGIC START
    dprintf("\nDEBUG: Starting Port Detection Loop.\n");

    static const uint32 kPortRegisters[4] = {
        INTEL_ARC_MMIO_PORT_A,
        INTEL_ARC_MMIO_PORT_B,
        INTEL_ARC_MMIO_PORT_C,
        INTEL_ARC_MMIO_PORT_D
    };

    shared.detected_port_bits = 0;
    for (uint32 port = 0; port < 4; port++) {
        dprintf("DEBUG: Checking Port %u...\n", port);

        if (!read32(info, kPortRegisters[port], shared.port_state[port])) {
            dprintf("WARNING: Failed to read state for Port %u.\n", port);
            continue;
        }

        dprintf("DEBUG: State register for Port %u: 0x%X\n", port, shared.port_state[port]);

        if ((shared.port_state[port] & (INTEL_ARC_PORT_ENABLED | INTEL_ARC_PORT_DETECTED)) != 0) {
            dprintf("SUCCESS: Port %u is detected and enabled.\n", port);
            shared.detected_port_bits |= (1 << port);
        } else {
            dprintf("INFO: Port %u is inactive or undetected.\n", port);
        }
    } // FINE PORT LOOP

    dprintf("\nDEBUG: probe_display_state finished execution.\n");
}*/


static status_t
get_next_supported_device(int32* cookie, pci_info& info,
	const supported_device** _device)
{
	for (int32 index = *cookie; gPCI->get_nth_pci_info(index, &info) == B_OK;
			index++) {
		if (info.vendor_id != INTEL_ARC_VENDOR_ID
			|| info.class_base != PCI_display
			|| (info.class_sub != PCI_vga
				&& info.class_sub != PCI_display_other)) {
			continue;
		}

		for (size_t i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (info.device_id == kSupportedDevices[i].device_id) {
				*_device = &kSupportedDevices[i];
				*cookie = index + 1;
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


static bool
get_bar_info(const pci_info& info, int32 index, pci_bar_info& bar)
{
	dprintf("DEBUG: Starting get_bar_info() for BAR %d\n", index);
	
	memset(&bar, 0, sizeof(bar));
	bar.index = index;
	bar.consumed = 1;

	if (index < 0 || index >= 6) {
		dprintf("DEBUG: Invalid BAR index %d (out of range [0-5])\n", index);
		return false;
	}

	bar.flags = info.u.h0.base_register_flags[index];
	bar.size = info.u.h0.base_register_sizes[index];
	dprintf("DEBUG: BAR %d - Flags: 0x%X, Size: 0x%lX\n", index, bar.flags, bar.size);
	 
	if ((bar.flags & PCI_address_space) != 0 || bar.size == 0) {
		dprintf("DEBUG: BAR %d skipped - Invalid flags (not addressable) or Size=0\n", index);
		return false;
	}

	bar.base = info.u.h0.base_registers[index];
	dprintf("DEBUG: BAR %d - Base Address (Low): 0x%lX\n", index, bar.base);

	if ((bar.flags & PCI_address_type) == PCI_address_type_64 && index < 5) {
		dprintf("DEBUG: BAR %d - Using 64-bit addressing (combining registers %d and %d)\n", index, index, index + 1);
		
		bar.base |= (uint64)info.u.h0.base_registers[index + 1] << 32;
		dprintf("DEBUG: BAR %d - Base Address (High): 0x%X\n", index, info.u.h0.base_registers[index + 1]);
		dprintf("DEBUG: BAR %d - Combined Base Address: 0x%lX\n",
				index, bar.base);
		
		bar.size |= (uint64)info.u.h0.base_register_sizes[index + 1] << 32;
		dprintf("DEBUG: BAR %d - Size (High): 0x%X\n", index, info.u.h0.base_register_sizes[index + 1]);
		dprintf("DEBUG: BAR %d - Combined Size: 0x%lX\n", index, bar.size);
		bar.consumed = 2;
	}

	//return bar.base != 0 && bar.size != 0;
	if (bar.base == 0 || bar.size == 0) {
        dprintf("DEBUG: BAR %d invalid - Base=0 or Size=0\n", index);
        return false;
    }

    dprintf("DEBUG: get_bar_info() completed for BAR %d - Success!\n", index);
    dprintf("DEBUG: Final BAR Info - Index:%d, Flags:0x%X, Base:0x%lX, Size:0x%lX\n",
            bar.index,
            bar.flags,
            bar.base,
            bar.size);

    return true;
}


static bool
select_bars(const pci_info& info, pci_bar_info& mmioBar,
	pci_bar_info& frameBufferBar)
{
	bool foundMMIO = false;
	bool foundFrameBuffer = false;
	uint64 bestFrameBufferSize = 0;

	memset(&mmioBar, 0, sizeof(mmioBar));
	memset(&frameBufferBar, 0, sizeof(frameBufferBar));

	for (int32 index = 0; index < 6;) {
		pci_bar_info bar;
		bool valid = get_bar_info(info, index, bar);
		index += bar.consumed;
		if (!valid)
			continue;

		const bool prefetchable
			= (bar.flags & PCI_address_prefetchable) != 0;

		if (!prefetchable && !foundMMIO) {
			mmioBar = bar;
			foundMMIO = true;
			continue;
		}

		if (prefetchable && bar.size >= bestFrameBufferSize) {
			frameBufferBar = bar;
			bestFrameBufferSize = bar.size;
			foundFrameBuffer = true;
			continue;
		}

		if (!foundMMIO) {
			mmioBar = bar;
			foundMMIO = true;
		}
	}

	if (!foundMMIO)
		return false;

	if (foundFrameBuffer && frameBufferBar.index == mmioBar.index) {
		foundFrameBuffer = false;
		memset(&frameBufferBar, 0, sizeof(frameBufferBar));
	}

	return foundMMIO;
}

#ifdef IS_PIRATI_BUILD
static void
draw_logo(intel_arc_info& info)
{
    if (info.shared_info == NULL)
        return;

    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(
        FRAME_BUFFER_BOOT_INFO, NULL);

    if (bi == NULL || bi->depth != 32 || bi->physical_frame_buffer == 0)
        return;

    uint32 screenWidth = bi->width;
    uint32 screenHeight = bi->height;

    uint32 bytesPerRow = bi->bytes_per_row;
    if (bytesPerRow == 0)
        bytesPerRow = screenWidth * 4;

    uint32 logoW = kBitmapWidth;   // 960
    uint32 logoH = kBitmapHeight;  // 523
    
    if (logoW > screenWidth || logoH > screenHeight) 
        return;

    int32 startX = (screenWidth - logoW) / 2;
    if (startX < 0) startX = 0;
    int32 startY = (screenHeight - logoH) / 2;
    if (startY < 0) startY = 0;

    // --- MAPPATURA KERNEL DEDICATA ---
    void* kFbPtr = NULL;
    size_t fbSize = bytesPerRow * screenHeight;
    area_id kFbArea = map_physical_memory(
        "kernel_draw_logo_fb",
        bi->physical_frame_buffer,
        fbSize,
        B_ANY_KERNEL_ADDRESS,
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
        &kFbPtr
    );

    if (kFbArea < B_OK || kFbPtr == NULL)
        return;

    uint8* fb = (uint8*)kFbPtr;
    const uint8* logoBits = kintel_arc_logo_Bits;

    for (uint32 y = 0; y < logoH && (startY + y) < screenHeight; y++) {
        uint32 fbOffset = (startY + y) * bytesPerRow + startX * sizeof(uint32);
        uint32* dst = (uint32*)(fb + fbOffset);

        uint32 remainingWidth = screenWidth - startX;
        uint32 copyPixels = (logoW < remainingWidth) ? logoW : remainingWidth;
        uint32 logoRowOffset = y * logoW * 4;

        for (uint32 x = 0; x < copyPixels; x++) {
            uint32 pxIndex = logoRowOffset + (x * 4);
            uint8 b = logoBits[pxIndex + 0];
            uint8 g = logoBits[pxIndex + 1];
            uint8 r = logoBits[pxIndex + 2];

            dst[x] = (255 << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // Smonta l'area temporanea Kernel
    delete_area(kFbArea);
}
#endif

static status_t
init_device(intel_arc_info& info)
{
	if (info.shared_area >= B_OK)
		return B_OK;

	AreaKeeper sharedKeeper;
	info.shared_area = sharedKeeper.Create("intel arc shared info",
		(void**)&info.shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(intel_arc_shared_info)), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info.shared_area < B_OK)
		return info.shared_area;

	memset(info.shared_info, 0, sizeof(intel_arc_shared_info));
	load_settings();
	//memcpy(&info.shared_info->settings, &current_settings, sizeof(intel_arc_settings));
	info.shared_info->settings = current_settings;
    info.shared_info->bDisableHdwCursor = !info.shared_info->settings.hardcursor;
    
	info.shared_info->vblank_sem = -1;
	info.shared_info->vendor_id = info.pci.vendor_id;
	info.shared_info->device_id = info.pci.device_id;
	info.shared_info->family = info.device->family;
	info.shared_info->revision = info.pci.revision;
	info.shared_info->subsystem_vendor_id = info.pci.u.h0.subsystem_vendor_id;
	info.shared_info->subsystem_id = info.pci.u.h0.subsystem_id;
	snprintf(info.shared_info->device_identifier,
		sizeof(info.shared_info->device_identifier), "%s", info.device->name);

	set_pci_config(&info.pci, PCI_command, 2,
		get_pci_config(&info.pci, PCI_command, 2)
			| PCI_command_memory | PCI_command_master);

	pci_bar_info mmioBar;
	pci_bar_info frameBufferBar;
	if (!select_bars(info.pci, mmioBar, frameBufferBar)) {
		info.shared_area = -1;
		info.shared_info = NULL;
		return B_ERROR;
	}

	AreaKeeper mmioKeeper;
	info.registers_area = mmioKeeper.Map("intel arc mmio", mmioBar.base,
		mmioBar.size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&info.registers);
	if (info.registers_area < B_OK) {
		info.shared_area = -1;
		info.shared_info = NULL;
		return info.registers_area;
	}

	info.shared_info->registers_area = info.registers_area;
	info.shared_info->registers_base = mmioBar.base;
	info.shared_info->registers_size = mmioBar.size;
	info.shared_info->mmio_bar = mmioBar.index;

	info.frame_buffer_area = -1;
	info.frame_buffer = NULL;
	if (frameBufferBar.base != 0 && frameBufferBar.size != 0) {
		size_t mapSize = frameBufferBar.size > MAX_CLONED_FRAMEBUFFER_SIZE
			? MAX_CLONED_FRAMEBUFFER_SIZE : (size_t)frameBufferBar.size;

		phys_addr_t bootFbPhys = frameBufferBar.base;
		phys_addr_t fbOffset = 0;

		frame_buffer_boot_info* bootInfo
			= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);

		if (bootInfo != NULL
			&& (phys_addr_t)bootInfo->physical_frame_buffer >= frameBufferBar.base
			&& (phys_addr_t)bootInfo->physical_frame_buffer < frameBufferBar.base
				+ frameBufferBar.size) {

			bootFbPhys = (phys_addr_t)bootInfo->physical_frame_buffer;
			fbOffset = bootFbPhys - frameBufferBar.base;

			size_t bootSurfaceSize = (size_t)bootInfo->bytes_per_row
				* (size_t)bootInfo->height;

			// Assicuriamo che mapSize copra almeno la superficie di boot, ma non
			// riduciamo la mappatura al solo GOP framebuffer: il preflet può
			// selezionare modalità con framebuffer più grande e una clone area
			// troppo piccola causa page fault lato app_server.
			if (bootSurfaceSize > 0) {
				size_t bootMapSize = ROUND_TO_PAGE_SIZE(fbOffset + bootSurfaceSize);
				if (bootMapSize > mapSize)
					mapSize = min_c(bootMapSize, (size_t)MAX_CLONED_FRAMEBUFFER_SIZE);
			}

			info.shared_info->current_mode.virtual_width = bootInfo->width;
			info.shared_info->current_mode.virtual_height = bootInfo->height;
			info.shared_info->current_mode.space
				= get_color_space_for_depth(bootInfo->depth);
			info.shared_info->bytes_per_row = bootInfo->bytes_per_row;
			dprintf("Arc Driver: app_server pitch = %" B_PRIu32 ", calculated pitch = %" B_PRIu32 "\n",
    			info.shared_info->bytes_per_row, (info.shared_info->current_mode.virtual_width * 4 + 63) & ~63); //boot in B_RGB32
		}

		// Tentativo 1: Mappatura con Write-Combining (stile S3)
		info.frame_buffer_area = map_physical_memory("intel arc framebuffer",
			frameBufferBar.base, mapSize,
			B_ANY_KERNEL_BLOCK_ADDRESS | B_WRITE_COMBINING_MEMORY,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA
				| B_CLONEABLE_AREA,
			(void**)&info.frame_buffer);

		if (info.frame_buffer_area < 0) {
			// Tentativo 2: Fallback senza Write Combining
			info.frame_buffer_area = map_physical_memory("intel arc framebuffer",
				frameBufferBar.base, mapSize, B_ANY_KERNEL_BLOCK_ADDRESS,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA
					| B_CLONEABLE_AREA,
				(void**)&info.frame_buffer);
		}

		TRACE("Video memory, area: %" B_PRId32 ", base phys: 0x%" B_PRIxPHYSADDR
			", fb offset: 0x%" B_PRIxPHYSADDR "\n",
			info.frame_buffer_area, frameBufferBar.base, fbOffset);

		if (info.frame_buffer_area >= B_OK) {
			info.shared_info->frame_buffer_area = info.frame_buffer_area;
			info.shared_info->frame_buffer_base = frameBufferBar.base;
			info.shared_info->frame_buffer_size = mapSize;
			info.shared_info->frame_buffer_offset = fbOffset; // Salva l'offset fisico

			// L'indirizzo virtuale del primo pixel visibile considera l'offset EFI/GOP
			info.shared_info->frame_buffer = (addr_t)info.frame_buffer + fbOffset;
			info.shared_info->frame_buffer_bar = frameBufferBar.index;
		} else {
			ERROR("could not map framebuffer BAR for %s: %s\n",
				info.device->name, strerror(info.frame_buffer_area));
			info.frame_buffer_area = -1;
		}
	}

	edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (edidInfo != NULL) {
		info.shared_info->has_boot_edid = true;
		memcpy(&info.shared_info->boot_edid, edidInfo, sizeof(edid1_info));
	}

	probe_display_state(info);

	info.shared_info->vblank_sem = create_sem(0, "intel arc vblank");
	if (info.shared_info->vblank_sem >= B_OK) {
		thread_id thread = find_thread(NULL);
		thread_info threadInfo;
		if (get_thread_info(thread, &threadInfo) != B_OK
			|| set_sem_owner(info.shared_info->vblank_sem, threadInfo.team) != B_OK) {
			delete_sem(info.shared_info->vblank_sem);
			info.shared_info->vblank_sem = -1;
		}
	}

	info.irq = 0;
	info.irq_installed = false;
	if (info.pci.u.h0.interrupt_pin != 0x00) {
		info.irq = info.pci.u.h0.interrupt_line;
		if (info.irq == 0xff)
			info.irq = 0;
	}
	if (info.irq != 0 && info.shared_info->vblank_sem >= B_OK
		&& install_io_interrupt_handler(info.irq, arc_interrupt_handler, &info, 0) == B_OK) {
		info.irq_installed = true;
		enable_interrupts(info, true);
	}

	
	struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(
FRAME_BUFFER_BOOT_INFO, NULL);

	if (bi) {
		info.shared_info->has_boot_info = true;
		info.shared_info->boot_width = bi->width;
		info.shared_info->boot_height = bi->height;
		info.shared_info->boot_depth = bi->depth;
	} else {
		info.shared_info->has_boot_info = false;
	}
	info.shared_info->fbc.frame_buffer = (void*)info.shared_info->frame_buffer;
    info.shared_info->fbc.frame_buffer_dma = (void *)(info.shared_info->frame_buffer_base 
    + info.shared_info->frame_buffer_offset);
    info.shared_info->fbc.bytes_per_row = info.shared_info->bytes_per_row;
	info.shared_info->accelerant_in_use=false;
	
	if (!info.shared_info->bDisableHdwCursor) {
        AreaKeeper cursorKeeper;
        size_t cursorSize = B_PAGE_SIZE * 4; // 16 KB

        info.cursor_area = cursorKeeper.Create("intel arc cursor buffer",
            &info.shared_info->cursor_virtual_base_kernel,
            B_ANY_KERNEL_ADDRESS,
            cursorSize,
            B_FULL_LOCK | B_CONTIGUOUS,
            B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);

        if (info.cursor_area >= B_OK) {
            physical_entry pe;
            status_t status = get_memory_map(
                info.shared_info->cursor_virtual_base_kernel,
                cursorSize, &pe, 1);

            if (status == B_OK) {
                info.shared_info->cursor_area = info.cursor_area;
                info.shared_info->cursor_physical_base = (uint32)pe.address;
                cursorKeeper.Detach();

                dprintf("intel_arc: Cursor buffer allocated - Phys: 0x%" B_PRIx32
                    ", Virt Kernel: %p\n",
                    info.shared_info->cursor_physical_base,
                    info.shared_info->cursor_virtual_base_kernel);
            } else {
                ERROR("intel_arc: Failed to get memory map for cursor buffer: %s\n",
                    strerror(status));
                info.shared_info->bDisableHdwCursor = true;
            }
        } else {
            ERROR("intel_arc: Failed to allocate contiguous cursor area: %s\n",
                strerror(info.cursor_area));
            info.shared_info->bDisableHdwCursor = true;
        }
    }
    // Rilevamento diagnostico VRAM fisica totale (LMEM)
    // -------------------------------------------------------------------------
    info.shared_info->vram_size = frameBufferBar.size;

    // Se ReBAR è disattivato, la BAR2 è bloccata a 256 MB.
    // Interroghiamo i registri MMIO di Intel Arc (DG2) per leggere la VRAM reale saldata sulla scheda.
    if (info.shared_info->vram_size <= 256 * 1024 * 1024 && info.registers != NULL) {
        // Su Intel Arc (DG2/Xe-HPG), il registro MMIO 0x138000 / 0x100000 
        // contiene la configurazione dei controller di memoria LMEM.
        dprintf("intel_arc: ReBAR disabled detected\n");
        uint32 lmemCap = info.registers[0x138000 / 4];

        if (lmemCap != 0 && lmemCap != 0xFFFFFFFF) {
            // Estragga la dimensione della VRAM in base ai blocchi LMEM attivi
            // Esempio tipico su DG2: shift dei blocchi da 1 GB / 2 GB
            uint64 detectedBytes = ((uint64)(lmemCap & 0xFFFF)) * 1024 * 1024 * 1024;
            if (detectedBytes > info.shared_info->vram_size)
                info.shared_info->vram_size = detectedBytes;
        }
    } else {
    	dprintf("intel_arc: ReBAR active!\n");
    }

    dprintf("intel_arc: Physical VRAM: %" B_PRIu64 " MB | Mapped Framebuffer: %" B_PRIu64 " MB\n",
        info.shared_info->vram_size / (1024 * 1024),
        info.shared_info->frame_buffer_size / (1024 * 1024));
	
#ifdef IS_PIRATI_BUILD
	draw_logo(info);
	snooze(2000000);
#endif

	sharedKeeper.Detach();
	mmioKeeper.Detach();
	


	return B_OK;
}


static void
uninit_device(intel_arc_info& info)
{
	if (info.irq_installed) {
		enable_interrupts(info, false);
		remove_io_interrupt_handler(info.irq, arc_interrupt_handler, &info);
		info.irq_installed = false;
	}

	if (info.shared_info != NULL && info.shared_info->vblank_sem >= B_OK) {
		delete_sem(info.shared_info->vblank_sem);
		info.shared_info->vblank_sem = -1;
	}

	if (info.frame_buffer_area >= B_OK) {
		vm_change_clones_to_null_areas(info.frame_buffer_area);
		delete_area(info.frame_buffer_area);
		info.frame_buffer_area = -1;
		info.frame_buffer = NULL;
	}

	if (info.registers_area >= B_OK) {
		delete_area(info.registers_area);
		info.registers_area = -1;
		info.registers = NULL;
	}

	if (info.shared_area >= B_OK) {
		delete_area(info.shared_area);
		info.shared_area = -1;
		info.shared_info = NULL;
	}
}


extern "C" status_t
init_hardware(void)
{
	pci_module_info* pci;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	gPCI = pci;

	int32 cookie = 0;
	pci_info info;
	const supported_device* device;
	status = get_next_supported_device(&cookie, info, &device);

	put_module(B_PCI_MODULE_NAME);
	gPCI = NULL;
	return status;
}


extern "C" status_t
init_driver(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK)
		return status;

	mutex_init(&gLock, "intel_arc lock");
	memset(gDeviceNames, 0, sizeof(gDeviceNames));
	memset(gDeviceInfo, 0, sizeof(gDeviceInfo));

	int32 found = 0;
	for (int32 cookie = 0; found < MAX_DEVICES;) {
		pci_info info;
		const supported_device* device;
		status = get_next_supported_device(&cookie, info, &device);
		if (status != B_OK)
			break;

		intel_arc_info* arcInfo = (intel_arc_info*)calloc(1,
			sizeof(intel_arc_info));
		if (arcInfo == NULL) {
			status = B_NO_MEMORY;
			break;
		}

		arcInfo->id = found;
		arcInfo->device = device;
		arcInfo->pci = info;
		arcInfo->shared_area = -1;
		arcInfo->registers_area = -1;
		arcInfo->frame_buffer_area = -1;

		char name[B_OS_NAME_LENGTH];
		snprintf(name, sizeof(name), "graphics/%s_%02x%02x%02x",
			INTEL_ARC_DEVICE_NAME, info.bus, info.device, info.function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL) {
			free(arcInfo);
			status = B_NO_MEMORY;
			break;
		}

		gDeviceInfo[found] = arcInfo;
		found++;
	}

	gDeviceNames[found] = NULL;
	if (found == 0 || status == B_NO_MEMORY) {
		uninit_driver();
		return found == 0 ? ENODEV : status;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	for (int32 i = 0; i < MAX_DEVICES; i++) {
		if (gDeviceInfo[i] != NULL) {
			uninit_device(*gDeviceInfo[i]);
			free(gDeviceInfo[i]);
			gDeviceInfo[i] = NULL;
		}
		free(gDeviceNames[i]);
		gDeviceNames[i] = NULL;
	}

	if (gPCI != NULL) {
		put_module(B_PCI_MODULE_NAME);
		gPCI = NULL;
	}

	mutex_destroy(&gLock);
}


extern "C" const char**
publish_devices(void)
{
	return (const char**)gDeviceNames;
}


extern "C" device_hooks*
find_device(const char* name)
{
	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		if (strcmp(name, gDeviceNames[i]) == 0)
			return &sDeviceHooks;
	}

	return NULL;
}


static status_t
open_hook(const char* name, uint32 /*flags*/, void** cookie)
{
	intel_arc_info* info = NULL;

	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		if (strcmp(name, gDeviceNames[i]) == 0) {
			info = gDeviceInfo[i];
			break;
		}
	}

	if (info == NULL)
		return B_BAD_VALUE;

	mutex_lock(&gLock);
	status_t status = B_OK;
	if (info->open_count == 0)
		status = init_device(*info);

	if (status == B_OK) {
		info->open_count++;
		*cookie = info;
	}
	mutex_unlock(&gLock);
	return status;
}


static status_t
close_hook(void* /*cookie*/)
{
	return B_OK;
}


static status_t
free_hook(void* cookie)
{
	intel_arc_info* info = (intel_arc_info*)cookie;

	mutex_lock(&gLock);
	if (info->open_count-- == 1)
		uninit_device(*info);
	mutex_unlock(&gLock);

	return B_OK;
}


static status_t
control_hook(void* cookie, uint32 msg, void* buf, size_t len)
{
	intel_arc_info* info = (intel_arc_info*)cookie;

	switch (msg) {
		case B_GET_ACCELERANT_SIGNATURE:
			if (user_strlcpy((char*)buf, INTEL_ARC_ACCELERANT_NAME, len) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

		case INTEL_ARC_GET_PRIVATE_DATA:
		{
			if (!IS_USER_ADDRESS(buf) || len < sizeof(intel_arc_get_private_data))
				return B_BAD_ADDRESS;

			intel_arc_get_private_data data;
			if (user_memcpy(&data, buf, sizeof(data)) < B_OK)
				return B_BAD_ADDRESS;

			if (data.magic != INTEL_ARC_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;

			data.shared_info_area = info->shared_area;
			return user_memcpy(buf, &data, sizeof(data));
		}

		case INTEL_ARC_GET_DEVICE_NAME:
			if (user_strlcpy((char*)buf, gDeviceNames[info->id], len) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

		case INTEL_ARC_CLONE_FRAME_BUFFER:
		{
			if (info->frame_buffer_area < B_OK)
				return B_UNSUPPORTED;

			if (!IS_USER_ADDRESS(buf) || len < sizeof(area_info))
				return B_BAD_ADDRESS;

			void* address = NULL;
			area_id area = vm_clone_area(B_CURRENT_TEAM,
				"intel arc cloned framebuffer", &address, B_ANY_ADDRESS,
				B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA, REGION_NO_PRIVATE_MAP,
				info->frame_buffer_area, false);
			if (area < B_OK)
				return area;

			area_info areaInfo;
			status_t status = get_area_info(area, &areaInfo);
			if (status != B_OK) {
				delete_area(area);
				return status;
			}

			return user_memcpy(buf, &areaInfo, sizeof(area_info));
		}
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
read_hook(void* /*cookie*/, off_t /*pos*/, void* /*buffer*/, size_t* len)
{
	*len = 0;
	return B_NOT_ALLOWED;
}


static status_t
write_hook(void* /*cookie*/, off_t /*pos*/, const void* /*buffer*/,
	size_t* len)
{
	*len = 0;
	return B_NOT_ALLOWED;
}
