/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Mark Watson
	Rudolf Cornelissen 9/2002-5/2003
*/

#define MODULE_BIT 0x02000000

#include "acc_std.h"
#define LOG_PREFIX "[VIA Graphics ACC] "
bool enableGMIDebug = false;

/*
	Return the current display mode.  The only time you might return an
	error is if a mode hasn't been set. Or if the system hands you a NULL pointer.
*/
status_t GET_DISPLAY_MODE(display_mode *current_mode)
{
    /* check for NULL pointer */
    if (current_mode == NULL) {
        if (enableGMIDebug) debug_printf(LOG_PREFIX "GET_DISPLAY_MODE: current mode pointer is NULL!\n");
        return B_ERROR;
    }

    *current_mode = si->dm;

    if (enableGMIDebug) {
        debug_printf(LOG_PREFIX "GET_DISPLAY_MODE: %" B_PRIu16 "x%" B_PRIu16 " (pixel clock: %" B_PRIu32 " kHz, space: 0x%" B_PRIx32 ")\n",
            current_mode->virtual_width, current_mode->virtual_height, current_mode->timing.pixel_clock, current_mode->space);
    }

    return B_OK;
}

/* Return the frame buffer configuration information. */
status_t GET_FRAME_BUFFER_CONFIG(frame_buffer_config *afb)
{
    /* check for NULL pointer */
    if (afb == NULL) {
        if (enableGMIDebug) debug_printf(LOG_PREFIX "GET_FRAME_BUFFER_CONFIG: frame_buffer_config pointer is NULL!\n");
        return B_ERROR;
    }

    *afb = si->fbc;

    if (enableGMIDebug) {
        debug_printf(LOG_PREFIX "GET_FRAME_BUFFER_CONFIG: ptr=%p, bytes_per_row=%" B_PRIu32 "\n",
            afb->frame_buffer, afb->bytes_per_row);
    }

    return B_OK;
}

/* Return the maximum and minium pixelclock limits for the specified mode. */
/* NOTE:
 * Due to BeOS constraints output for all heads will be limited to the head with
 * the least capabilities. */
status_t GET_PIXEL_CLOCK_LIMITS(display_mode *dm, uint32 *low, uint32 *high) 
{
	uint32 max_pclk = 0;
	uint32 min_pclk = 0;

	/* check for NULL pointers */
	if ((dm == NULL) || (low == NULL) || (high == NULL)) {
        if (enableGMIDebug) {
            debug_printf(LOG_PREFIX "GET_PIXEL_CLOCK_LIMITS: NULL parameter detected (dm=%p, low=%p, high=%p)\n",
                dm, low, high);
        }
        return B_ERROR;
    }

	/* specify requested info */
	if (dm->flags & DUALHEAD_BITS)
	{
		/* dualhead mode */
		/* find min. value */
		switch (si->ps.card_type)
		{
			default:
				*low = ((si->ps.min_video_vco * 1000) / 16);
				break;
		}
		/* find max. value:
		 * using decondary DAC specs because they could be narrower (twinview) */
		switch (dm->space)
		{
			case B_CMAP8:
				max_pclk = si->ps.max_dac2_clock_8;
				break;
			case B_RGB15_LITTLE:
			case B_RGB16_LITTLE:
				max_pclk = si->ps.max_dac2_clock_16;
				break;
			case B_RGB24_LITTLE:
				max_pclk = si->ps.max_dac2_clock_24;
				break;
			case B_RGB32_LITTLE:
				/* specially noted because of RAM speed constraints! */
				max_pclk = si->ps.max_dac2_clock_32dh;
				break;
			default:
				/* use fail-safe value */
				max_pclk = si->ps.max_dac2_clock_32dh;
				break;
		}
		/* return values in kHz */
		*high = max_pclk * 1000;
	}
	else
	{
		/* singlehead mode */
		/* find min. value */
		switch (si->ps.card_type)
		{
			default:
				*low = ((si->ps.min_pixel_vco * 1000) / 16);
				break;
		}
		/* find max. value: depends on which head is used as primary head */
		if (!si->ps.crtc2_prim)
		{
			switch (dm->space)
			{
				case B_CMAP8:
					max_pclk = si->ps.max_dac1_clock_8;
					break;
				case B_RGB15_LITTLE:
				case B_RGB16_LITTLE:
					max_pclk = si->ps.max_dac1_clock_16;
					break;
				case B_RGB24_LITTLE:
					max_pclk = si->ps.max_dac1_clock_24;
					break;
				case B_RGB32_LITTLE:
					max_pclk = si->ps.max_dac1_clock_32;
					break;
				default:
					/* use fail-safe value */
					max_pclk = si->ps.max_dac1_clock_32;
					break;
			}
		}
		else
		{
			switch (dm->space)
			{
				case B_CMAP8:
					max_pclk = si->ps.max_dac2_clock_8;
					break;
				case B_RGB15_LITTLE:
				case B_RGB16_LITTLE:
					max_pclk = si->ps.max_dac2_clock_16;
					break;
				case B_RGB24_LITTLE:
					max_pclk = si->ps.max_dac2_clock_24;
					break;
				case B_RGB32_LITTLE:
					max_pclk = si->ps.max_dac2_clock_32;
					break;
				default:
					/* use fail-safe value */
					max_pclk = si->ps.max_dac2_clock_32;
					break;
			}
		}
		/* return values in kHz */
		*high = max_pclk * 1000;
	}

	/* clamp lower limit to 48Hz vertical refresh for now.
	 * Apparantly the BeOS screenprefs app does limit the upper refreshrate to 90Hz,
	 * while it does not limit the lower refreshrate. */
	min_pclk = ((uint32)dm->timing.h_total * (uint32)dm->timing.v_total * 48) / 1000;
	if (min_pclk > *low) *low = min_pclk;
	
	if (enableGMIDebug) {
        debug_printf(LOG_PREFIX "GET_PIXEL_CLOCK_LIMITS: mode=%" B_PRIu16 "x%" B_PRIu16 " (%s), pclk limits = [%" B_PRIu32 " kHz, %" B_PRIu32 " kHz]\n",
            dm->virtual_width, dm->virtual_height,
            (dm->flags & DUALHEAD_BITS) ? "DualHead" : "SingleHead",
            *low, *high);
    }

	return B_OK;
}

/* Return the semaphore id that will be used to signal a vertical sync occured.  */
sem_id ACCELERANT_RETRACE_SEMAPHORE(void)
{
//	return si->vblank;
return B_ERROR;
}
