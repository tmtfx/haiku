/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef NVIDIA_GT7XX_DRIVER_INTERFACE_H
#define NVIDIA_GT7XX_DRIVER_INTERFACE_H


#include <Drivers.h>
#include <Accelerant.h>
#include <PCI.h>

#include <edid.h>


#define DRIVER_PREFIX "nvidia_gt7xx"
#define VESA_EDID_BOOT_INFO "vesa_edid/v1"

enum nvidia_gt7xx_output_type {
	NVIDIA_GT7XX_OUTPUT_UNKNOWN = 0,
	NVIDIA_GT7XX_OUTPUT_ANALOG,
	NVIDIA_GT7XX_OUTPUT_DIGITAL,
};

struct vesa_shared_info {
	area_id			mode_list_area;
	uint32			mode_count;
	display_mode	current_mode;
	uint32			bytes_per_row;

	edid1_info		edid_info;
	bool			has_edid;
	uint8			active_output;
	uint8			active_head;
	uint8			dpms_capabilities;
	uint8			dpms_mode;

	char			name[32];
	uint16			vendor_id;
	uint16			device_id;
	uint32			vram_size;

	area_id			registers_area;
	phys_addr_t		registers_base;
	uint32			registers_size;
	uint8			mmio_bar;
	uint8			frame_buffer_bar;
	uint8			reserved[2];

	area_id			frame_buffer_area;
	phys_addr_t		frame_buffer_base;
	uint32			frame_buffer_size;
	addr_t			physical_frame_buffer;
	frame_buffer_config fbc;

	uint32			boot_width;
	uint32			boot_height;
	uint32			boot_depth;
	bool			native_mode_set;
};

enum {
	VESA_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	VESA_CLONE_FRAME_BUFFER,
	VESA_GET_DEVICE_NAME,
	VESA_SET_DISPLAY_MODE,
	VESA_GET_DPMS_MODE,
	VESA_SET_DPMS_MODE,
};

#endif	/* NVIDIA_GT7XX_DRIVER_INTERFACE_H */
