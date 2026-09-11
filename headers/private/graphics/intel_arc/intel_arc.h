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


/*
 * Raw display/MMIO offsets below are a minimal, MIT-compatible reinterpretation
 * of register definitions for Intel Display Engine.
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
#define INTEL_ARC_MMIO_PLANE_A_COLOR_CTL        (INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0184)
#define INTEL_ARC_MMIO_PLANE_A_STRIDE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0188)
#define INTEL_ARC_MMIO_PLANE_A_POS				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x018c)
#define INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE		(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0190)
#define INTEL_ARC_MMIO_PLANE_A_SURFACE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x019c)
#define INTEL_ARC_MMIO_PORT_A					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4000)
#define INTEL_ARC_MMIO_PORT_B					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4100)
#define INTEL_ARC_MMIO_PORT_C					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4200)
#define INTEL_ARC_MMIO_PORT_D					(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4300)
#define INTEL_ARC_MMIO_PCH_MASTER_INT_CTL		0x44200
#define INTEL_ARC_MMIO_PIPE_INT_MASK(pipe)		(0x44404 + ((pipe) - 1) * 0x10)
#define INTEL_ARC_MMIO_PIPE_INT_IDENTITY(pipe)	(0x44408 + ((pipe) - 1) * 0x10)
#define INTEL_ARC_MMIO_PIPE_INT_ENABLE(pipe)	(0x4440c + ((pipe) - 1) * 0x10)

#define INTEL_ARC_PIPE_ENABLED					(1UL << 31)
#define INTEL_ARC_PIPE_DDI_SELECT_SHIFT		28
#define INTEL_ARC_PIPE_DDI_SELECT_MASK			(7 << INTEL_ARC_PIPE_DDI_SELECT_SHIFT)
//#define INTEL_ARC_PIPE_DDI_MODE_SHIFT			24
//#define INTEL_ARC_PIPE_DDI_MODE_MASK			(7 << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
#define INTEL_ARC_PORT_ENABLED					(1UL << 31)
#define INTEL_ARC_PORT_DETECTED				(1UL << 2)
#define INTEL_ARC_MASTER_INT_GLOBAL			(1UL << 31)
#define INTEL_ARC_MASTER_INT_PIPE_PENDING(pipe)	(1UL << (15 + (pipe)))
#define INTEL_ARC_PIPE_INT_VBLANK				(1UL << 0)
#define INTEL_ARC_MAX_PIPES				4

#define INTEL_ARC_PIPE_DDI_FUNC_ENABLE           (1U << 31)

#define INTEL_ARC_PIPE_DDI_MODE_SHIFT            24
#define INTEL_ARC_PIPE_DDI_MODE_MASK             (0x7U << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
#define INTEL_ARC_PIPE_DDI_MODE_HDMI             (0U << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
//#define INTEL_ARC_PIPE_DDI_MODE_DP_SST           (1U << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
//#define INTEL_ARC_PIPE_DDI_MODE_DP_MST           (2U << INTEL_ARC_PIPE_DDI_MODE_SHIFT)
#define INTEL_ARC_PIPE_DDI_MODESEL_MASK			(7U << 24)
#define INTEL_ARC_PIPE_DDI_MODE_DP_SST			2U
#define INTEL_ARC_PIPE_DDI_MODE_DP_MST			3U

#define INTEL_ARC_PIPE_DDI_MODE_FDI              (3U << INTEL_ARC_PIPE_DDI_MODE_SHIFT)

#define INTEL_ARC_PIPE_DDI_BPC_SHIFT             20
#define INTEL_ARC_PIPE_DDI_BPC_MASK              (0x7U << INTEL_ARC_PIPE_DDI_BPC_SHIFT)
#define INTEL_ARC_PIPE_DDI_BPC_8                 (0U << INTEL_ARC_PIPE_DDI_BPC_SHIFT)
#define INTEL_ARC_PIPE_DDI_BPC_10                (1U << INTEL_ARC_PIPE_DDI_BPC_SHIFT)
#define INTEL_ARC_PIPE_DDI_BPC_6                 (2U << INTEL_ARC_PIPE_DDI_BPC_SHIFT)
#define INTEL_ARC_PIPE_DDI_BPC_12                (3U << INTEL_ARC_PIPE_DDI_BPC_SHIFT)

#define INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT        1 // non è 19 zio cribbio con 1 funziona?
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK         (0x7U << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_1            (0U << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_2            (1U << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_3            (2U << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_4            (3U << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT)

#define INTEL_ARC_DDI_VSYNC_POLARITY_POSITIVE    (1U << 17)
#define INTEL_ARC_DDI_HSYNC_POLARITY_POSITIVE    (1U << 16)

/* DisplayPort AUX Channel Registers (Display 13+) */
#define INTEL_ARC_MMIO_AUX_CH_CTL_A             (INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4010) // 0x64010
#define INTEL_ARC_MMIO_AUX_CH_DATA1_A           (INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4014) // 0x64014
#define INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE       0x100
#define DP_AUX_NATIVE_READ 0x9
#define DPCD_LINK_BW_SET             0x00100


enum intel_arc_family {
	INTEL_ARC_FAMILY_UNKNOWN = 0,
	INTEL_ARC_FAMILY_ALCHEMIST,
	INTEL_ARC_FAMILY_BATTLEMAGE
};

typedef struct {
	char	accelerant[B_FILE_NAME_LENGTH];
	bool	dumprom;
	uint32 	memory;		/* Forza riconoscimento memoria */
	bool	hardcursor;
	uint32	cursorbits;
	bool	overlay;
} intel_arc_settings;

struct dp_link_config {
    uint32 lanes;
    uint32 bpp;
    uint32 linkBandwidth; // in kHz
};

struct intel_arc_shared_info {
	// -------------------------------------------------------------------------
    // Informazioni e Aree di Memoria Generali
    // -------------------------------------------------------------------------
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
	uint16			subsystem_vendor_id;
	uint16			subsystem_id;
	
	// -------------------------------------------------------------------------
    // Configurazione Display, Pipe Attiva e DP / Boot
    // -------------------------------------------------------------------------
	uint8			pipe_count;
	int8			active_pipe;
	uint8			active_ddi_port;
	uint8			active_ddi_mode;
	uint8			reserved1[3];
	
	bool			has_boot_info;
    uint32			boot_width;
    uint32			boot_height;
    uint32			boot_depth;

	uint8			has_boot_edid;
	uint8			detected_port_bits;
	uint8			has_dpcd;
	uint8			dpcd_revision;
	
	bool			dp_link_trained_by_gop;
	dp_link_config	dp_boot_config;
	dp_link_config	dp_config[INTEL_ARC_MAX_PIPES];
	
	uint8			dpcd[8];
	uint8			dpcd_max_lane_count;
	uint8			dpcd_sink_count;
	uint8			dpcd_max_link_rate;
	edid1_info		boot_edid;
	frame_buffer_config fbc;
	
	// -------------------------------------------------------------------------
    // Mappature MMIO e Framebuffer
    // -------------------------------------------------------------------------
	uint8			mmio_bar;
	uint8			frame_buffer_bar;
	uint8			reserved0[2];

	phys_addr_t		registers_base;
	uint64			registers_size;
	phys_addr_t		frame_buffer_base;
	phys_addr_t		frame_buffer_offset;
	uint64			frame_buffer_size;

	addr_t			frame_buffer;
	uint64			vram_size;            // VRAM fisica totale (es. 16 GB)
	
	// -------------------------------------------------------------------------
    // Hardware Cursor
    // -------------------------------------------------------------------------
    bool			bDisableOverlay;
	bool			bDisableHdwCursor;      // Toggle impostabile da settings/driver
    bool			cursor_visible;
    uint16			cursor_hot_x;
    uint16			cursor_hot_y;

    void*			cursor_virtual_base_kernel;  // Puntatore kernel (opzionale)
    void*			cursor_virtual_base;         // Mappatura virtuale per accelerant (Userland)
    uint32			cursor_physical_base;        // Indirizzo fisico/GGTT letto da CUR_SURF
    
	// -------------------------------------------------------------------------
    // Stato Registri Hardware Pipe & Plane (Speculare ai Registri MMIO)
    // -------------------------------------------------------------------------
	uint32			pipe_control[INTEL_ARC_MAX_PIPES];
	uint32			pipe_size[INTEL_ARC_MAX_PIPES];
	uint32			pipe_ddi_func_ctl[INTEL_ARC_MAX_PIPES];
	uint32			pipe_data_m[INTEL_ARC_MAX_PIPES];
	uint32			pipe_h_total[INTEL_ARC_MAX_PIPES];
	uint32			pipe_h_blank[INTEL_ARC_MAX_PIPES];
	uint32			pipe_h_sync[INTEL_ARC_MAX_PIPES];
	uint32			pipe_v_total[INTEL_ARC_MAX_PIPES];
	uint32			pipe_v_blank[INTEL_ARC_MAX_PIPES];
	uint32			pipe_v_sync[INTEL_ARC_MAX_PIPES];
	uint32			plane_control[INTEL_ARC_MAX_PIPES];
	uint32			plane_stride[INTEL_ARC_MAX_PIPES];
	uint32			plane_pos[INTEL_ARC_MAX_PIPES];
	uint32			plane_image_size[INTEL_ARC_MAX_PIPES];
	uint32			plane_surface[INTEL_ARC_MAX_PIPES];
	// -------------------------------------------------------------------------
    // Port State & Hotplug Interrupts
    // -------------------------------------------------------------------------
	uint32			port_state[INTEL_ARC_MAX_PIPES];
	uint32			hotplug_ctl;
	uint32			hpd_iir;
	uint32			hotplug_event_count;
	
	
	//uint8			dp_lanes[INTEL_ARC_MAX_PIPES];
	//uint32			dp_bpp[INTEL_ARC_MAX_PIPES];

	

	// -------------------------------------------------------------------------
    // Driver Control & Settings
    // -------------------------------------------------------------------------
	intel_arc_settings	settings;
	char				device_identifier[32];
	bool				accelerant_in_use;

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
