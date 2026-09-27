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
#define NVIDIA_GT7XX_EVO_PUSH_WORDS 1024
#define NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS 128

enum nvidia_gt7xx_output_type {
	NVIDIA_GT7XX_OUTPUT_UNKNOWN = 0,
	NVIDIA_GT7XX_OUTPUT_ANALOG,
	NVIDIA_GT7XX_OUTPUT_DIGITAL,
};

enum nvidia_gt7xx_evo_channel_id {
	NVIDIA_GT7XX_EVO_CHANNEL_CORE = 0,
	NVIDIA_GT7XX_EVO_CHANNEL_BASE = 1,
	NVIDIA_GT7XX_EVO_CHANNEL_COUNT = 2,
};

#define NVIDIA_GT7XX_EVO_MAX_METHODS 32
#define NVIDIA_GT7XX_EVO_MAGIC 'gevo'

#define NVIDIA_GT7XX_EVO_CLASS_CORE 0x0000927d
#define NVIDIA_GT7XX_EVO_CLASS_BASE 0x0000927c
#define NVIDIA_GT7XX_EVO_PRAMIN_BASE 0x00700000U
#define NVIDIA_GT7XX_EVO_DMA_USER_BASE 0x00c00000U
#define NVIDIA_GT7XX_EVO_DMA_USER_STRIDE 0x00001000U
#define NVIDIA_GT7XX_EVO_RAMFC_WORDS 64

#define NVIDIA_GT7XX_EVO_DMA_OPCODE_METHOD 0x00000000U
#define NVIDIA_GT7XX_EVO_DMA_OPCODE_JUMP 0x20000000U
#define NVIDIA_GT7XX_EVO_DMA_OPCODE_NONINC_METHOD 0x40000000U
#define NVIDIA_GT7XX_EVO_DMA_METHOD_COUNT_SHIFT 18
#define NVIDIA_GT7XX_EVO_DMA_METHOD_OFFSET_SHIFT 2

#define NVIDIA_GT7XX_EVO_UPDATE 0x00000080
#define NVIDIA_GT7XX_EVO_DAC_SET_CONTROL(output) \
	((uint32)(0x00000400 + (output) * 0x00000080))
#define NVIDIA_GT7XX_EVO_DAC_SET_POLARITY(output) \
	((uint32)(0x00000404 + (output) * 0x00000080))
#define NVIDIA_GT7XX_EVO_SOR_SET_CONTROL(output) \
	((uint32)(0x00000600 + (output) * 0x00000040))
#define NVIDIA_GT7XX_EVO_HEAD_SET_PIXEL_CLOCK(head) \
	((uint32)(0x00000804 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_CONTROL(head) \
	((uint32)(0x00000808 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_OVERSCAN_COLOR(head) \
	((uint32)(0x00000810 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SIZE(head) \
	((uint32)(0x00000814 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SYNC_END(head) \
	((uint32)(0x00000818 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_END(head) \
	((uint32)(0x0000081c + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_START(head) \
	((uint32)(0x00000820 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK2(head) \
	((uint32)(0x00000824 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK_DMI(head) \
	((uint32)(0x00000828 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_DEFAULT_BASE_COLOR(head) \
	((uint32)(0x0000082c + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_OFFSET(head, index) \
	((uint32)(0x00000860 + (head) * 0x00000400 + (index) * 0x00000004))
#define NVIDIA_GT7XX_EVO_HEAD_SET_SIZE(head) \
	((uint32)(0x00000868 + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_STORAGE(head) \
	((uint32)(0x0000086c + (head) * 0x00000400))
#define NVIDIA_GT7XX_EVO_HEAD_SET_PARAMS(head) \
	((uint32)(0x00000870 + (head) * 0x00000400))

struct nvidia_gt7xx_evo_method {
	uint32			method;
	uint32			value;
};

struct nvidia_gt7xx_evo_memory_binding {
	uint64			pushbuf_physical;
	uint64			notifier_physical;
	uint64			instance_physical;
	uint32			pushbuf_size;
	uint32			notifier_size;
	uint32			instance_size;
	uint32			flags;
};

struct nvidia_gt7xx_evo_channel_state {
	uint32			class_id;
	area_id			push_area;
	area_id			notifier_area;
	area_id			instance_area;
	uint32			push_words;
	uint32			notifier_dwords;
	uint32			channel_id;
	uint32			version;
	nvidia_gt7xx_evo_memory_binding memory;
	uint32			user_aperture_offset;
	uint32			pramin_offset;
	uint32			put;
	uint32			get;
	uint32			last_submit_words;
	uint32			submit_count;
	uint32			notifier_status;
	uint32			materialized;
};

struct nvidia_gt7xx_evo_push {
	uint32			magic;
	uint32			channel;
	uint32			count;
	nvidia_gt7xx_evo_method methods[NVIDIA_GT7XX_EVO_MAX_METHODS];
};

struct nvidia_gt7xx_evo_state {
	uint32			submit_count;
	uint32			last_method_count;
	uint32			last_update;
	uint32			output_control;
	uint32			output_polarity;
	uint32			pixel_clock;
	uint32			head_control;
	uint32			overscan_color;
	uint32			raster_size;
	uint32			raster_sync_end;
	uint32			raster_blank_end;
	uint32			raster_blank_start;
	uint32			raster_vert_blank2;
	uint32			raster_vert_blank_dmi;
	uint32			default_base_color;
	uint32			base_offset;
	uint32			base_size;
	uint32			base_storage;
	uint32			base_params;
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
	nvidia_gt7xx_evo_channel_state channels[NVIDIA_GT7XX_EVO_CHANNEL_COUNT];
	nvidia_gt7xx_evo_state evo;
};

enum {
	VESA_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	VESA_CLONE_FRAME_BUFFER,
	VESA_GET_DEVICE_NAME,
	VESA_SET_DISPLAY_MODE,
	VESA_GET_DPMS_MODE,
	VESA_SET_DPMS_MODE,
	NVIDIA_GT7XX_SUBMIT_EVO,
};

#endif	/* NVIDIA_GT7XX_DRIVER_INTERFACE_H */
