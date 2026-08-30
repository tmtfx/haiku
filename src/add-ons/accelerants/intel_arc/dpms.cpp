#include <OS.h>

#include "accelerant_protos.h"
#include "accelerant.h"

#include <dp.h>
#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)
extern accelerant_info* gInfo;

static status_t
set_sink_power(uint8 ddiPort, uint8 value)
{
	if (!gInfo->shared_info->has_dpcd || ddiPort == 0)
		return B_OK;

	debug_printf("intel_arc.accelerant: set_sink_power(ddiPort=%u, value=0x%02x)\n", ddiPort, value);
	return write_dpcd(DP_SET_POWER, &value, 1);
}

status_t
apply_dpms_off(void)
{
    debug_printf("intel_arc.accelerant: apply_dpms_off() entering\n");
    if (gInfo->shared_info->active_pipe < 0)
        return B_UNSUPPORTED;

    const int8 pipe = gInfo->shared_info->active_pipe;
    const uint32 planeControlReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_CONTROL, pipe);
    const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);
    const uint32 pipeDdiReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL, pipe);
    const uint32 pipeControlReg = pipe_register(INTEL_ARC_MMIO_PIPE_A_CONTROL, pipe);

    // 1. Disabilita il Piano e forza il latch con la scrittura di SURFACE
    write_register(planeControlReg,
        gInfo->shared_info->plane_control[pipe] & ~INTEL_ARC_DISPLAY_CONTROL_ENABLED);
    write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);
    (void)wait_for_clear(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

    // 2. Disabilita la funzione DDI del Transcoder e il Buffer PHY
    write_register(pipeDdiReg,
        gInfo->shared_info->pipe_ddi_func_ctl[pipe] & ~INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
    (void)wait_for_clear(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);
    (void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, 4, false);

    // 3. Disabilita la Pipe
    write_register(pipeControlReg,
        gInfo->shared_info->pipe_control[pipe] & ~INTEL_ARC_PIPE_ENABLED);
    (void)wait_for_clear(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

    (void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D3);
    gInfo->shared_info->dpms_mode = B_DPMS_OFF;
    return B_OK;
}
status_t
apply_dpms_on(void)
{
    debug_printf("intel_arc.accelerant: apply_dpms_on() entering\n");
    if (gInfo->shared_info->active_pipe < 0)
        return B_UNSUPPORTED;

    const int8 pipe = gInfo->shared_info->active_pipe;

    (void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

    // 1. Scrittura Timing della Pipe
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
    const uint32 planeStrideReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_STRIDE, pipe);
    const uint32 planePosReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_POS, pipe);
    const uint32 planeImageReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE, pipe);
    const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);

    // 2. Determina le lane e costruisci DDI_FUNC_CTL in modo pulito
    //uint8 lanes = gInfo->shared_info->dp_lanes[pipe];
    //if (lanes == 0) lanes = 1;
    uint32 ddiFuncCtl = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
    uint8 lanes = 4; // HDMI richiede sempre 4 lane (3 dati + 1 clock)
    const uint32 modeSel = (ddiFuncCtl & INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
    //uint32 modeSel = (gInfo->shared_info->pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODE_MASK);
    if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
	//invece di ricalcolarlo usiamo il valore salvato nella shared_info
	lanes = gInfo->shared_info->dp_lanes[pipe];
	if (lanes == 0) {
		
		/*
		uint32 width = (ddiFuncCtl >> 19) & 0x7;
		if (width == 0) lanes = 1;
		else if (width == 1) lanes = 2;
		else if (width == 3) lanes = 4;
		*/
		lanes = pipe_ddi_decode_dp_width(ddiFuncCtl);
		debug_printf("intel_arc.accelerant: dpms ricalcolo lanes: %d\n",lanes);
		}
    }
  
    //uint32 ddiFuncCtl = (gInfo->shared_info->active_ddi_port << 28) | (INTEL_ARC_PIPE_DDI_MODE_DP_SST << 24);
    
    // BPC 8 bit = (0 << 16)
    //uint32 widthCode = (lanes == 4) ? 3 : ((lanes == 2) ? 1 : 0);
    //ddiFuncCtl |= (widthCode << 19);

    // 3. PASSO FONDAMENTALE 1: Abilita Prima il Transcoder DDI
    write_register(pipeDdiReg, ddiFuncCtl | INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
    (void)wait_for_set(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);

    // 4. PASSO FONDAMENTALE 2: Abilita Successivamente la Pipe
    write_register(pipeControlReg,
        gInfo->shared_info->pipe_control[pipe] | INTEL_ARC_PIPE_ENABLED);
    (void)wait_for_set(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

    // 5. Programma DDI Buffer PHY
    debug_printf("intel_arc.accelerant: dpms, programming ddi buffer with active_ddi_port %d, pipe %d, lanes %d\n",
        gInfo->shared_info->active_ddi_port, pipe, lanes);
    (void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, lanes, true);

    // 6. Configura e Abilita il Piano Graphics
    write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
    write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
    write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
    write_register(planeControlReg,
        gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
    
    write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);

    (void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

    gInfo->shared_info->dpms_mode = B_DPMS_ON;
    debug_printf("intel_arc.accelerant: apply_dpms_on() finished successfully\n");
    return B_OK;
}
/* sequenza temporale sbagliata?
status_t
apply_dpms_on(void)
{
    debug_printf("intel_arc.accelerant: apply_dpms_on() entering\n");
    if (gInfo->shared_info->active_pipe < 0)
        return B_UNSUPPORTED;

    const int8 pipe = gInfo->shared_info->active_pipe;

    (void)set_sink_power(gInfo->shared_info->active_ddi_port, DP_SET_POWER_D0);

    // 1. Timing della Pipe
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
    const uint32 planeStrideReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_STRIDE, pipe);
    const uint32 planePosReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_POS, pipe);
    const uint32 planeImageReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_IMAGE_SIZE, pipe);
    const uint32 planeSurfaceReg = pipe_register(INTEL_ARC_MMIO_PLANE_A_SURFACE, pipe);

    // 2. Abilita Pipe
    write_register(pipeControlReg,
        gInfo->shared_info->pipe_control[pipe] | INTEL_ARC_PIPE_ENABLED);
    (void)wait_for_set(pipeControlReg, INTEL_ARC_PIPE_ENABLED, 20000);

    // 3. Abilita Transcoder DDI
    const uint32 ddiFuncCtl = gInfo->shared_info->pipe_ddi_func_ctl[pipe];
    write_register(pipeDdiReg, ddiFuncCtl | INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE);
    (void)wait_for_set(pipeDdiReg, INTEL_ARC_PIPE_DDI_FUNC_CTL_ENABLE, 20000);

    // 4. Determina le Lane PHY: 4 fisse per HDMI/TMDS, dinamiche per DisplayPort
    uint8 lanes = 4; // HDMI richiede sempre 4 lane (3 dati + 1 clock)
    const uint32 modeSel = (ddiFuncCtl & INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
    //if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
    //    lanes = ((ddiFuncCtl & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK)
    //        >> INTEL_ARC_PIPE_DDI_DP_WIDTH_SHIFT) + 1;
    //}
    if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
	//invece di ricalcolarlo usiamo il valore salvato nella shared_info
	lanes = gInfo->shared_info->dp_lanes[pipe];
	if (lanes == 0) {
		debug_printf("intel_arc.accelerant: dpms ricalcolo lanes\n");
		uint32 width = (ddiFuncCtl >> 1) & 0x7;
		if (width == 0) lanes = 1;
		else if (width == 1) lanes = 2;
		else if (width == 3) lanes = 4;
		}
    }
    debug_printf("intel_arc.accelerant: dpms, programming ddi buffer with active_ddi_port %d, pipe %d, lanes %d\n",gInfo->shared_info->active_ddi_port, pipe, lanes);
    (void)program_ddi_buffer(gInfo->shared_info->active_ddi_port, pipe, lanes, true);

    // 5. Configura e Abilita il Piano (SURFACE va scritto PER ULTIMO come trigger di Latch)
    write_register(planeStrideReg, gInfo->shared_info->plane_stride[pipe]);
    write_register(planePosReg, gInfo->shared_info->plane_pos[pipe]);
    write_register(planeImageReg, gInfo->shared_info->plane_image_size[pipe]);
    write_register(planeControlReg,
        gInfo->shared_info->plane_control[pipe] | INTEL_ARC_DISPLAY_CONTROL_ENABLED);
    
    // La scrittura del registro SURFACE esegue l'arm/latch hardware dei registri del piano
    write_register(planeSurfaceReg, gInfo->shared_info->plane_surface[pipe]);

    (void)wait_for_set(planeControlReg, INTEL_ARC_DISPLAY_CONTROL_ENABLED, 20000);

    gInfo->shared_info->dpms_mode = B_DPMS_ON;
    debug_printf("intel_arc.accelerant: apply_dpms_on() finished successfully\n");
    return B_OK;
}*/

uint32
intel_arc_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_OFF;
}

uint32
intel_arc_dpms_mode(void)
{
	(void)handle_hotplug_event();
	return gInfo->shared_info->dpms_mode;
}

status_t
intel_arc_set_dpms_mode(uint32 mode)
{
	debug_printf("intel_arc.accelerant: intel_arc_set_dpms_mode(mode=%u)\n", mode);
	(void)handle_hotplug_event();
	if (gInfo->shared_info->dpms_mode == mode) {
		debug_printf("intel_arc.accelerant: DPMS already in requested state %u, skipping\n",
			mode);
		return B_OK;
	}
	switch (mode) {
		case B_DPMS_ON:
			return apply_dpms_on();
		case B_DPMS_OFF:
			return apply_dpms_off();
		default:
			return B_UNSUPPORTED;
	}
}
