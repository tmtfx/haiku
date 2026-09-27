/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef NVIDIA_GT7XX_PRIVATE_H
#define NVIDIA_GT7XX_PRIVATE_H


#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>

#include "DriverInterface.h"


#define DEVICE_NAME "nvidia_gt7xx"
#define VESA_ACCELERANT_NAME "nvidia_gt7xx.accelerant"


struct vesa_info {
	int32			open_count;
	int32			id;
	pci_info		pci;

	area_id			shared_area;
	vesa_shared_info* shared_info;

	area_id			registers_area;
	uint8*			registers;

	area_id			frame_buffer_area;
	addr_t			frame_buffer;
	phys_addr_t		physical_frame_buffer;
	size_t			physical_frame_buffer_size;
	uint8			mmio_bar_index;
	uint8			frame_buffer_bar_index;

	area_id			channel_push_area[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	uint32*			channel_push_buffer[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	area_id			channel_notifier_area[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	uint32*			channel_notifier[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	area_id			channel_instance_area[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	uint32*			channel_instance_data[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
};


status_t nvidia_gt7xx_init(vesa_info& info);
void nvidia_gt7xx_uninit(vesa_info& info);
status_t nvidia_gt7xx_set_display_mode(vesa_info& info, const display_mode& mode);
status_t nvidia_gt7xx_get_dpms_mode(vesa_info& info, uint32& mode);
status_t nvidia_gt7xx_set_dpms_mode(vesa_info& info, uint32 mode);
status_t nvidia_gt7xx_submit_evo(vesa_info& info, const nvidia_gt7xx_evo_push& push);

#endif	/* NVIDIA_GT7XX_PRIVATE_H */
