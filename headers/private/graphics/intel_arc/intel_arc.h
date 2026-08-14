/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Shared private interface between the intel_arc kernel driver and the
 * intel_arc accelerant.
 *
 * In-tree structural references:
 *  - headers/private/graphics/intel_extreme/intel_extreme.h
 *  - headers/private/graphics/vesa/vesa_info.h
 *  - src/add-ons/kernel/drivers/graphics/framebuffer/framebuffer_private.h
 */

#ifndef INTEL_ARC_H
#define INTEL_ARC_H


#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>
#include <edid.h>


#define INTEL_ARC_DEVICE_NAME			"intel_arc"
#define INTEL_ARC_ACCELERANT_NAME		"intel_arc.accelerant"
#define INTEL_ARC_VENDOR_ID				0x8086
#define INTEL_ARC_PRIVATE_DATA_MAGIC	'iarc'


enum intel_arc_family {
	INTEL_ARC_FAMILY_UNKNOWN = 0,
	INTEL_ARC_FAMILY_ALCHEMIST,
	INTEL_ARC_FAMILY_BATTLEMAGE
};


struct intel_arc_shared_info {
	area_id			mode_list_area;
	uint32			mode_count;
	display_mode	current_mode;
	uint32			bytes_per_row;
	uint32			dpms_mode;
	sem_id			vblank_sem;

	area_id			registers_area;
	area_id			frame_buffer_area;

	uint16			vendor_id;
	uint16			family;
	uint32			device_id;
	uint8			revision;
	uint8			pipe_count;
	int8			active_pipe;
	uint8			active_ddi_port;
	uint8			active_ddi_mode;
	uint8			reserved1[3];
	uint16			subsystem_vendor_id;
	uint16			subsystem_id;
	uint8			has_boot_edid;
	uint8			detected_port_bits;
	uint8			has_dpcd;
	uint8			dpcd_revision;

	uint8			mmio_bar;
	uint8			frame_buffer_bar;
	uint8			reserved0[2];

	phys_addr_t		registers_base;
	uint64			registers_size;
	phys_addr_t		frame_buffer_base;
	uint64			frame_buffer_size;

	addr_t			frame_buffer;

	uint32			pipe_control[4];
	uint32			pipe_size[4];
	uint32			pipe_ddi_func_ctl[4];
	uint32			pipe_h_total[4];
	uint32			pipe_h_blank[4];
	uint32			pipe_h_sync[4];
	uint32			pipe_v_total[4];
	uint32			pipe_v_blank[4];
	uint32			pipe_v_sync[4];
	uint32			plane_control[4];
	uint32			plane_stride[4];
	uint32			plane_pos[4];
	uint32			plane_image_size[4];
	uint32			plane_surface[4];
	uint32			port_state[4];
	uint32			hotplug_ctl;
	uint32			hpd_iir;
	uint32			hotplug_event_count;
	uint8			dpcd[8];
	uint8			dpcd_max_lane_count;
	uint8			dpcd_sink_count;
	uint8			dpcd_max_link_rate;

	edid1_info		boot_edid;

	char			device_identifier[32];
};


enum {
	INTEL_ARC_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	INTEL_ARC_GET_DEVICE_NAME,
	INTEL_ARC_CLONE_FRAME_BUFFER
};


struct intel_arc_get_private_data {
	uint32	magic;
	area_id	shared_info_area;
};


#endif	/* INTEL_ARC_H */
