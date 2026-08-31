#include <OS.h>
#include "intel_arc.h"
#include "accelerant_protos.h"
#include "accelerant.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)
extern accelerant_info* gInfo;

bool read_register(uint32 offset, uint32& value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return false;
	}

	value = *(volatile uint32*)(gInfo->registers + offset);
	return true;
}

void write_register(uint32 offset, uint32 value)
{
	if (gInfo->registers == NULL
		|| offset + sizeof(uint32) > gInfo->shared_info->registers_size) {
		return;
	}

	*(volatile uint32*)(gInfo->registers + offset) = value;
}


//static status_t
status_t wait_for_clear(uint32 offset, uint32 mask, bigtime_t timeout)
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
	debug_printf("intel_arc.accelerant ERROR: wait_for_clear timed out on reg 0x%08X (mask 0x%08X)\n", offset, mask);
	return B_TIMED_OUT;
}

//static status_t
status_t wait_for_set(uint32 offset, uint32 mask, bigtime_t timeout)
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
	debug_printf("intel_arc.accelerant ERROR: wait_for_set timed out on reg 0x%08X (mask 0x%08X)\n", offset, mask);
	return B_TIMED_OUT;
}

uint32 aux_control_register(uint8 ddiPort)
{
	return INTEL_ARC_MMIO_AUX_CH_CTL_A + ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE;
}

uint32 aux_data_register(uint8 ddiPort, uint8 index)
{
	return INTEL_ARC_MMIO_AUX_CH_DATA1_A + ddiPort * INTEL_ARC_MMIO_AUX_CHANNEL_STRIDE + index * 4;
}


uint32 pipe_register(uint32 base, int8 pipe)
{
	return base + (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
}

uint32
pipe_ddi_decode_bpp(uint32 regValue)
{
    uint32 bpcCode = (regValue & INTEL_ARC_PIPE_DDI_BPC_MASK) >> INTEL_ARC_PIPE_DDI_BPC_SHIFT;
    switch (bpcCode) {
        case 0:  return 24; // 8 bpc
        case 1:  return 30; // 10 bpc
        case 2:  return 18; // 6 bpc
        case 3:  return 36; // 12 bpc
        default: return 24;
    }
}

uint32
pipe_ddi_encode_dp_width(uint32 lanes)
{
    switch (lanes) {
        case 4:  return INTEL_ARC_PIPE_DDI_DP_WIDTH_4;
        case 2:  return INTEL_ARC_PIPE_DDI_DP_WIDTH_2;
        case 1:
        default: return INTEL_ARC_PIPE_DDI_DP_WIDTH_1;
    }
}

uint32
pipe_ddi_decode_dp_width(uint32 regValue)
{
    uint32 widthCode = (regValue & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK) >> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT;
    switch (widthCode) {
        case 3:  return 4;
        case 1:  return 2;
        case 0:
        default: return 1;
    }
}
