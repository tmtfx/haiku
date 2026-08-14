/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 *
 * This accelerant is intentionally minimal but functional. Structure and
 * API usage were derived from MIT-compatible Haiku sources and MIT-licensed
 * upstream Intel sources:
 *  - src/add-ons/accelerants/framebuffer/accelerant.cpp
 *  - src/add-ons/accelerants/framebuffer/hooks.cpp
 *  - src/add-ons/accelerants/framebuffer/mode.cpp
 *  - src/add-ons/accelerants/intel_extreme/accelerant.cpp
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy_regs.h
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h
 *
 * No GPL-only source was used.
 */

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

#include <AutoDeleterOS.h>


accelerant_info* gInfo;

/*
 * AUX/DDC implementation below is a standalone procedural adaptation of the
 * MIT-licensed logic in src/add-ons/accelerants/intel_extreme/Ports.cpp.
 * It intentionally keeps only the pieces needed for EDID/DPCD reads on ARC.
 */

#define INTEL_ARC_MMIO_PIPE_BLOCK_BASE			0x60000
#define INTEL_ARC_MMIO_PIPE_OFFSET				0x1000
#define INTEL_ARC_MMIO_AUX_CH_CTL_A				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4010)
#define INTEL_ARC_MMIO_AUX_CH_DATA1_A			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4014)
#define INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE		0x100
#define INTEL_ARC_MMIO_PIPE_A_HTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0000)
#define INTEL_ARC_MMIO_PIPE_A_HBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0004)
#define INTEL_ARC_MMIO_PIPE_A_HSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PIPE_A_VTOTAL			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x000c)
#define INTEL_ARC_MMIO_PIPE_A_VBLANK			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0010)
#define INTEL_ARC_MMIO_PIPE_A_VSYNC				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0014)
#define INTEL_ARC_MMIO_PIPE_A_SIZE				(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x001c)
#define INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0400)
#define INTEL_ARC_MMIO_DDI_PIPE_A_DATA_M		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0030)
#define INTEL_ARC_MMIO_DDI_PIPE_A_DATA_N		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0034)
#define INTEL_ARC_MMIO_DDI_PIPE_A_LINK_M		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0040)
#define INTEL_ARC_MMIO_DDI_PIPE_A_LINK_N		(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x0044)
#define INTEL_ARC_MMIO_DDI_BUF_CTL_A			(INTEL_ARC_MMIO_PIPE_BLOCK_BASE + 0x4000)
#define INTEL_ARC_MMIO_SNPS_PHY_A_BASE			0x168000
#define INTEL_ARC_MMIO_SNPS_PHY_B_BASE			0x169000
#define INTEL_ARC_MMIO_SNPS_PHY_TX_EQ(base, lane)	((base) + 0x300 + (lane) * 0x10)
#define INTEL_ARC_MMIO_PLANE_BLOCK_BASE			0x70000
#define INTEL_ARC_MMIO_PIPE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0008)
#define INTEL_ARC_MMIO_PLANE_A_CONTROL			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0180)
#define INTEL_ARC_MMIO_PLANE_A_BASE				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0184)
#define INTEL_ARC_MMIO_PLANE_A_STRIDE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0188)
#define INTEL_ARC_MMIO_PLANE_A_POS				(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x018c)
#define INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE		(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x0190)
#define INTEL_ARC_MMIO_PLANE_A_SURFACE			(INTEL_ARC_MMIO_PLANE_BLOCK_BASE + 0x019c)

#define INTEL_ARC_DP_AUX_CTL_BUSY				(1U << 31)
#define INTEL_ARC_DP_AUX_CTL_DONE				(1U << 30)
#define INTEL_ARC_DP_AUX_CTL_INTERRUPT			(1U << 29)
#define INTEL_ARC_DP_AUX_CTL_TIMEOUT_ERROR		(1U << 28)
#define INTEL_ARC_DP_AUX_CTL_TIMEOUT_1600US		(3U << 26)
#define INTEL_ARC_DP_AUX_CTL_RECEIVE_ERROR		(1U << 25)
#define INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT		20
#define INTEL_ARC_DP_AUX_CTL_MSG_SIZE_MASK		(0x1fU << 20)
#define INTEL_ARC_DP_AUX_CTL_FW_SYNC_PULSE_SKL(c) (((c) - 1) << 5)
#define INTEL_ARC_DP_AUX_CTL_SYNC_PULSE_SKL(c)	((c) - 1)
#define INTEL_ARC_DDI_MN_TU_SIZE_MASK			(0x3fU << 25)
#define INTEL_ARC_PIPE_ENABLED					(1U << 31)
#define INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE		(1U << 31)
#define INTEL_ARC_PIPE_DDI_SELECT_MASK			(7U << 28)
#define INTEL_ARC_PIPE_DDI_MODESEL_MASK			(7U << 24)
#define INTEL_ARC_PIPE_DDI_MODE_DP_SST			2U
#define INTEL_ARC_PIPE_DDI_MODE_DP_MST			3U
#define INTEL_ARC_PIPE_DDI_BPC_MASK				(7U << 20)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK		(7U << 1)
#define INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT		1
#define INTEL_ARC_DISPLAY_CONTROL_ENABLED		(1U << 31)
#define INTEL_ARC_DISPLAY_CONTROL_COLOR_MASK_SKY (0x0fU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_CMAP8_SKY		(0x0cU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_RGB16_SKY		(0x0eU << 24)
#define INTEL_ARC_DISPLAY_CONTROL_RGB32_SKY		(0x04U << 24)
#define INTEL_ARC_DDI_BUF_CTL_ENABLE			(1U << 31)
#define INTEL_ARC_DDI_BUF_TRANS_SELECT(n)		((uint32)(n) << 24)
#define INTEL_ARC_DDI_PORT_WIDTH(width)			(((uint32)(width) - 1) << 1)
#define INTEL_ARC_DDI_BUF_IS_IDLE				(1U << 7)
#define INTEL_ARC_DDI_BUF_EMP_MASK				(0xfU << 24)

#define INTEL_ARC_DP_LINK_RATE_810				0x1e
#define INTEL_ARC_DP_LINK_RATE_1350				0x2a
#define INTEL_ARC_DP_LINK_RATE_2000				0x32

#define INTEL_ARC_TGL_DPCLKA_CFGCR0				0x164280
#define INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF		(1U << 24)
#define INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF		(1U << 11)
#define INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF		(1U << 10)
#define INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_SELECT	(3U << 4)
#define INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT	(3U << 2)
#define INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT_SHIFT 2
#define INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_SELECT	(3U << 0)

#define INTEL_ARC_TGL_DPLL0_CFGCR0				0x164284
#define INTEL_ARC_TGL_DPLL1_CFGCR0				0x16428C
#define INTEL_ARC_TGL_DPLL4_CFGCR0				0x164294
#define INTEL_ARC_TGL_DPLL_DCO_FRACTION_SHIFT	10
#define INTEL_ARC_TGL_DPLL0_CFGCR1				0x164288
#define INTEL_ARC_TGL_DPLL1_CFGCR1				0x164290
#define INTEL_ARC_TGL_DPLL4_CFGCR1				0x164298
#define INTEL_ARC_TGL_DPLL_QDIV_RATIO_SHIFT		10
#define INTEL_ARC_TGL_DPLL_QDIV_ENABLE			(1U << 9)
#define INTEL_ARC_TGL_DPLL_KDIV_1				(1U << 6)
#define INTEL_ARC_TGL_DPLL_KDIV_2				(2U << 6)
#define INTEL_ARC_TGL_DPLL_KDIV_3				(4U << 6)
#define INTEL_ARC_TGL_DPLL_PDIV_2				(1U << 2)
#define INTEL_ARC_TGL_DPLL_PDIV_3				(2U << 2)
#define INTEL_ARC_TGL_DPLL_PDIV_5				(4U << 2)
#define INTEL_ARC_TGL_DPLL_PDIV_7				(8U << 2)
#define INTEL_ARC_TGL_DPLL0_ENABLE				0x46010
#define INTEL_ARC_TGL_DPLL1_ENABLE				0x46014
#define INTEL_ARC_TGL_DPLL4_ENABLE				0x46018
#define INTEL_ARC_TGL_DPLL_ENABLE				(1U << 31)
#define INTEL_ARC_TGL_DPLL_LOCK					(1U << 30)
#define INTEL_ARC_TGL_DPLL_POWER_ENABLE			(1U << 27)
#define INTEL_ARC_TGL_DPLL_POWER_STATE			(1U << 26)
#define INTEL_ARC_TGL_DPLL0_SPREAD_SPECTRUM		0x164B10
#define INTEL_ARC_TGL_DPLL1_SPREAD_SPECTRUM		0x164C10
#define INTEL_ARC_TGL_DPLL4_SPREAD_SPECTRUM		0x164E10
#define INTEL_ARC_TGL_DPLL_SSC_ENABLE			(1U << 9)

#define INTEL_ARC_CX0_M2P_CTL_A_LN0			0x64040
#define INTEL_ARC_CX0_M2P_CTL_B_LN0			0x64140
#define INTEL_ARC_CX0_M2P_CTL_USBC1_LN0			0x16F240
#define INTEL_ARC_CX0_M2P_CTL_USBC2_LN0			0x16F440
#define INTEL_ARC_CX0_P2M_STATUS_OFFSET			0x8
#define INTEL_ARC_CX0_TIMER_A_LN0				0x640d8
#define INTEL_ARC_CX0_TIMER_B_LN0				0x641d8
#define INTEL_ARC_CX0_TIMER_USBC1_LN0			0x16f258
#define INTEL_ARC_CX0_TIMER_USBC2_LN0			0x16f458
#define INTEL_ARC_CX0_M2P_TRANSACTION_PENDING		(1U << 31)
#define INTEL_ARC_CX0_M2P_COMMAND_WRITE_UNCOMMITTED	(0x1U << 27)
#define INTEL_ARC_CX0_M2P_COMMAND_WRITE_COMMITTED	(0x2U << 27)
#define INTEL_ARC_CX0_M2P_COMMAND_READ			(0x3U << 27)
#define INTEL_ARC_CX0_M2P_DATA(val)				(((uint32)(val) & 0xff) << 16)
#define INTEL_ARC_CX0_M2P_TRANSACTION_RESET		(1U << 15)
#define INTEL_ARC_CX0_M2P_ADDRESS(val)			((uint32)(val) & 0xfff)
#define INTEL_ARC_CX0_P2M_RESPONSE_READY			(1U << 31)
#define INTEL_ARC_CX0_P2M_COMMAND_READ_ACK		0x4U
#define INTEL_ARC_CX0_P2M_COMMAND_WRITE_ACK		0x5U
#define INTEL_ARC_CX0_P2M_COMMAND_TYPE_MASK		(0xfU << 27)
#define INTEL_ARC_CX0_P2M_DATA_MASK				(0xffU << 16)
#define INTEL_ARC_CX0_P2M_ERROR_SET				(1U << 15)
#define INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS			1000
#define INTEL_ARC_CX0_TIMER_VALUE				0x0000a000

#define INTEL_ARC_C10_VDR_CMN(idx)				(0xC20 + (idx))
#define INTEL_ARC_C10_CMN3_TXVBOOST_MASK		(7U << 5)
#define INTEL_ARC_C10_CMN3_TXVBOOST(val)		(((uint8)(val) & 0x7) << 5)
#define INTEL_ARC_C10_VDR_TX(idx)				(0xC30 + (idx))
#define INTEL_ARC_C10_TX1_TERMCTL_MASK			(7U << 5)
#define INTEL_ARC_C10_TX1_TERMCTL(val)			(((uint8)(val) & 0x7) << 5)
#define INTEL_ARC_C10_VDR_CONTROL(idx)			(0xC70 + (idx) - 1)
#define INTEL_ARC_C10_VDR_CTRL_MSGBUS_ACCESS	(1U << 2)
#define INTEL_ARC_C10_VDR_CTRL_MASTER_LANE		(1U << 1)
#define INTEL_ARC_C10_VDR_CTRL_UPDATE_CFG		(1U << 0)
#define INTEL_ARC_C10_VDR_OVRD					0xD71
#define INTEL_ARC_C10_VDR_OVRD_TX1				(1U << 0)
#define INTEL_ARC_C10_VDR_OVRD_TX2				(1U << 2)
#define INTEL_ARC_C10_VDR_PRE_OVRD_TX1			0xD80
#define INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK		0x3fU
#define INTEL_ARC_C10_PHY_OVRD_LEVEL(val)		((uint8)(val) & 0x3f)
#define INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, control) \
	(INTEL_ARC_C10_VDR_PRE_OVRD_TX1 + (((lane) ^ (tx)) * 0x10) + (control))
#define INTEL_ARC_PHY_C20_VDR_CUSTOM_SERDES_RATE	0xD00
#define INTEL_ARC_PHY_C20_IS_DP					(1U << 6)
#define INTEL_ARC_PHY_C20_DP_RATE(val)			(((uint8)(val) & 0xf) << 1)
#define INTEL_ARC_PHY_C20_CONTEXT_TOGGLE			(1U << 0)
#define INTEL_ARC_PHY_C20_VDR_CUSTOM_WIDTH		0xD02
#define INTEL_ARC_PHY_C20_CUSTOM_WIDTH(val)		((uint8)(val) & 0x3)


struct arc_mit_buf_trans_entry {
	uint8	main;
	uint8	pre;
	uint8	post;
};

// From MIT-licensed upstream intel_ddi_buf_trans.c (_dg2_snps_trans / _dg2_snps_trans_uhbr).
static const arc_mit_buf_trans_entry kDg2SnpsDp14Trans[] = {
	{25, 0, 0}, {32, 0, 6}, {35, 0, 10}, {43, 0, 17}, {35, 0, 0},
	{45, 0, 8}, {48, 0, 14}, {47, 0, 0}, {55, 0, 7}, {62, 0, 0}
};

static const arc_mit_buf_trans_entry kDg2SnpsUhbrTrans[] = {
	{62, 0, 0}, {55, 0, 7}, {50, 0, 12}, {44, 0, 18}, {35, 0, 21},
	{59, 3, 0}, {53, 3, 6}, {48, 3, 11}, {42, 5, 15}, {37, 5, 20},
	{56, 6, 0}, {48, 7, 7}, {45, 7, 10}, {39, 8, 15}, {48, 14, 0},
	{45, 4, 4}
};

// From MIT-licensed upstream intel_ddi_buf_trans.c (_mtl_c10_trans_dp14).
static const arc_mit_buf_trans_entry kMtlC10Dp14Trans[] = {
	{26, 0, 0}, {33, 0, 6}, {38, 0, 11}, {43, 0, 19}, {39, 0, 0},
	{45, 0, 7}, {46, 0, 13}, {46, 0, 0}, {55, 0, 7}, {62, 0, 0}
};

// From MIT-licensed upstream intel_ddi_buf_trans.c (_mtl_c20_trans_dp14 / _mtl_c20_trans_uhbr).
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


static bool read_register(uint32 offset, uint32& value);
static void write_register(uint32 offset, uint32 value);
static status_t wait_for_clear(uint32 offset, uint32 mask, bigtime_t timeout);
static status_t wait_for_set(uint32 offset, uint32 mask, bigtime_t timeout);
static uint32 aux_control_register(uint8 ddiPort);
static uint32 aux_data_register(uint8 ddiPort, uint8 index);
static uint32 pipe_register(uint32 base, int8 pipe);
static status_t apply_dpms_off(void);
static status_t apply_dpms_on(void);
static bool is_mode_in_list(const display_mode& mode, display_mode* match = NULL);
static uint32 bytes_per_pixel_for_space(color_space space);
static uint32 plane_color_format_for_space(color_space space);
static status_t write_dpcd(uint32 address, const void* buffer, size_t size);
static status_t read_dpcd(uint32 address, void* buffer, size_t size);
static uint32 decode_link_rate(uint8 rawLinkRate);
static uint8 encode_link_rate(uint32 linkRate);
static uint8 c20_dp_rate(uint32 linkRate);
static uint8 c20_custom_width(uint32 linkRate);
static bool dp_clock_recovery_ok(const uint8* status, uint32 lanes);
static bool dp_channel_eq_ok(const uint8* status, uint32 lanes);
static void dp_get_adjust_request(const uint8* status, uint8 lane,
	uint8* voltage, uint8* emphasis);
static int dp14_level_index(uint8 voltage, uint8 emphasis);
static status_t apply_snps_phy_levels(uint8 ddiPort, const uint8* laneSettings,
	uint32 lanes, bool uhbr);
static uint32 cx0_port_base(uint8 ddiPort, uint8 lane, bool timer);
static status_t cx0_write(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 data,
	bool committed);
static status_t apply_c20_phy_levels(uint8 ddiPort, uint32 linkRate,
	const uint8* laneSettings, uint32 lanes);
static status_t cx0_rmw(uint8 ddiPort, uint8 laneMask, uint16 addr, uint8 clear,
	uint8 set, bool committed);
static status_t apply_c10_phy_levels(uint8 ddiPort, uint32 linkRate,
	const uint8* laneSettings, uint32 lanes);
static status_t apply_ddi_source_levels(uint8 ddiPort, int8 pipe, uint32 lanes,
	const uint8* laneSettings, uint32 linkRate);
static status_t perform_dp_link_training(uint32 linkRate, uint32 lanes);
static bool compute_displayport_dpll(int* pDiv, int* qDiv, int* kDiv, float* dco);
static status_t program_port_dpll(uint8 ddiPort);
static status_t program_ddi_buffer(uint8 ddiPort, int8 pipe, uint32 lanes, bool enable);
static status_t configure_dp_link(display_mode* mode);
static status_t set_sink_power(uint8 ddiPort, uint8 value);
static status_t read_edid_from_hardware(void);
static status_t read_edid_from_port(uint8 ddiPort, edid1_info& edid);
static status_t read_dpcd_caps_from_port(uint8 ddiPort);
static status_t aux_send_receive(const i2c_bus* bus, uint32 slaveAddress,
	const uint8* writeBuffer, size_t writeLength, uint8* readBuffer,
	size_t readLength);
static ssize_t aux_transfer(uint8 ddiPort, dp_aux_msg* message);
static ssize_t aux_transfer(uint8 ddiPort, uint8* transmitBuffer,
	uint8 transmitSize, uint8* receiveBuffer, uint8 receiveSize);


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
is_mode_supported(display_mode* mode)
{
	return mode != NULL && *mode == gInfo->shared_info->current_mode;
}


static status_t
create_mode_list(void)
{
	display_mode mode = gInfo->shared_info->current_mode;
	const color_space kSupportedSpaces[] = {
		B_RGB32_LITTLE, B_RGB16_LITTLE, B_CMAP8
	};
	if (mode.virtual_width == 0 || mode.virtual_height == 0
		|| mode.space == B_NO_COLOR_SPACE) {
		mode.virtual_width = 1024;
		mode.virtual_height = 768;
		mode.space = B_RGB32;
		compute_display_timing(mode.virtual_width, mode.virtual_height, 60,
			false, &mode.timing);
		if (gInfo->shared_info->bytes_per_row == 0)
			gInfo->shared_info->bytes_per_row = mode.virtual_width * 4;
	} else {
		compute_display_timing(mode.virtual_width, mode.virtual_height, 60,
			false, &mode.timing);
	}
	gInfo->shared_info->current_mode = mode;
	if (gInfo->has_edid) {
		gInfo->mode_list_area = create_display_modes("intel arc modes",
			&gInfo->edid_info, NULL, 0, kSupportedSpaces,
			sizeof(kSupportedSpaces) / sizeof(kSupportedSpaces[0]), NULL,
			&gInfo->mode_list, &gInfo->shared_info->mode_count);
	} else {
		fill_display_mode(mode.virtual_width, mode.virtual_height, &mode);
		gInfo->mode_list_area = create_display_modes("intel arc modes", NULL,
			&mode, 1, kSupportedSpaces,
			sizeof(kSupportedSpaces) / sizeof(kSupportedSpaces[0]),
			is_mode_supported, &gInfo->mode_list, &gInfo->shared_info->mode_count);
	}
	if (gInfo->mode_list_area < B_OK)
		return gInfo->mode_list_area;

	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
	return B_OK;
}


static status_t
init_common(int device, bool isClone)
{
	gInfo = (accelerant_info*)malloc(sizeof(accelerant_info));
	if (gInfo == NULL)
		return B_NO_MEMORY;
	MemoryDeleter infoDeleter(gInfo);

	memset(gInfo, 0, sizeof(accelerant_info));
	gInfo->device = device;
	gInfo->is_clone = isClone;
	gInfo->shared_info_area = -1;
	gInfo->regs_area = -1;
	gInfo->mode_list_area = -1;
	gInfo->frame_buffer_area = -1;
	gInfo->has_edid = false;

	intel_arc_get_private_data data;
	data.magic = INTEL_ARC_PRIVATE_DATA_MAGIC;
	if (ioctl(device, INTEL_ARC_GET_PRIVATE_DATA, &data, sizeof(data)) != 0)
		return B_ERROR;

	AreaDeleter sharedDeleter(clone_area("intel arc shared info",
		(void**)&gInfo->shared_info, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.shared_info_area));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK)
		return status;

	if (gInfo->shared_info->registers_area >= B_OK) {
		AreaDeleter regsDeleter(clone_area("intel arc regs", (void**)&gInfo->registers,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			gInfo->shared_info->registers_area));
		status = gInfo->regs_area = regsDeleter.Get();
		if (status < B_OK)
			return status;
		regsDeleter.Detach();
	}

	infoDeleter.Detach();
	sharedDeleter.Detach();
	return B_OK;
}


static void
uninit_common(void)
{
	if (gInfo->frame_buffer_area >= B_OK)
		delete_area(gInfo->frame_buffer_area);
	if (gInfo->regs_area >= B_OK)
		delete_area(gInfo->regs_area);
	if (gInfo->shared_info_area >= B_OK)
		delete_area(gInfo->shared_info_area);

	if (gInfo->is_clone)
		close(gInfo->device);

	free(gInfo);
	gInfo = NULL;
}


status_t
intel_arc_init_accelerant(int device)
{
	status_t status = init_common(device, false);
	if (status != B_OK)
		return status;

	read_edid_from_hardware();

	status = create_mode_list();
	if (status != B_OK) {
		uninit_common();
		return status;
	}

	area_info info;
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info,
		sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
	}

	return B_OK;
}


ssize_t
intel_arc_accelerant_clone_info_size(void)
{
	return B_PATH_NAME_LENGTH;
}


void
intel_arc_get_accelerant_clone_info(void* info)
{
	ioctl(gInfo->device, INTEL_ARC_GET_DEVICE_NAME, info, B_PATH_NAME_LENGTH);
}


status_t
intel_arc_clone_accelerant(void* data)
{
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), "/dev/%s", (const char*)data);

	int fd = open(path, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK) {
		close(fd);
		return status;
	}

	read_edid_from_hardware();

	status = gInfo->mode_list_area = clone_area("intel arc cloned modes",
		(void**)&gInfo->mode_list, B_ANY_ADDRESS, B_READ_AREA,
		gInfo->shared_info->mode_list_area);
	if (status < B_OK) {
		uninit_common();
		return status;
	}

	area_info info;
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info,
		sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
	}

	return B_OK;
}


void
intel_arc_uninit_accelerant(void)
{
	if (gInfo->mode_list_area >= B_OK)
		delete_area(gInfo->mode_list_area);
	uninit_common();
}


status_t
intel_arc_get_accelerant_device_info(accelerant_device_info* info)
{
	info->version = B_ACCELERANT_VERSION;
	snprintf(info->name, sizeof(info->name), "Intel ARC");
	snprintf(info->chipset, sizeof(info->chipset), "%s",
		gInfo->shared_info->device_identifier);
	snprintf(info->serial_no, sizeof(info->serial_no), "%04x:%04x",
		gInfo->shared_info->vendor_id, gInfo->shared_info->device_id);
	info->memory = gInfo->shared_info->frame_buffer_size > 0xffffffffULL
		? 0xffffffffU : (uint32)gInfo->shared_info->frame_buffer_size;
	info->dac_speed = 0;
	return B_OK;
}


sem_id
intel_arc_accelerant_retrace_semaphore(void)
{
	return -1;
}


uint32
intel_arc_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_OFF;
}


uint32
intel_arc_dpms_mode(void)
{
	return gInfo->shared_info->dpms_mode;
}


status_t
intel_arc_set_dpms_mode(uint32 mode)
{
	switch (mode) {
		case B_DPMS_ON:
			return apply_dpms_on();
		case B_DPMS_OFF:
			return apply_dpms_off();
		default:
			return B_UNSUPPORTED;
	}
}


uint32
intel_arc_accelerant_mode_count(void)
{
	return gInfo->shared_info->mode_count;
}


status_t
intel_arc_get_mode_list(display_mode* modeList)
{
	if (gInfo->shared_info->mode_count == 0)
		return B_ENTRY_NOT_FOUND;

	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
intel_arc_propose_display_mode(display_mode* target, display_mode* low,
	display_mode* high)
{
	(void)low;
	(void)high;

	display_mode match;
	if (!is_mode_in_list(*target, &match))
		return B_BAD_VALUE;

	*target = match;
	return B_OK;
}


status_t
intel_arc_get_preferred_mode(display_mode* mode)
{
	if (gInfo->shared_info->mode_count == 0)
		return B_ENTRY_NOT_FOUND;

	*mode = gInfo->mode_list[0];
	return B_OK;
}


status_t
intel_arc_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	if (*mode == gInfo->shared_info->current_mode)
		return B_OK;

	display_mode target = *mode;
	status_t status = intel_arc_propose_display_mode(&target, &target, &target);
	if (status != B_OK)
		return status;

	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	status = apply_dpms_off();
	if (status != B_OK)
		return status;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 bytesPerPixel
		= bytes_per_pixel_for_space((color_space)target.space);
	if (bytesPerPixel == 0)
		return B_BAD_VALUE;

	gInfo->shared_info->current_mode = target;
	gInfo->shared_info->bytes_per_row = target.virtual_width * bytesPerPixel;
	gInfo->shared_info->pipe_h_total[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->pipe_h_blank[pipe]
		= ((uint32)(target.timing.h_total - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->pipe_h_sync[pipe]
		= ((uint32)(target.timing.h_sync_end - 1) << 16)
			| ((uint32)target.timing.h_sync_start - 1);
	gInfo->shared_info->pipe_v_total[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16)
			| ((uint32)target.timing.v_display - 1);
	gInfo->shared_info->pipe_v_blank[pipe]
		= ((uint32)(target.timing.v_total - 1) << 16)
			| ((uint32)target.timing.v_display - 1);
	gInfo->shared_info->pipe_v_sync[pipe]
		= ((uint32)(target.timing.v_sync_end - 1) << 16)
			| ((uint32)target.timing.v_sync_start - 1);
	gInfo->shared_info->pipe_size[pipe]
		= ((uint32)(target.timing.v_display - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->plane_stride[pipe] = gInfo->shared_info->bytes_per_row;
	gInfo->shared_info->plane_pos[pipe] = 0;
	gInfo->shared_info->plane_image_size[pipe]
		= ((uint32)(target.timing.v_display - 1) << 16)
			| ((uint32)target.timing.h_display - 1);
	gInfo->shared_info->plane_control[pipe]
		= (gInfo->shared_info->plane_control[pipe]
			& ~INTEL_ARC_DISPLAY_CONTROL_COLOR_MASK_SKY)
		| plane_color_format_for_space((color_space)target.space);

	write_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL + pipeOffset,
		gInfo->shared_info->pipe_h_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HBLANK + pipeOffset,
		gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_HSYNC + pipeOffset,
		gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL + pipeOffset,
		gInfo->shared_info->pipe_v_total[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VBLANK + pipeOffset,
		gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_VSYNC + pipeOffset,
		gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(INTEL_ARC_MMIO_PIPE_A_SIZE + pipeOffset,
		gInfo->shared_info->pipe_size[pipe]);

	if ((((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24)
			== INTEL_ARC_PIPE_DDI_MODE_DP_SST)
		|| (((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24)
			== INTEL_ARC_PIPE_DDI_MODE_DP_MST)) {
		status = configure_dp_link(&target);
		if (status != B_OK)
			return status;
	}

	status = apply_dpms_on();
	if (status == B_OK)
		*mode = target;
	return status;
}


status_t
intel_arc_get_display_mode(display_mode* mode)
{
	*mode = gInfo->shared_info->current_mode;
	return B_OK;
}


status_t
intel_arc_get_edid_info(void* info, size_t size, uint32* version)
{
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
	if (gInfo->frame_buffer == NULL)
		return B_UNSUPPORTED;

	config->frame_buffer = gInfo->frame_buffer;
	config->frame_buffer_dma = NULL;
	config->bytes_per_row = gInfo->shared_info->bytes_per_row;
	return B_OK;
}


status_t
intel_arc_get_pixel_clock_limits(display_mode* mode, uint32* low, uint32* high)
{
	uint32 totalPixel = (uint32)mode->timing.h_total
		* (uint32)mode->timing.v_total;
	uint32 clockLimit = 2000000;

	*low = totalPixel * 48L / 1000L;
	if (*low > clockLimit)
		return B_ERROR;

	*high = clockLimit;
	return B_OK;
}


extern "C" void*
get_accelerant_hook(uint32 feature, void* /*data*/)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)intel_arc_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)intel_arc_uninit_accelerant;
		case B_CLONE_ACCELERANT:
			return (void*)intel_arc_clone_accelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)intel_arc_accelerant_clone_info_size;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)intel_arc_get_accelerant_clone_info;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)intel_arc_get_accelerant_device_info;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)intel_arc_accelerant_retrace_semaphore;
		case B_DPMS_CAPABILITIES:
			return (void*)intel_arc_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)intel_arc_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)intel_arc_set_dpms_mode;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)intel_arc_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)intel_arc_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)intel_arc_propose_display_mode;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)intel_arc_get_preferred_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)intel_arc_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)intel_arc_get_display_mode;
		case B_GET_EDID_INFO:
			return (void*)intel_arc_get_edid_info;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)intel_arc_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)intel_arc_get_pixel_clock_limits;
	}

	return NULL;
}


static bool
read_register(uint32 offset, uint32& value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return false;
	}

	value = *(volatile uint32*)(gInfo->registers + offset);
	return true;
}


static void
write_register(uint32 offset, uint32 value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return;
	}

	*(volatile uint32*)(gInfo->registers + offset) = value;
}


static status_t
wait_for_clear(uint32 offset, uint32 mask, bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	uint32 value = 0;
	while (system_time() < deadline) {
		if (!read_register(offset, value))
			return B_ERROR;
		if ((value & mask) == 0)
			return B_OK;
		snooze(100);
	}
	return B_TIMED_OUT;
}


static status_t
wait_for_set(uint32 offset, uint32 mask, bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	uint32 value = 0;
	while (system_time() < deadline) {
		if (!read_register(offset, value))
			return B_ERROR;
		if ((value & mask) == mask)
			return B_OK;
		snooze(100);
	}
	return B_TIMED_OUT;
}


static uint32
pipe_register(uint32 base, int8 pipe)
{
	return base + (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
}


static bool
is_mode_in_list(const display_mode& mode, display_mode* match)
{
	for (uint32 i = 0; i < gInfo->shared_info->mode_count; i++) {
		const display_mode& current = gInfo->mode_list[i];
		if (current.virtual_width != mode.virtual_width
			|| current.virtual_height != mode.virtual_height
			|| current.space != mode.space) {
			continue;
		}

		if (match != NULL)
			*match = current;
		return true;
	}

	return false;
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


static status_t
configure_dp_link(display_mode* mode)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	const uint32 pipeFunc = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
	uint32 bitsPerPixel = 24;
	switch ((pipeFunc & INTEL_ARC_PIPE_DDI_BPC_MASK) >> 20) {
		case 0:
			bitsPerPixel = 24;
			break;
		case 1:
			bitsPerPixel = 30;
			break;
		case 2:
			bitsPerPixel = 18;
			break;
		case 3:
			bitsPerPixel = 36;
			break;
		default:
			break;
	}

	uint32 maxLanes
		= ((pipeFunc & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			>> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1;
	if (gInfo->shared_info->has_dpcd && gInfo->shared_info->dpcd_max_lane_count != 0)
		maxLanes = min_c(maxLanes, (uint32)gInfo->shared_info->dpcd_max_lane_count);
	if (maxLanes == 0 || maxLanes > 4)
		maxLanes = 4;

	uint32 maxLinkRate = 270000;
	if (gInfo->shared_info->has_dpcd && gInfo->shared_info->dpcd_max_link_rate != 0)
		maxLinkRate = decode_link_rate(gInfo->shared_info->dpcd_max_link_rate);

	const uint32 candidateRates[] = {810000, 540000, 270000, 162000};
	const uint32 uhbrCandidateRates[] = {2000000, 1350000, 1000000};
	uint32 linkBandwidth = 0;
	uint32 lanes = 0;
	const uint32 bps = mode->timing.pixel_clock * bitsPerPixel * 21 / 20;
	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_BATTLEMAGE
		&& maxLinkRate > 810000) {
		for (size_t i = 0; i < sizeof(uhbrCandidateRates) / sizeof(uhbrCandidateRates[0]); i++) {
			if (uhbrCandidateRates[i] > maxLinkRate)
				continue;
			for (uint32 candidateLanes = 1; candidateLanes <= 4; candidateLanes <<= 1) {
				if (candidateLanes > maxLanes)
					continue;
				if (bps <= uhbrCandidateRates[i] * candidateLanes * 8) {
					linkBandwidth = uhbrCandidateRates[i];
					lanes = candidateLanes;
					break;
				}
			}
			if (lanes != 0)
				break;
		}
	}
	for (size_t i = 0; i < sizeof(candidateRates) / sizeof(candidateRates[0]); i++) {
		if (lanes != 0)
			break;
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
	if (lanes == 0)
		return B_BAD_VALUE;

	uint64 linkSpeed = (uint64)lanes * linkBandwidth * 8;
	uint64 retN = 1;
	while (retN < linkSpeed)
		retN <<= 1;
	if (retN > 0x800000)
		retN = 0x800000;
	uint64 retM = (uint64)mode->timing.pixel_clock * retN * bitsPerPixel
		/ linkSpeed;
	while (retN > 0xffffff || retM > 0xffffff) {
		retN >>= 1;
		retM >>= 1;
	}
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_M + pipeOffset,
		(uint32)retM | INTEL_ARC_DDI_MN_TU_SIZE_MASK);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_DATA_N + pipeOffset,
		(uint32)retN);

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
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_M + pipeOffset, (uint32)retM);
	write_register(INTEL_ARC_MMIO_DDI_PIPE_A_LINK_N + pipeOffset, (uint32)retN);

	if (gInfo->shared_info->has_dpcd) {
		status_t status = program_port_dpll(gInfo->shared_info->active_ddi_port);
		if (status != B_OK)
			return status;

		status = program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, lanes, true);
		if (status != B_OK)
			return status;

		status = perform_dp_link_training(linkBandwidth, lanes);
		if (status != B_OK)
			return status;

		gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			= (gInfo->shared_info->pipe_ddi_func_ctl[pipe]
				& ~INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			| ((lanes - 1) << INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT);
	}

	return B_OK;
}


static status_t
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


static status_t
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


static void
dp_get_adjust_request(const uint8* status, uint8 lane, uint8* voltage,
	uint8* emphasis)
{
	const uint8 request = status[4 + lane / 2];
	if ((lane & 1) == 0) {
		*voltage = (request & DP_ADJ_VCC_SWING_LANEA_MASK)
			>> DP_ADJ_VCC_SWING_LANEA_SHIFT;
		*emphasis = (request & DP_ADJ_PRE_EMPHASIS_LANEA_MASK)
			>> DP_ADJ_PRE_EMPHASIS_LANEA_SHIFT;
	} else {
		*voltage = (request & DP_ADJ_VCC_SRING_LANEB_MASK)
			>> DP_ADJ_VCC_SWING_LANEB_SHIFT;
		*emphasis = (request & DP_ADJ_PRE_EMPHASIS_LANEB_MASK)
			>> DP_ADJ_PRE_EMPHASIS_LANEB_SHIFT;
	}
}


static uint32
decode_link_rate(uint8 rawLinkRate)
{
	switch (rawLinkRate) {
		case DP_LINK_RATE_162:
			return 162000;
		case DP_LINK_RATE_270:
			return 270000;
		case DP_LINK_RATE_540:
			return 540000;
		case INTEL_ARC_DP_LINK_RATE_810:
			return 810000;
		case INTEL_ARC_DP_LINK_RATE_1350:
			return 1350000;
		case INTEL_ARC_DP_LINK_RATE_2000:
			return 2000000;
		default:
			return 162000;
	}
}


static uint8
encode_link_rate(uint32 linkRate)
{
	switch (linkRate) {
		case 162000:
			return DP_LINK_RATE_162;
		case 270000:
			return DP_LINK_RATE_270;
		case 540000:
			return DP_LINK_RATE_540;
		case 810000:
			return INTEL_ARC_DP_LINK_RATE_810;
		case 1350000:
			return INTEL_ARC_DP_LINK_RATE_1350;
		case 2000000:
			return INTEL_ARC_DP_LINK_RATE_2000;
		default:
			return DP_LINK_RATE_162;
	}
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


static uint8
c20_custom_width(uint32 linkRate)
{
	if (linkRate == 1000000 || linkRate == 1350000 || linkRate == 2000000)
		return 2;
	return 0;
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
	const arc_mit_buf_trans_entry* table = uhbr
		? kDg2SnpsUhbrTrans : kDg2SnpsDp14Trans;
	const size_t tableCount = uhbr ? B_COUNT_OF(kDg2SnpsUhbrTrans)
		: B_COUNT_OF(kDg2SnpsDp14Trans);

	/*
	 * Provisional port-to-PHY mapping for DG2's two SNPS PHY blocks:
	 * DDI B/C -> PHY A, DDI D/E/F/G -> PHY B. This matches the two-PHY
	 * topology exposed by the MIT upstream register definitions and can be
	 * refined later when full port discovery lands.
	 */
	const uint32 phyBase = ddiPort <= 2
		? INTEL_ARC_MMIO_SNPS_PHY_A_BASE : INTEL_ARC_MMIO_SNPS_PHY_B_BASE;

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

		write_register(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY
			| INTEL_ARC_CX0_P2M_ERROR_SET);

		write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING
			| (committed ? INTEL_ARC_CX0_M2P_COMMAND_WRITE_COMMITTED
				: INTEL_ARC_CX0_M2P_COMMAND_WRITE_UNCOMMITTED)
			| INTEL_ARC_CX0_M2P_DATA(data)
			| INTEL_ARC_CX0_M2P_ADDRESS(addr));

		if (wait_for_clear(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING,
				INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
			write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_RESET);
			return B_TIMED_OUT;
		}

		if (committed) {
			uint32 status = 0;
			if (wait_for_set(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY,
					INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
				return B_TIMED_OUT;
			}
			if (!read_register(statusReg, status))
				return B_ERROR;
			if ((status & INTEL_ARC_CX0_P2M_ERROR_SET) != 0
				|| ((status & INTEL_ARC_CX0_P2M_COMMAND_TYPE_MASK) >> 27)
					!= INTEL_ARC_CX0_P2M_COMMAND_WRITE_ACK) {
				return B_ERROR;
			}
		}
	}

	return B_OK;
}


static status_t
perform_dp_link_training(uint32 linkRate, uint32 lanes)
{
	uint8 laneSettings[4] = {0, 0, 0, 0};
	uint8 status[DP_LINK_STATUS_SIZE];

	uint8 value = encode_link_rate(linkRate);
	status_t result = write_dpcd(DP_LINK_RATE, &value, 1);
	if (result != B_OK)
		return result;

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

	for (int attempt = 0; attempt < 5; attempt++) {
		snooze(400);
		result = read_dpcd(DP_LANE_STATUS_0_1, status, DP_LINK_STATUS_SIZE);
		if (result != B_OK)
			return result;
		if (dp_clock_recovery_ok(status, lanes))
			break;

		for (uint32 lane = 0; lane < lanes; lane++) {
			uint8 voltage;
			uint8 emphasis;
			dp_get_adjust_request(status, lane, &voltage, &emphasis);
			laneSettings[lane] = (voltage & 0x3) | ((emphasis & 0x3) << 3);
			if (voltage == 3)
				laneSettings[lane] |= DP_TRAIN_MAX_SWING_EN;
			if (emphasis == 3)
				laneSettings[lane] |= DP_TRAIN_MAX_EMPHASIS_EN;
		}

		result = write_dpcd(DP_TRAINING_LANE0_SET, laneSettings, lanes);
		if (result != B_OK)
			return result;
		result = apply_ddi_source_levels(gInfo->shared_info->active_ddi_port,
			gInfo->shared_info->active_pipe, lanes, laneSettings, linkRate);
		if (result != B_OK)
			return result;

		if (attempt == 4)
			return B_ERROR;
	}

	value = DP_TRAINING_PATTERN_2;
	result = write_dpcd(DP_TRAINING_PATTERN_SET, &value, 1);
	if (result != B_OK)
		return result;

	for (int attempt = 0; attempt < 5; attempt++) {
		snooze(400);
		result = read_dpcd(DP_LANE_STATUS_0_1, status, DP_LINK_STATUS_SIZE);
		if (result != B_OK)
			return result;
		if (dp_channel_eq_ok(status, lanes))
			break;

		for (uint32 lane = 0; lane < lanes; lane++) {
			uint8 voltage;
			uint8 emphasis;
			dp_get_adjust_request(status, lane, &voltage, &emphasis);
			laneSettings[lane] = (voltage & 0x3) | ((emphasis & 0x3) << 3);
			if (voltage == 3)
				laneSettings[lane] |= DP_TRAIN_MAX_SWING_EN;
			if (emphasis == 3)
				laneSettings[lane] |= DP_TRAIN_MAX_EMPHASIS_EN;
		}

		result = write_dpcd(DP_TRAINING_LANE0_SET, laneSettings, lanes);
		if (result != B_OK)
			return result;
		result = apply_ddi_source_levels(gInfo->shared_info->active_ddi_port,
			gInfo->shared_info->active_pipe, lanes, laneSettings, linkRate);
		if (result != B_OK)
			return result;

		if (attempt == 4)
			return B_ERROR;
	}

	value = DP_TRAINING_PATTERN_DISABLE;
	result = write_dpcd(DP_TRAINING_PATTERN_SET, &value, 1);
	return result;
}


static bool
compute_displayport_dpll(int* pDiv, int* qDiv, int* kDiv, float* dco)
{
	// Reinterpreted from TigerLakePLL.cpp / ComputeDisplayPortDpll().
	*pDiv = 3;
	*qDiv = 1;
	*kDiv = 2;
	*dco = 8090;
	return true;
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
		if (wait_for_clear(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING,
				INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
			return B_TIMED_OUT;
		}

		write_register(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY
			| INTEL_ARC_CX0_P2M_ERROR_SET);
		write_register(ctlReg, INTEL_ARC_CX0_M2P_TRANSACTION_PENDING
			| INTEL_ARC_CX0_M2P_COMMAND_READ
			| INTEL_ARC_CX0_M2P_ADDRESS(addr));
		if (wait_for_set(statusReg, INTEL_ARC_CX0_P2M_RESPONSE_READY,
				INTEL_ARC_CX0_MSGBUS_TIMEOUT_MS) != B_OK) {
			return B_TIMED_OUT;
		}
		uint32 status = 0;
		if (!read_register(statusReg, status))
			return B_ERROR;
		if ((status & INTEL_ARC_CX0_P2M_ERROR_SET) != 0
			|| ((status & INTEL_ARC_CX0_P2M_COMMAND_TYPE_MASK) >> 27)
				!= INTEL_ARC_CX0_P2M_COMMAND_READ_ACK) {
			return B_ERROR;
		}
		oldValue = (status & INTEL_ARC_CX0_P2M_DATA_MASK) >> 16;

		status_t result = cx0_write(ddiPort, 1 << lane, addr,
			(oldValue & ~clear) | set, committed);
		if (result != B_OK)
			return result;
	}
	return B_OK;
}


static status_t
apply_c10_phy_levels(uint8 ddiPort, uint32 linkRate, const uint8* laneSettings,
	uint32 lanes)
{
	const uint8 laneMask = lanes > 2 ? 0x3 : 0x1;

	status_t status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CONTROL(1),
		0, INTEL_ARC_C10_VDR_CTRL_MSGBUS_ACCESS, true);
	if (status != B_OK)
		return status;

	const uint8 txVboost = (linkRate == 540000 || linkRate == 810000) ? 5 : 4;
	const uint8 termCtl = (linkRate == 540000 || linkRate == 810000) ? 5 : 2;
	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CMN(3),
		INTEL_ARC_C10_CMN3_TXVBOOST_MASK,
		INTEL_ARC_C10_CMN3_TXVBOOST(txVboost), false);
	if (status != B_OK)
		return status;
	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_TX(1),
		INTEL_ARC_C10_TX1_TERMCTL_MASK,
		INTEL_ARC_C10_TX1_TERMCTL(termCtl), true);
	if (status != B_OK)
		return status;

	for (uint32 ln = 0; ln < lanes; ln++) {
		const uint8 voltage = laneSettings[ln] & 0x3;
		const uint8 emphasis = (laneSettings[ln] >> 3) & 0x3;
		const int level = dp14_level_index(voltage, emphasis);
		if (level < 0 || level >= (int)B_COUNT_OF(kMtlC10Dp14Trans))
			return B_BAD_VALUE;

		const int lane = ln / 2;
		const int tx = ln % 2;
		const uint8 singleLaneMask = lane == 0 ? 0x1 : 0x2;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 0),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].pre), true);
		if (status != B_OK)
			return status;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 1),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].main), true);
		if (status != B_OK)
			return status;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 2),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(kMtlC10Dp14Trans[level].post), true);
		if (status != B_OK)
			return status;
	}

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_OVRD, 0,
		INTEL_ARC_C10_VDR_OVRD_TX1 | INTEL_ARC_C10_VDR_OVRD_TX2, true);
	if (status != B_OK)
		return status;

	return cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_CONTROL(1), 0,
		INTEL_ARC_C10_VDR_CTRL_UPDATE_CFG, true);
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

	status_t status = cx0_rmw(ddiPort, laneMask,
		INTEL_ARC_PHY_C20_VDR_CUSTOM_WIDTH, 0x3,
		INTEL_ARC_PHY_C20_CUSTOM_WIDTH(c20_custom_width(linkRate)), true);
	if (status != B_OK)
		return status;

	// Toggle contexts so the updated UHBR/DP mode configuration is latched.
	status = cx0_rmw(ddiPort, laneMask,
		INTEL_ARC_PHY_C20_VDR_CUSTOM_SERDES_RATE,
		INTEL_ARC_PHY_C20_IS_DP | (0xf << 1),
		INTEL_ARC_PHY_C20_IS_DP | INTEL_ARC_PHY_C20_DP_RATE(c20_dp_rate(linkRate)), true);
	if (status != B_OK)
		return status;

	for (uint32 ln = 0; ln < lanes; ln++) {
		const uint8 voltage = laneSettings[ln] & 0x3;
		const uint8 emphasis = (laneSettings[ln] >> 3) & 0x3;
		const int level = dp14_level_index(voltage, emphasis);
		if (level < 0 || (size_t)level >= tableCount)
			return B_BAD_VALUE;

		const int lane = ln / 2;
		const int tx = ln % 2;
		const uint8 singleLaneMask = lane == 0 ? 0x1 : 0x2;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 0),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].pre), true);
		if (status != B_OK)
			return status;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 1),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].main), true);
		if (status != B_OK)
			return status;
		status = cx0_rmw(ddiPort, singleLaneMask,
			INTEL_ARC_CX0_VDROVRD_CTL(lane, tx, 2),
			INTEL_ARC_C10_PHY_OVRD_LEVEL_MASK,
			INTEL_ARC_C10_PHY_OVRD_LEVEL(table[level].post), true);
		if (status != B_OK)
			return status;
	}

	status = cx0_rmw(ddiPort, laneMask, INTEL_ARC_C10_VDR_OVRD, 0,
		INTEL_ARC_C10_VDR_OVRD_TX1 | INTEL_ARC_C10_VDR_OVRD_TX2, true);
	if (status != B_OK)
		return status;

	return cx0_rmw(ddiPort, laneMask, INTEL_ARC_PHY_C20_VDR_CUSTOM_SERDES_RATE,
		INTEL_ARC_PHY_C20_CONTEXT_TOGGLE, INTEL_ARC_PHY_C20_CONTEXT_TOGGLE, true);
}


static status_t
apply_ddi_source_levels(uint8 ddiPort, int8 pipe, uint32 lanes,
	const uint8* laneSettings, uint32 linkRate)
{
	(void)pipe;
	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_ALCHEMIST)
		return apply_snps_phy_levels(ddiPort, laneSettings, lanes, linkRate > 810000);
	if (gInfo->shared_info->family == INTEL_ARC_FAMILY_BATTLEMAGE)
		return linkRate > 810000
			? apply_c20_phy_levels(ddiPort, linkRate, laneSettings, lanes)
			: apply_c10_phy_levels(ddiPort, linkRate, laneSettings, lanes);
	return B_UNSUPPORTED;
}


static status_t
program_port_dpll(uint8 ddiPort)
{
	if (ddiPort < 1 || ddiPort > 3)
		return B_OK;

	int pDiv, qDiv, kDiv;
	float dco;
	if (!compute_displayport_dpll(&pDiv, &qDiv, &kDiv, &dco))
		return B_ERROR;

	uint32 cfg0 = 0;
	uint32 cfg1 = 0;
	uint32 enable = 0;
	uint32 ssc = 0;
	uint32 dpllIndex = 0;
	switch (ddiPort) {
		case 1:
			dpllIndex = 0;
			cfg0 = INTEL_ARC_TGL_DPLL0_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL0_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL0_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL0_SPREAD_SPECTRUM;
			break;
		case 2:
			dpllIndex = 1;
			cfg0 = INTEL_ARC_TGL_DPLL1_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL1_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL1_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL1_SPREAD_SPECTRUM;
			break;
		case 3:
			dpllIndex = 4;
			cfg0 = INTEL_ARC_TGL_DPLL4_CFGCR0;
			cfg1 = INTEL_ARC_TGL_DPLL4_CFGCR1;
			enable = INTEL_ARC_TGL_DPLL4_ENABLE;
			ssc = INTEL_ARC_TGL_DPLL4_SPREAD_SPECTRUM;
			break;
	}

	uint32 refKHz = 19200;
	uint32 dcoInt = (uint32)floorf(dco / (refKHz / 1000.0f));
	uint32 dcoFrac = (uint32)ceilf((dco / (refKHz / 1000.0f) - dcoInt)
		* (1 << 15));
	uint32 dcoReg = dcoInt | (dcoFrac << INTEL_ARC_TGL_DPLL_DCO_FRACTION_SHIFT);

	uint32 dividers = 0;
	switch (pDiv) {
		case 2: dividers |= INTEL_ARC_TGL_DPLL_PDIV_2; break;
		case 3: dividers |= INTEL_ARC_TGL_DPLL_PDIV_3; break;
		case 5: dividers |= INTEL_ARC_TGL_DPLL_PDIV_5; break;
		case 7: dividers |= INTEL_ARC_TGL_DPLL_PDIV_7; break;
		default: return B_BAD_VALUE;
	}
	switch (kDiv) {
		case 1: dividers |= INTEL_ARC_TGL_DPLL_KDIV_1; break;
		case 2: dividers |= INTEL_ARC_TGL_DPLL_KDIV_2; break;
		case 3: dividers |= INTEL_ARC_TGL_DPLL_KDIV_3; break;
		default: return B_BAD_VALUE;
	}
	if (qDiv != 1)
		dividers |= ((uint32)qDiv << INTEL_ARC_TGL_DPLL_QDIV_RATIO_SHIFT)
			| INTEL_ARC_TGL_DPLL_QDIV_ENABLE;

	uint32 value = 0;
	read_register(enable, value);
	write_register(enable, value & ~INTEL_ARC_TGL_DPLL_ENABLE);
	(void)wait_for_clear(enable, INTEL_ARC_TGL_DPLL_LOCK, 50000);
	read_register(enable, value);
	write_register(enable, value | INTEL_ARC_TGL_DPLL_POWER_ENABLE);
	(void)wait_for_set(enable, INTEL_ARC_TGL_DPLL_POWER_STATE, 50000);
	read_register(ssc, value);
	write_register(ssc, value & ~INTEL_ARC_TGL_DPLL_SSC_ENABLE);
	write_register(cfg0, dcoReg);
	write_register(cfg1, dividers);
	read_register(cfg1, value);
	read_register(enable, value);
	write_register(enable, value | INTEL_ARC_TGL_DPLL_ENABLE);
	(void)wait_for_set(enable, INTEL_ARC_TGL_DPLL_LOCK, 50000);

	read_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
	switch (ddiPort) {
		case 1:
			value |= INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_SELECT;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIA_CLOCK_OFF;
			break;
		case 2:
			value |= INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT;
			value |= 1 << INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_SELECT_SHIFT;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIB_CLOCK_OFF;
			break;
		case 3:
			value |= INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF;
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_SELECT;
			value |= 2 << 4;
			write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
			value &= ~INTEL_ARC_TGL_DPCLKA_DDIC_CLOCK_OFF;
			break;
		default:
			break;
	}
	write_register(INTEL_ARC_TGL_DPCLKA_CFGCR0, value);
	(void)dpllIndex;
	return B_OK;
}


static status_t
program_ddi_buffer(uint8 ddiPort, int8 pipe, uint32 lanes, bool enable)
{
	if (ddiPort > 6)
		return B_BAD_VALUE;

	const uint32 reg = INTEL_ARC_MMIO_DDI_BUF_CTL_A + ddiPort * 0x100;
	uint32 value = 0;
	read_register(reg, value);
	value &= ~INTEL_ARC_DDI_PORT_WIDTH(4);
	value &= ~INTEL_ARC_DDI_BUF_TRANS_SELECT(0x7);
	value |= INTEL_ARC_DDI_BUF_TRANS_SELECT(pipe);
	value |= INTEL_ARC_DDI_PORT_WIDTH(lanes);
	if (enable)
		value |= INTEL_ARC_DDI_BUF_CTL_ENABLE;
	else
		value &= ~INTEL_ARC_DDI_BUF_CTL_ENABLE;
	write_register(reg, value);
	if (ddiPort > 0 && ddiPort <= 4)
		gInfo->shared_info->port_state[ddiPort - 1] = value;
	if (enable)
		(void)wait_for_clear(reg, INTEL_ARC_DDI_BUF_IS_IDLE, 50000);
	return B_OK;
}


static status_t
set_sink_power(uint8 ddiPort, uint8 value)
{
	if (!gInfo->shared_info->has_dpcd || ddiPort == 0)
		return B_OK;

	return write_dpcd(DP_SET_POWER, &value, 1);
}


static status_t
apply_dpms_off(void)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
	const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
	const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);

	write_register(planeControlReg,
		gInfo->shared_info->plane_control[pipe] & ~INTEL_ARC_DISPLAY_CONTROL_ENABLED);
	(void)wait_for_clear(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

	write_register(pipeDdiReg,
		gInfo->shared_info->pipe_ddi_func_ctl[pipe] & ~INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
	(void)wait_for_clear(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);
	(void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, 4, false);

	write_register(pipeControlReg,
		gInfo->shared_info->pipe_control[pipe] & ~INTEL_ARC_PIPE_ENABLED);
	(void)wait_for_clear(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

	(void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D3);
	gInfo->shared_info->dpms_mode = B_DPMS_OFF;
	return B_OK;
}


static status_t
apply_dpms_on(void)
{
	if (gInfo->shared_info->active_pipe < 0)
		return B_UNSUPPORTED;

	const int8 pipe = gInfo->shared_info->active_pipe;

	(void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HTOTAL, pipe),
		gInfo->shared_info->pipe_h_total[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HBLANK, pipe),
		gInfo->shared_info->pipe_h_blank[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_HSYNC, pipe),
		gInfo->shared_info->pipe_h_sync[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VTOTAL, pipe),
		gInfo->shared_info->pipe_v_total[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VBLANK, pipe),
		gInfo->shared_info->pipe_v_blank[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_VSYNC, pipe),
		gInfo->shared_info->pipe_v_sync[pipe]);
	write_register(pipe_register(INTEL_ARC_MMIO_PIPE_A_SIZE, pipe),
		gInfo->shared_info->pipe_size[pipe]);

	const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);
	const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
	const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
	const uint32 planeBaseReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_BASE, pipe);
	const uint32 planeStrideReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_STRIDE, pipe);
	const uint32 planePosReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_POS, pipe);
	const uint32 planeImageReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE, pipe);
	const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);

	write_register(pipeControlReg,
		gInfo->shared_info->pipe_control[pipe] | INTEL_ARC_PIPE_ENABLED);
	(void)wait_for_set(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

	write_register(pipeDdiReg,
		gInfo->shared_info->pipe_ddi_func_ctl[pipe] | INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
	(void)wait_for_set(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);
	(void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe,
		((gInfo->shared_info->pipe_ddi_func_ctl[pipe]
			& INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
			>> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1, true);

	write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
	write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
	write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
	write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeBaseReg, gInfo->shared_info->plane_surface[pipe]);
	write_register(planeControlReg,
		gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
	(void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

	gInfo->shared_info->dpms_mode = B_DPMS_ON;
	return B_OK;
}


static uint32
aux_control_register(uint8 ddiPort)
{
	return INTEL_ARC_MMIO_AUX_CH_CTL_A
		+ ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE;
}


static uint32
aux_data_register(uint8 ddiPort, uint8 index)
{
	return INTEL_ARC_MMIO_AUX_CH_DATA1_A
		+ ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE + index * 4;
}


static status_t
read_edid_from_hardware(void)
{
	if (gInfo->registers == NULL) {
		if (gInfo->shared_info->has_boot_edid) {
			memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid,
				sizeof(edid1_info));
			gInfo->has_edid = true;
			return B_OK;
		}

		gInfo->has_edid = false;
		return B_ENTRY_NOT_FOUND;
	}

	uint8 candidates[7];
	size_t candidateCount = 0;
	if (gInfo->shared_info->active_ddi_port != 0)
		candidates[candidateCount++] = gInfo->shared_info->active_ddi_port;

	for (uint8 port = 1; port <= 3; port++) {
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

	for (uint8 port = 1; port <= 6; port++) {
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
		if (read_edid_from_port(candidates[i], gInfo->edid_info) == B_OK) {
			read_dpcd_caps_from_port(candidates[i]);
			gInfo->has_edid = true;
			return B_OK;
		}
	}
	if (gInfo->shared_info->has_boot_edid) {
		memcpy(&gInfo->edid_info, &gInfo->shared_info->boot_edid,
			sizeof(edid1_info));
		gInfo->has_edid = true;
		return B_OK;
	}

	gInfo->has_edid = false;
	return B_ENTRY_NOT_FOUND;
}


static status_t
read_edid_from_port(uint8 ddiPort, edid1_info& edid)
{
	if (ddiPort > 6)
		return B_BAD_VALUE;

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
	uint8 buffer[8];
	dp_aux_msg message;
	memset(&message, 0, sizeof(message));
	memset(buffer, 0, sizeof(buffer));

	message.address = DP_DPCD_REV;
	message.request = DP_AUX_NATIVE_READ;
	message.buffer = buffer;
	message.size = sizeof(buffer);

	ssize_t result = aux_transfer(ddiPort, &message);
	if (result < (ssize_t)sizeof(buffer))
		return result < B_OK ? (status_t)result : B_ERROR;

	gInfo->shared_info->has_dpcd = true;
	memcpy(gInfo->shared_info->dpcd, buffer, sizeof(buffer));
	gInfo->shared_info->dpcd_revision = buffer[0];
	gInfo->shared_info->dpcd_max_link_rate = buffer[1];
	gInfo->shared_info->dpcd_max_lane_count = buffer[2] & DP_MAX_LANE_COUNT_MASK;
	gInfo->shared_info->dpcd_sink_count = buffer[7] & DP_SINK_COUNT_MASK;
	return B_OK;
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

	transmitBuffer[0] = (message->request << 4)
		| ((message->address >> 16) & 0x0f);
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
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize,
					receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					result = result > 1 ? min_c((ssize_t)receiveBuffer[1],
						(ssize_t)message->size) : (ssize_t)message->size;
				}
				break;

			case DP_AUX_NATIVE_READ:
			case DP_AUX_I2C_READ:
				receiveSize = message->size + 1;
				result = aux_transfer(ddiPort, transmitBuffer, transmitSize,
					receiveBuffer, receiveSize);
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
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 16;
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 8;
			if (i < transmitSize)
				data |= transmitBuffer[i++];
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

	uint8 bytes = (status & INTEL_ARC_DP_AUX_CTL_MSG_SIZE_MASK)
		>> INTEL_ARC_DP_AUX_CTL_MSG_SIZE_SHIFT;
	if (bytes == 0 || bytes > 20)
		return B_BUSY;
	if (bytes > receiveSize)
		bytes = receiveSize;

	for (uint8 i = 0; i < bytes;) {
		uint32 data = 0;
		if (!read_register(aux_data_register(ddiPort, i / 4), data))
			return B_ERROR;
		receiveBuffer[i++] = data >> 24;
		if (i < bytes)
			receiveBuffer[i++] = data >> 16;
		if (i < bytes)
			receiveBuffer[i++] = data >> 8;
		if (i < bytes)
			receiveBuffer[i++] = data;
	}

	return bytes;
}
