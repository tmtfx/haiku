/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "framebuffer_private.h"
#include "vesa.h"

#include <string.h>

#include <drivers/bios.h>

#include <boot_item.h>
#include <frame_buffer_console.h>
#include <util/kernel_cpp.h>
#include <vm/vm.h>

#include "driver.h"
#include "utility.h"
#include "vesa_info.h"
#ifdef IS_PIRATI_BUILD
#include "framebuffer_logo.h"
#endif

static uint32
get_color_space_for_depth(uint32 depth)
{
	switch (depth) {
		case 1:
			return B_GRAY1;
		case 4:
			return B_GRAY8;
				// the app_server is smart enough to translate this to VGA mode
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
	}

	return 0;
}


static status_t
remap_frame_buffer(framebuffer_info& info, addr_t physicalBase, uint32 width,
	uint32 height, int8 depth, uint32 bytesPerRow)
{
	vesa_shared_info& sharedInfo = *info.shared_info;
	addr_t frameBuffer = info.frame_buffer;

	addr_t base = physicalBase;
	size_t size = bytesPerRow * height;

	area_id area = map_physical_memory("framebuffer buffer", base,
		size, B_ANY_KERNEL_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		(void**)&frameBuffer);
	if (area < 0)
		return area;

	frame_buffer_update(frameBuffer, width, height, depth,
		bytesPerRow);

	vm_change_clones_to_null_areas(info.frame_buffer_area);
	delete_area(info.frame_buffer_area);

	info.frame_buffer = frameBuffer;
	info.frame_buffer_area = area;

	// Turn on write combining for the area
	vm_set_area_memory_type(area, base, B_WRITE_COMBINING_MEMORY);

	// Update shared frame buffer information
	sharedInfo.bytes_per_row = bytesPerRow;

	return B_OK;
}


//	#pragma mark -


#ifdef IS_PIRATI_BUILD
static void
draw_framebuffer_logo(framebuffer_info &info, struct frame_buffer_boot_info *bi)
{
	if (!bi)
		return;

	if (bi->depth != 32)
		return;

	uint32 screenWidth = bi->width;
	uint32 screenHeight = bi->height;
	uint32 bytesPerRow = bi->bytes_per_row ? bi->bytes_per_row : screenWidth * 4;
	uint32 fbPitch = bytesPerRow / 4;

	uint32 logoW = framebuffer_logo_width;
	uint32 logoH = framebuffer_logo_height;
	
	int32 startX = (int32)((screenWidth - logoW) / 2);
	if (startX < 0) startX = 0;

	int32 startY = (int32)((screenHeight - logoH) / 2);
	if (startY < 0) startY = 0;
	
	uint8* fb = (uint8*)info.frame_buffer;
	if (fb == NULL) {
		fb = (uint8*)bi->frame_buffer;
	}

	if (fb == NULL)
		return;

	for (uint32 y = 0; y < logoH && (startY + y) < screenHeight; y++) {
		uint32 fbOffset = ((startY + y) * fbPitch + startX) * sizeof(uint32);
		uint32 logoRowOffset = y * logoW;
		uint32 remainingWidth = screenWidth - startX;
		uint32 copyPixels = (logoW < remainingWidth) ? logoW : remainingWidth;
		uint32 copySize = copyPixels * sizeof(uint32);

		user_memcpy(fb + fbOffset, (void*)&framebuffer_logo[logoRowOffset], copySize);
	}
}
#endif

status_t
framebuffer_init(framebuffer_info& info)
{
	frame_buffer_boot_info* bufferInfo
		= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bufferInfo == NULL)
		return B_ERROR;

	size_t sharedSize = (sizeof(vesa_shared_info) + 7) & ~7;

	info.shared_area = create_area("framebuffer shared info",
		(void**)&info.shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sharedSize), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info.shared_area < 0)
		return info.shared_area;

	vesa_shared_info& sharedInfo = *info.shared_info;

	memset(&sharedInfo, 0, sizeof(vesa_shared_info));

	info.frame_buffer_area = bufferInfo->area;

	remap_frame_buffer(info, bufferInfo->physical_frame_buffer,
		bufferInfo->width, bufferInfo->height, bufferInfo->depth,
		bufferInfo->bytes_per_row);
		// Does not matter if this fails - the frame buffer was already mapped
		// before.

	sharedInfo.current_mode.virtual_width = bufferInfo->width;
	sharedInfo.current_mode.virtual_height = bufferInfo->height;
	sharedInfo.current_mode.space = get_color_space_for_depth(
		bufferInfo->depth);

	edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO,
		NULL);
	if (edidInfo != NULL) {
		sharedInfo.has_edid = true;
		memcpy(&sharedInfo.edid_info, edidInfo, sizeof(edid1_info));
	}
	
#ifdef IS_PIRATI_BUILD
	draw_framebuffer_logo(info, bufferInfo);
	snooze(2000000);
#endif
	dprintf(DEVICE_NAME ": framebuffer_init() completed successfully!\n");
	return B_OK;
}


void
framebuffer_uninit(framebuffer_info& info)
{
	dprintf(DEVICE_NAME": framebuffer_uninit()\n");

	vm_change_clones_to_null_areas(info.frame_buffer_area);
	delete_area(info.frame_buffer_area);
	delete_area(info.shared_area);
}
