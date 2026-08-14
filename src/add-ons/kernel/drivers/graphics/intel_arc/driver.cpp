/*
 * Copyright 2026, Haiku contributors.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define TRACE_INTEL_ARC
#ifdef TRACE_INTEL_ARC
#	define TRACE(x...) dprintf("intel_arc: " x)
#else
#	define TRACE(x...) do {} while (false)
#endif
#define ERROR(x...) dprintf("intel_arc: " x)

#define MAX_DEVICES 8
#define MAX_CLONED_FRAMEBUFFER_SIZE (64 * 1024 * 1024)
#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))

/*
 * Raw display/MMIO offsets below are a minimal, MIT-compatible reinterpretation
 * of register definitions from:
 *   headers/private/graphics/intel_extreme/intel_extreme.h
 *
 * They are used only for non-destructive bring-up introspection:
 * reading current pipe state, attached DDI path, and active mode sizing.
 */
#define INTEL_ARC_MMIO_PIPE_BLOCK_BASE			0x60000
#define INTEL_ARC_MMIO_PLANE_BLOCK_BASE			0x70000
#define INTEL_ARC_MMIO_PIPE_OFFSET				0x1000
#define INTEL_ARC_MMIO_PIPE_A_SIZE				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x001c)
#define INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0400)
#define INTEL_ARC_MMIO_PIPE_A_HTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0000)
#define INTEL_ARC_MMIO_PIPE_A_HBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0004)
#define INTEL_ARC_MMIO_PIPE_A_HSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PIPE_A_VTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x000c)
#define INTEL_ARC_MMIO_PIPE_A_VBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0010)
#define INTEL_ARC_MMIO_PIPE_A_VSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0014)
#define INTEL_ARC_MMIO_PIPE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PLANE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0180)
#define INTEL_ARC_MMIO_PLANE_A_STRIDE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0188)
#define INTEL_ARC_MMIO_PLANE_A_POS				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x018c)
#define INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE		(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0190)
#define INTEL_ARC_MMIO_PLANE_A_SURFACE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x019c)
#define INTEL_ARC_MMIO_PORT_A					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4000)
#define INTEL_ARC_MMIO_PORT_B					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4100)
#define INTEL_ARC_MMIO_PORT_C					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4200)
#define INTEL_ARC_MMIO_PORT_D					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4300)

#define INTEL_ARC_PIPE_ENABLED					(1UL << 31)
#define INTEL_ARC_PIPE_DDI_ENABLE				(1UL << 31)
#define INTEL_ARC_PIPE_DDI_SELECT_SHIFT		28
#define INTEL_ARC_PIPE_DDI_SELECT_MASK			(7 << INTEL_ARC_PIPE_DDI_SELECT_SHIFT)
#define INTEL_ARC_PIPE_DDI_MODE_SHIFT			24
#define INTEL_ARC_PIPE_DDI_MODE_MASK			(7 << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
#define INTEL_ARC_PORT_ENABLED					(1UL << 31)
#define INTEL_ARC_PORT_DETECTED				(1UL << 2)


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
static void probe_display_state(intel_arc_info& info);
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


/*
 * ARC PCI IDs collected from public hardware ID databases and release coverage:
 *  - https://pci-ids.ucw.cz/read/PC/8086
 *  - https://github.com/pciutils/pciids
 *  - https://www.phoronix.com/news/Three-More-Battlemage-IDs
 *  - https://wccftech.com/intel-adds-big-battlemage-bmg-g31-gpu-support-four-device-ids-spotted/
 *  - src/data/ids/pci.ids in this repository
 */
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
	/*
	 * Reinterpreted from framebuffer/vesa depth conversion helpers:
	 *  - src/add-ons/kernel/drivers/graphics/framebuffer/framebuffer.cpp
	 *  - src/add-ons/kernel/drivers/graphics/vesa/vesa.cpp
	 */
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
	return true;
}


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
		read32(info, INTEL_ARC_MMIO_PIPE_A_HTOTAL + stride,
			shared.pipe_h_total[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_HBLANK + stride,
			shared.pipe_h_blank[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_HSYNC + stride,
			shared.pipe_h_sync[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VTOTAL + stride,
			shared.pipe_v_total[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VBLANK + stride,
			shared.pipe_v_blank[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_VSYNC + stride,
			shared.pipe_v_sync[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_CONTROL + stride,
			shared.pipe_control[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_SIZE + stride,
			shared.pipe_size[pipe]);
		read32(info, INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + stride,
			shared.pipe_ddi_func_ctl[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_CONTROL + stride,
			shared.plane_control[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_STRIDE + stride,
			shared.plane_stride[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_POS + stride,
			shared.plane_pos[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE + stride,
			shared.plane_image_size[pipe]);
		read32(info, INTEL_ARC_MMIO_PLANE_A_SURFACE + stride,
			shared.plane_surface[pipe]);

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
	memset(&bar, 0, sizeof(bar));
	bar.index = index;
	bar.consumed = 1;

	if (index < 0 || index >= 6)
		return false;

	bar.flags = info.u.h0.base_register_flags[index];
	bar.size = info.u.h0.base_register_sizes[index];
	if ((bar.flags & PCI_address_space) != 0 || bar.size == 0) {
		return false;
	}

	bar.base = info.u.h0.base_registers[index];

	if ((bar.flags & PCI_address_type) == PCI_address_type_64 && index < 5) {
		bar.base |= (uint64)info.u.h0.base_registers[index + 1] << 32;
		bar.size |= (uint64)info.u.h0.base_register_sizes[index + 1] << 32;
		bar.consumed = 2;
	}

	return bar.base != 0 && bar.size != 0;
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

		// Heuristic: ARC exposes a small non-prefetchable MMIO BAR and a large
		// prefetchable VRAM/aperture BAR. Prefer that split when present.
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

		frame_buffer_boot_info* bootInfo
			= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
		if (bootInfo != NULL
			&& (phys_addr_t)bootInfo->physical_frame_buffer >= frameBufferBar.base
			&& (phys_addr_t)bootInfo->physical_frame_buffer < frameBufferBar.base
				+ frameBufferBar.size) {
			size_t bootSurfaceSize = (size_t)bootInfo->bytes_per_row
				* (size_t)bootInfo->height;
			if (bootSurfaceSize > 0
				&& bootSurfaceSize < MAX_CLONED_FRAMEBUFFER_SIZE) {
				mapSize = ROUND_TO_PAGE_SIZE(bootSurfaceSize);
			}

			info.shared_info->current_mode.virtual_width = bootInfo->width;
			info.shared_info->current_mode.virtual_height = bootInfo->height;
			info.shared_info->current_mode.space
				= get_color_space_for_depth(bootInfo->depth);
			info.shared_info->bytes_per_row = bootInfo->bytes_per_row;
		}

		info.frame_buffer_area = map_physical_memory("intel arc framebuffer",
			frameBufferBar.base, mapSize, B_ANY_KERNEL_ADDRESS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA
				| B_CLONEABLE_AREA,
			(void**)&info.frame_buffer);
		if (info.frame_buffer_area >= B_OK) {
			info.shared_info->frame_buffer_area = info.frame_buffer_area;
			info.shared_info->frame_buffer_base = frameBufferBar.base;
			info.shared_info->frame_buffer_size = mapSize;
			info.shared_info->frame_buffer = (addr_t)info.frame_buffer;
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

	sharedKeeper.Detach();
	mmioKeeper.Detach();

	TRACE("initialized %s (%04x:%04x), MMIO BAR %d @ %#" B_PRIxPHYSADDR
		", framebuffer BAR %d @ %#" B_PRIxPHYSADDR
		", active pipe %d size %#" B_PRIx32 " ddi %#" B_PRIx32 "\n",
		info.device->name, info.pci.vendor_id, info.pci.device_id,
		(int)info.shared_info->mmio_bar, info.shared_info->registers_base,
		(int)info.shared_info->frame_buffer_bar,
		info.shared_info->frame_buffer_base, (int)info.shared_info->active_pipe,
		info.shared_info->active_pipe >= 0
			? info.shared_info->pipe_size[info.shared_info->active_pipe] : 0,
		info.shared_info->active_pipe >= 0
			? info.shared_info->pipe_ddi_func_ctl[info.shared_info->active_pipe] : 0);
	return B_OK;
}


static void
uninit_device(intel_arc_info& info)
{
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

			void* address = NULL;
			area_id area = vm_clone_area(B_CURRENT_TEAM,
				"intel arc cloned framebuffer", &address, B_ANY_ADDRESS,
				B_READ_AREA | B_WRITE_AREA, 0, info->frame_buffer_area, true);
			if (area < B_OK)
				return area;

			return _user_get_area_info(area, (area_info*)buf);
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
