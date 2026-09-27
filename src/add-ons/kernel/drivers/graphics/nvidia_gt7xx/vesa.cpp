/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "vesa_private.h"

#include <boot_item.h>
#include <frame_buffer_console.h>
#include <driver_settings.h>
#include <KernelExport.h>
#include <PCI.h>
#include <util/AreaKeeper.h>
#include <string.h>

#include "driver.h"
#include "DriverInterface.h"

#define ROUND_TO_PAGE_SIZE(x) (((x) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1))


struct pci_bar_info {
	int32		index;
	uint8		flags;
	uint64		base;
	uint64		size;
	int32		consumed;
};


static bool
supports_device(uint16 vendorID, uint16 deviceID)
{
	return vendorID == 0x10de && deviceID == 0x1287;
}


static const char*
device_name(uint16 vendorID, uint16 deviceID)
{
	if (vendorID == 0x10de && deviceID == 0x1287)
		return "NVIDIA GeForce GT 730 (GK208)";

	return "NVIDIA GT7xx";
}


static color_space
get_color_space_for_depth(uint32 depth)
{
	switch (depth) {
		case 8:
			return B_CMAP8;
		case 15:
			return B_RGB15_LITTLE;
		case 16:
			return B_RGB16_LITTLE;
		case 24:
			return B_RGB24_LITTLE;
		case 32:
			return B_RGB32_LITTLE;
		default:
			return B_NO_COLOR_SPACE;
	}
}


static uint32
get_dpms_capabilities(const edid1_info* edid)
{
	uint32 capabilities = B_DPMS_ON;
	if (edid == NULL)
		return capabilities;

	if (edid->display.dpms_standby)
		capabilities |= B_DPMS_STAND_BY;
	if (edid->display.dpms_suspend)
		capabilities |= B_DPMS_SUSPEND;
	if (edid->display.dpms_off)
		capabilities |= B_DPMS_OFF;

	return capabilities;
}


static uint8
get_active_output_type(const edid1_info* edid)
{
	if (edid == NULL)
		return NVIDIA_GT7XX_OUTPUT_UNKNOWN;

	return edid->display.input_type != 0
		? NVIDIA_GT7XX_OUTPUT_DIGITAL : NVIDIA_GT7XX_OUTPUT_ANALOG;
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

	if ((bar.flags & PCI_address_space) != 0 || bar.size == 0)
		return false;

	bar.base = info.u.h0.base_registers[index];
	if ((bar.flags & PCI_address_type) == PCI_address_type_64 && index < 5) {
		bar.base |= (uint64)info.u.h0.base_registers[index + 1] << 32;
		bar.size |= (uint64)info.u.h0.base_register_sizes[index + 1] << 32;
		bar.consumed = 2;
	}

	return bar.base != 0 && bar.size != 0;
}


static status_t
find_graphics_card(addr_t frameBuffer, pci_info& match, pci_bar_info& mmioBar,
	pci_bar_info& frameBufferBar)
{
	pci_module_info* pci;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	status = B_ENTRY_NOT_FOUND;
	pci_info info;
	for (int32 index = 0; pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.class_base != PCI_display)
			continue;
		if (!supports_device(info.vendor_id, info.device_id))
			continue;

		pci_bar_info chosenFrameBuffer = {};
		for (int32 barIndex = 0; barIndex < 6; barIndex++) {
			pci_bar_info bar;
			if (!get_bar_info(info, barIndex, bar))
				continue;
			if (bar.base <= frameBuffer && frameBuffer < bar.base + bar.size) {
				chosenFrameBuffer = bar;
				break;
			}
			barIndex += bar.consumed - 1;
		}

		if (chosenFrameBuffer.size == 0)
			continue;

		pci_bar_info chosenMmio = {};
		for (int32 barIndex = 0; barIndex < 6; barIndex++) {
			pci_bar_info bar;
			if (!get_bar_info(info, barIndex, bar))
				continue;
			if (bar.index == chosenFrameBuffer.index) {
				barIndex += bar.consumed - 1;
				continue;
			}

			if ((bar.flags & PCI_address_prefetchable) == 0) {
				chosenMmio = bar;
				break;
			}

			if (chosenMmio.size == 0)
				chosenMmio = bar;

			barIndex += bar.consumed - 1;
		}

		if (chosenMmio.size == 0)
			continue;

		match = info;
		mmioBar = chosenMmio;
		frameBufferBar = chosenFrameBuffer;
		status = B_OK;
		break;
	}

	put_module(B_PCI_MODULE_NAME);
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


status_t
vesa_init(vesa_info& info)
{
	frame_buffer_boot_info* bootInfo
		= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bootInfo == NULL)
		return B_ERROR;

	pci_bar_info mmioBar = {};
	pci_bar_info frameBufferBar = {};
	status_t status = find_graphics_card(bootInfo->physical_frame_buffer,
		info.pci, mmioBar, frameBufferBar);
	if (status != B_OK)
		return status;

	AreaKeeper sharedKeeper;
	info.shared_area = sharedKeeper.Create("nvidia_gt7xx shared info",
		(void**)&info.shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(vesa_shared_info)), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info.shared_area < B_OK)
		return info.shared_area;

	memset(info.shared_info, 0, sizeof(vesa_shared_info));
	vesa_shared_info& sharedInfo = *info.shared_info;

	sharedInfo.vendor_id = info.pci.vendor_id;
	sharedInfo.device_id = info.pci.device_id;
	strlcpy(sharedInfo.name, device_name(info.pci.vendor_id, info.pci.device_id),
		sizeof(sharedInfo.name));
	sharedInfo.registers_base = mmioBar.base;
	sharedInfo.registers_size = mmioBar.size > UINT32_MAX
		? UINT32_MAX : (uint32)mmioBar.size;
	sharedInfo.mmio_bar = mmioBar.index;
	sharedInfo.frame_buffer_bar = frameBufferBar.index;
	sharedInfo.frame_buffer_base = frameBufferBar.base;
	sharedInfo.frame_buffer_size = frameBufferBar.size > UINT32_MAX
		? UINT32_MAX : (uint32)frameBufferBar.size;
	sharedInfo.physical_frame_buffer = bootInfo->physical_frame_buffer;
	sharedInfo.vram_size = sharedInfo.frame_buffer_size;
	sharedInfo.boot_width = bootInfo->width;
	sharedInfo.boot_height = bootInfo->height;
	sharedInfo.boot_depth = bootInfo->depth;
	sharedInfo.current_mode.virtual_width = bootInfo->width;
	sharedInfo.current_mode.virtual_height = bootInfo->height;
	sharedInfo.current_mode.space = get_color_space_for_depth(bootInfo->depth);
	sharedInfo.current_mode.timing.h_display = bootInfo->width;
	sharedInfo.current_mode.timing.v_display = bootInfo->height;
	sharedInfo.bytes_per_row = bootInfo->bytes_per_row;
	sharedInfo.fbc.frame_buffer = (void*)bootInfo->frame_buffer;
	sharedInfo.fbc.frame_buffer_dma = (void*)bootInfo->physical_frame_buffer;
	sharedInfo.fbc.bytes_per_row = bootInfo->bytes_per_row;
	sharedInfo.frame_buffer_area = info.frame_buffer_area = bootInfo->area;
	info.frame_buffer = bootInfo->frame_buffer;
	info.physical_frame_buffer = bootInfo->physical_frame_buffer;
	info.physical_frame_buffer_size = frameBufferBar.size;
	info.mmio_bar_index = mmioBar.index;
	info.frame_buffer_bar_index = frameBufferBar.index;
	sharedInfo.active_head = 0;
	sharedInfo.dpms_mode = B_DPMS_ON;
	sharedInfo.native_mode_set = false;

	edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (edidInfo != NULL) {
		sharedInfo.has_edid = true;
		sharedInfo.edid_info = *edidInfo;
		sharedInfo.active_output = get_active_output_type(edidInfo);
		sharedInfo.dpms_capabilities = get_dpms_capabilities(edidInfo);
	} else {
		sharedInfo.active_output = NVIDIA_GT7XX_OUTPUT_UNKNOWN;
		sharedInfo.dpms_capabilities = B_DPMS_ON;
	}

	AreaKeeper mmioKeeper;
	info.registers_area = mmioKeeper.Map("nvidia_gt7xx mmio", mmioBar.base,
		mmioBar.size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&info.registers);
	if (info.registers_area < B_OK)
		return info.registers_area;

	sharedInfo.registers_area = info.registers_area;

	sharedKeeper.Detach();
	mmioKeeper.Detach();
	return B_OK;
}


void
vesa_uninit(vesa_info& info)
{
	if (info.registers_area >= B_OK) {
		delete_area(info.registers_area);
		info.registers_area = -1;
	}

	if (info.shared_area >= B_OK) {
		delete_area(info.shared_area);
		info.shared_area = -1;
	}
}


status_t
vesa_set_display_mode(vesa_info& info, const display_mode& mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	if (!same_mode(mode, info.shared_info->current_mode)) {
		dprintf("nvidia_gt7xx: native modesetting for GK208 is not implemented yet;"
			" requested %ux%u space 0x%08" B_PRIx32 "\n",
			mode.virtual_width, mode.virtual_height, mode.space);
		return B_NOT_SUPPORTED;
	}

	info.shared_info->native_mode_set = true;
	return B_OK;
}


status_t
vesa_get_dpms_mode(vesa_info& info, uint32& mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	mode = info.shared_info->dpms_mode;
	return B_OK;
}


status_t
vesa_set_dpms_mode(vesa_info& info, uint32 mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	if (mode != B_DPMS_ON)
		return B_NOT_SUPPORTED;

	info.shared_info->dpms_mode = mode;
	return B_OK;
}
