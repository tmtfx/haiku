/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * The phase coefficient computation was taken from the X driver written by
 * Alan Hourihane and David Dawes.
 */


#include "accelerant.h"
#include "accelerant_protos.h"
#include "commands.h"

#include <Debug.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <AGP.h>


#undef TRACE
//#define TRACE_OVERLAY
#ifdef TRACE_OVERLAY
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define NUM_HORIZONTAL_TAPS		5
#define NUM_VERTICAL_TAPS		3
#define NUM_HORIZONTAL_UV_TAPS	3
#define NUM_VERTICAL_UV_TAPS	3
#define NUM_PHASES				17
#define MAX_TAPS				5

struct phase_coefficient {
	uint8	sign;
	uint8	exponent;
	uint16	mantissa;
};


/*!	Splits the coefficient floating point value into the 3 components
	sign, mantissa, and exponent.
*/
static bool
split_coefficient(double &coefficient, int32 mantissaSize,
	phase_coefficient &splitCoefficient)
{
	double absCoefficient = fabs(coefficient);

	int sign;
	if (coefficient < 0.0)
		sign = 1;
	else
		sign = 0;

	int32 intCoefficient, res;
	int32 maxValue = 1 << mantissaSize;
	res = 12 - mantissaSize;

	if ((intCoefficient = (int)(absCoefficient * 4 * maxValue + 0.5))
			< maxValue) {
		splitCoefficient.exponent = 3;
		splitCoefficient.mantissa = intCoefficient << res;
		coefficient = (double)intCoefficient / (double)(4 * maxValue);
	} else if ((intCoefficient = (int)(absCoefficient * 2 * maxValue + 0.5))
			< maxValue) {
		splitCoefficient.exponent = 2;
		splitCoefficient.mantissa = intCoefficient << res;
		coefficient = (double)intCoefficient / (double)(2 * maxValue);
	} else if ((intCoefficient = (int)(absCoefficient * maxValue + 0.5))
			< maxValue) {
		splitCoefficient.exponent = 1;
		splitCoefficient.mantissa = intCoefficient << res;
		coefficient = (double)intCoefficient / (double)maxValue;
	} else if ((intCoefficient = (int)(absCoefficient * maxValue * 0.5 + 0.5))
			< maxValue) {
		splitCoefficient.exponent = 0;
		splitCoefficient.mantissa = intCoefficient << res;
		coefficient = (double)intCoefficient / (double)(maxValue / 2);
	} else {
		// coefficient out of range
		return false;
	}

	splitCoefficient.sign = sign;
	if (sign)
		coefficient = -coefficient;

	return true;
}


static void __attribute__((unused))
update_coefficients(int32 taps, double filterCutOff, bool horizontal, bool isY,
	phase_coefficient* splitCoefficients)
{
	if (filterCutOff < 1)
		filterCutOff = 1;
	if (filterCutOff > 3)
		filterCutOff = 3;

	bool isVerticalUV = !horizontal && !isY;
	int32 mantissaSize = horizontal ? 7 : 6;

	double rawCoefficients[MAX_TAPS * 32], coefficients[NUM_PHASES][MAX_TAPS];

	int32 num = taps * 16;
	for (int32 i = 0; i < num * 2; i++) {
		double sinc;
		double value = (1.0 / filterCutOff) * taps * M_PI * (i - num)
			/ (2 * num);
		if (value == 0.0)
			sinc = 1.0;
		else
			sinc = sin(value) / value;

		// Hamming window
		double window = (0.5 - 0.5 * cos(i * M_PI / num));
		rawCoefficients[i] = sinc * window;
	}

	for (int32 i = 0; i < NUM_PHASES; i++) {
		// Normalise the coefficients
		double sum = 0.0;
		int32 pos;
		for (int32 j = 0; j < taps; j++) {
			pos = i + j * 32;
			sum += rawCoefficients[pos];
		}
		for (int32 j = 0; j < taps; j++) {
			pos = i + j * 32;
			coefficients[i][j] = rawCoefficients[pos] / sum;
		}

		// split them into sign/mantissa/exponent
		for (int32 j = 0; j < taps; j++) {
			pos = j + i * taps;

			split_coefficient(coefficients[i][j], mantissaSize
				+ (((j == (taps - 1) / 2) && !isVerticalUV) ? 2 : 0),
				splitCoefficients[pos]);
		}

		int32 tapAdjust[MAX_TAPS];
		tapAdjust[0] = (taps - 1) / 2;
		for (int32 j = 1, k = 1; j <= tapAdjust[0]; j++, k++) {
			tapAdjust[k] = tapAdjust[0] - j;
			tapAdjust[++k] = tapAdjust[0] + j;
		}

		// Adjust the coefficients
		sum = 0.0;
		for (int32 j = 0; j < taps; j++) {
			sum += coefficients[i][j];
		}

		if (sum != 1.0) {
			for (int32 k = 0; k < taps; k++) {
				int32 tap2Fix = tapAdjust[k];
				double diff = 1.0 - sum;

				coefficients[i][tap2Fix] += diff;
				pos = tap2Fix + i * taps;

				split_coefficient(coefficients[i][tap2Fix], mantissaSize
					+ (((tap2Fix == (taps - 1) / 2) && !isVerticalUV) ? 2 : 0),
					splitCoefficients[pos]);

				sum = 0.0;
				for (int32 j = 0; j < taps; j++) {
					sum += coefficients[i][j];
				}
				if (sum == 1.0)
					break;
			}
		}
	}
}


static void
set_color_key(uint8 red, uint8 green, uint8 blue, uint8 redMask,
	uint8 greenMask, uint8 blueMask)
{
	overlay_registers* registers = gInfo->overlay_registers;

	registers->color_key_red = red;
	registers->color_key_green = green;
	registers->color_key_blue = blue;
	registers->color_key_mask_red = ~redMask;
	registers->color_key_mask_green = ~greenMask;
	registers->color_key_mask_blue = ~blueMask;
	registers->color_key_enabled = true;
	
	debug_printf("Intel OVERLAY DBG: HW Regs -> Color RGB(%u,%u,%u) Mask RGB(0x%02x,0x%02x,0x%02x)\n",
        registers->color_key_red, registers->color_key_green, registers->color_key_blue,
        registers->color_key_mask_red, registers->color_key_mask_green, registers->color_key_mask_blue);
}


static void
set_color_key(const overlay_window* window)
{
	switch (gInfo->shared_info->current_mode.space) {
		case B_CMAP8:
			set_color_key(0, 0, window->blue.value, 0x0, 0x0, 0xff);
			break;
		case B_RGB15:
			set_color_key(window->red.value << 3, window->green.value << 3,
				window->blue.value << 3, window->red.mask << 3,
				window->green.mask << 3, window->blue.mask << 3);
			break;
		case B_RGB16:
			set_color_key(window->red.value << 3, window->green.value << 2,
				window->blue.value << 3, window->red.mask << 3,
				window->green.mask << 2, window->blue.mask << 3);
			break;

		default:
			set_color_key(window->red.value, window->green.value,
				window->blue.value, window->red.mask, window->green.mask, window->blue.mask);//0x00, 0xff, 0x00);
			debug_printf("Intel OVERLAY DBG: set_color_kay on window COLORKEY R=%u G=%u B=%u | Mask R=0x%x G=0x%x B=0x%x\n",
    			window->red.value, window->green.value, window->blue.value,
				window->red.mask, window->green.mask, window->blue.mask);
			break;
	}
}

/*
static void
update_overlay(bool updateCoefficients)
{
	if (!gInfo->shared_info->overlay_active
		|| gInfo->shared_info->device_type.IsModel(INTEL_MODEL_965))
		return;

//	QueueCommands queue(gInfo->shared_info->primary_ring_buffer);
//	queue.PutFlush();
//	queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);
//	queue.PutOverlayFlip(COMMAND_OVERLAY_CONTINUE, updateCoefficients);
//
//	// make sure the flip is done now
//	queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);
//	queue.PutFlush();

	asm volatile("mfence" ::: "memory");
	QueueCommands queue(gInfo->shared_info->primary_ring_buffer);
    queue.PutFlush();
    queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);
    //Invia l'update del flip con i coefficienti aggiornati
    queue.PutOverlayFlip(COMMAND_OVERLAY_CONTINUE, updateCoefficients);
    queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);
    queue.PutFlush();
	TRACE("%s: UP: %lx, TST: %lx, ST: %lx, CMD: %lx (%lx), ERR: %lx\n",
		__func__, read32(INTEL_OVERLAY_UPDATE),
		read32(INTEL_OVERLAY_TEST), read32(INTEL_OVERLAY_STATUS),
		*(((uint32*)gInfo->overlay_registers) + 0x68/4), read32(0x30168),
		read32(0x2024));
	debug_printf("Intel OVERLAY DBG: UPDATE_REG UP=0x%" B_PRIx32 " TST=0x%" B_PRIx32 " ST=0x%" B_PRIx32 "\n",
    read32(INTEL_OVERLAY_UPDATE),
    read32(INTEL_OVERLAY_TEST),
    read32(INTEL_OVERLAY_STATUS));
}*/
static void
update_overlay(bool updateCoefficients)
{
    if (!gInfo->shared_info->overlay_active
        || gInfo->shared_info->device_type.IsModel(INTEL_MODEL_965))
        return;

    asm volatile("mfence" ::: "memory");
    QueueCommands queue(gInfo->shared_info->primary_ring_buffer);
    
    // 1. Inviamo solo il Flip
    queue.PutOverlayFlip(COMMAND_OVERLAY_CONTINUE, updateCoefficients);
    
    // 2. Attendiamo che l'overlay completi e poi facciamo il flush
    queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);
    queue.PutFlush();

    debug_printf("Intel OVERLAY DBG: UPDATE_REG UP=0x%" B_PRIx32 " TST=0x%" B_PRIx32 " ST=0x%" B_PRIx32 "\n",
        read32(INTEL_OVERLAY_UPDATE),
        read32(INTEL_OVERLAY_TEST),
        read32(INTEL_OVERLAY_STATUS));
}


static void
show_overlay(void)
{
	if (gInfo->shared_info->overlay_active
		|| gInfo->shared_info->device_type.IsModel(INTEL_MODEL_965))
		return;

	gInfo->shared_info->overlay_active = true;
	gInfo->overlay_registers->overlay_enabled = true;

	QueueCommands queue(gInfo->shared_info->primary_ring_buffer);
	queue.PutOverlayFlip(COMMAND_OVERLAY_ON, true);
	queue.PutFlush();

	TRACE("%s: UP: %lx, TST: %lx, ST: %lx, CMD: %lx (%lx), ERR: %lx\n",
		__func__, read32(INTEL_OVERLAY_UPDATE),
		read32(INTEL_OVERLAY_TEST), read32(INTEL_OVERLAY_STATUS),
		*(((uint32*)gInfo->overlay_registers) + 0x68/4),
		read32(0x30168), read32(0x2024));
	debug_printf("Intel OVERLAY DBG: SHOW UP: %x, TST: %x, ST: %x, CMD: %x (%x), ERR: %x\n", 
		read32(INTEL_OVERLAY_UPDATE),
		read32(INTEL_OVERLAY_TEST), read32(INTEL_OVERLAY_STATUS),
		*(((uint32*)gInfo->overlay_registers) + 0x68/4),
		read32(0x30168), read32(0x2024));
}


static void
hide_overlay(void)
{
	if (!gInfo->shared_info->overlay_active
		|| gInfo->shared_info->device_type.IsModel(INTEL_MODEL_965))
		return;

	overlay_registers* registers = gInfo->overlay_registers;

	gInfo->shared_info->overlay_active = false;
	registers->overlay_enabled = false;

	QueueCommands queue(gInfo->shared_info->primary_ring_buffer);

	// flush pending commands
	queue.PutFlush();
	queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);

	// clear overlay enabled bit
	queue.PutOverlayFlip(COMMAND_OVERLAY_CONTINUE, false);
	queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);

	// turn off overlay engine
	queue.PutOverlayFlip(COMMAND_OVERLAY_OFF, false);
	queue.PutWaitFor(COMMAND_WAIT_FOR_OVERLAY_FLIP);

	gInfo->current_overlay = NULL;
}


//	#pragma mark -


uint32
legacy_overlay_count(const display_mode* mode)
{
	debug_printf("Intel extreme OVERLAY: Entro in intel_overlay_count ritorno 5\n");
	// TODO: make this depending on the amount of RAM and the screen mode
	// (and we could even have more than one when using 3D as well)
	return 5;
}


const uint32*
legacy_overlay_supported_spaces(const display_mode* mode)
{
	debug_printf("Intel extreme OVERLAY: CALLED intel_overlay_supported_spaces\n");

	static const uint32 kSupportedSpaces[] = {B_RGB15, B_RGB16, B_RGB32,
		B_YCbCr422, 0};
	static const uint32 kSupportedi965Spaces[] = {B_YCbCr422, 0};
	intel_shared_info &sharedInfo = *gInfo->shared_info;

	if (sharedInfo.device_type.InGroup(INTEL_GROUP_96x))
		return kSupportedi965Spaces;

	return kSupportedSpaces;
}


uint32
legacy_overlay_supported_features(uint32 colorSpace)
{
	debug_printf("Intel extreme OVERLAY: CALLED intel_overlay_supported_features\n");

	return B_OVERLAY_COLOR_KEY
		| B_OVERLAY_HORIZONTAL_FILTERING
		| B_OVERLAY_VERTICAL_FILTERING
		| B_OVERLAY_HORIZONTAL_MIRRORING;
}


const overlay_buffer* 
legacy_allocate_overlay_buffer(color_space colorSpace, uint16 width,
	uint16 height)
{
	debug_printf("Intel extreme OVERLAY: CALLED intel_allocate_overlay_buffer\n");

	TRACE("%s(width %u, height %u, colorSpace %lu)\n", __func__, width,
		height, colorSpace);

	intel_shared_info &sharedInfo = *gInfo->shared_info;
	uint32 bytesPerPixel;

	switch (colorSpace) {
		case B_RGB15:
			bytesPerPixel = 2;
			break;
		case B_RGB16:
			bytesPerPixel = 2;
			break;
		case B_RGB32:
			bytesPerPixel = 4;
			break;
		case B_YCbCr422:
			bytesPerPixel = 2;
			break;
		default:
			return NULL;
	}

	struct overlay* overlay = (struct overlay*)malloc(sizeof(struct overlay));
	if (overlay == NULL)
		return NULL;

	// TODO: locking!

	// alloc graphics mem

	int32 alignment = 0x3f;
	if (sharedInfo.device_type.IsModel(INTEL_MODEL_965))
		alignment = 0xff;

	overlay_buffer* buffer = &overlay->buffer;
	buffer->space = colorSpace;
	buffer->width = width;
	buffer->height = height;
	buffer->bytes_per_row = (width * bytesPerPixel + alignment) & ~alignment;
	

	status_t status = intel_allocate_memory(buffer->bytes_per_row * height,
		0, overlay->buffer_base);
	if (status < B_OK) {
		free(overlay);
		return NULL;
	}
	
	debug_printf("=== Intel OVERLAY ALLOC ===\n");
    debug_printf("  -> space=0x%" B_PRIx32 " w=%" B_PRIu16 " h=%" B_PRIu16 "\n", colorSpace, width, height);
    debug_printf("  -> bytes_per_row = %" B_PRIu32 "\n", buffer->bytes_per_row);
    debug_printf("  -> raw overlay->buffer_offset = 0x%" B_PRIx32 "\n", overlay->buffer_offset);

	if (sharedInfo.device_type.IsModel(INTEL_MODEL_965)) {
		status = intel_allocate_memory(INTEL_i965_OVERLAY_STATE_SIZE,
			B_APERTURE_NON_RESERVED, overlay->state_base);
		if (status < B_OK) {
			intel_free_memory(overlay->buffer_base);
			free(overlay);
			return NULL;
		}

		overlay->state_offset = overlay->state_base
			- (addr_t)gInfo->shared_info->graphics_memory;
	}

	overlay->buffer_offset = overlay->buffer_base
		- (addr_t)gInfo->shared_info->graphics_memory;

	buffer->buffer = (uint8*)overlay->buffer_base;
	buffer->buffer_dma = (uint8*)gInfo->shared_info->physical_graphics_memory
		+ overlay->buffer_offset;

	//TRACE("%s: base=%x, offset=%x, address=%x, physical address=%x\n",
	//	__func__, overlay->buffer_base, overlay->buffer_offset,
	//	buffer->buffer, buffer->buffer_dma);
	struct overlay* intelOverlay = (struct overlay*)buffer;
	
	debug_printf("Intel OVERLAY ALLOC: space=0x%" B_PRIx32 " w=%" B_PRIu16 " h=%" B_PRIu16
        " -> buffer_offset=0x%" B_PRIx32 " (bytes_per_row=%" B_PRIu32 ")\n",
        colorSpace, width, height, intelOverlay->buffer_offset, buffer->bytes_per_row);

	return buffer;
}


status_t
legacy_release_overlay_buffer(const overlay_buffer* buffer)
{
	debug_printf("Intel extreme OVERLAY: CALLED intel_release_overlay_buffer...\n");
	struct overlay* overlay = (struct overlay*)buffer;

	// TODO: locking!

	if (gInfo->current_overlay == overlay)
		hide_overlay();

	intel_free_memory(overlay->buffer_base);
	if (gInfo->shared_info->device_type.IsModel(INTEL_MODEL_965))
		intel_free_memory(overlay->state_base);
	free(overlay);

	return B_OK;
}


status_t
legacy_get_overlay_constraints(const display_mode* mode,
	const overlay_buffer* buffer, overlay_constraints* constraints)
{
	debug_printf("Intel extreme OVERLAY: CALLED intel_get_overlay_constraints...\n");
	// taken from the Radeon driver...

	// scaler input restrictions
	// TODO: check all these values; most of them are probably too restrictive

	// position
	constraints->view.h_alignment = 0;
	constraints->view.v_alignment = 0;

	// alignment
	switch (buffer->space) {
		case B_RGB15:
			constraints->view.width_alignment = 7;
			break;
		case B_RGB16:
			constraints->view.width_alignment = 7;
			break;
		case B_RGB32:
			constraints->view.width_alignment = 3;
			break;
		case B_YCbCr422:
			constraints->view.width_alignment = 7;
			break;
		case B_YUV12:
			constraints->view.width_alignment = 7;
			break;
		default:
			return B_BAD_VALUE;
	}
	constraints->view.height_alignment = 0;

	// size
	constraints->view.width.min = 4;		// make 4-tap filter happy
	constraints->view.height.min = 4;
	constraints->view.width.max = buffer->width;
	constraints->view.height.max = buffer->height;

	// scaler output restrictions
	constraints->window.h_alignment = 0;
	constraints->window.v_alignment = 0;
	constraints->window.width_alignment = 0;
	constraints->window.height_alignment = 0;
	constraints->window.width.min = 2;
	constraints->window.width.max = mode->virtual_width;
	constraints->window.height.min = 2;
	constraints->window.height.max = mode->virtual_height;

	// TODO: the minimum values are not tested
	constraints->h_scale.min = 1.0f / (1 << 4);
	constraints->h_scale.max = buffer->width * 7;
	constraints->v_scale.min = 1.0f / (1 << 4);
	constraints->v_scale.max = buffer->height * 7;

	return B_OK;
}


overlay_token
legacy_allocate_overlay(void)
{
	// we only have a single overlay channel
	if (atomic_or(&gInfo->shared_info->overlay_channel_used, 1) != 0)
		return NULL;

	return (overlay_token)++gInfo->shared_info->overlay_token;
}


status_t
legacy_release_overlay(overlay_token overlayToken)
{
	// we only have a single token, which simplifies this
	if (overlayToken != (overlay_token)gInfo->shared_info->overlay_token)
		return B_BAD_VALUE;
	
	memset(&gInfo->last_overlay_view, 0, sizeof(overlay_view));
    memset(&gInfo->last_overlay_window, 0, sizeof(overlay_window));
    gInfo->last_vertical_overlay_scale = 0;
    gInfo->last_horizontal_overlay_scale = 0;

	atomic_and(&gInfo->shared_info->overlay_channel_used, 0);

	return B_OK;
}

static status_t
validate_overlay_registers()
{
    if (gInfo->shared_info->overlay_offset == 0) {
        debug_printf("=== Intel OVERLAY MAP ERROR: overlay_offset is 0 in shared_info! ===\n");
        gInfo->overlay_registers = NULL;
        return B_ERROR;
    }

    // Aritmetica forzata in BYTE tramite addr_t
    addr_t userBase = (addr_t)gInfo->shared_info->graphics_memory;
    uint32 offset = gInfo->shared_info->overlay_offset;

    gInfo->overlay_registers = (struct overlay_registers*)(userBase + offset);

    debug_printf("=== Intel OVERLAY MAP CHECK ===\n");
    debug_printf("  -> graphics_memory (User Base) = 0x%" B_PRIxADDR "\n", userBase);
    debug_printf("  -> overlay_offset              = 0x%" B_PRIx32 "\n", offset);
    debug_printf("  -> overlay_registers (Calculated)= %p\n", gInfo->overlay_registers);

    return B_OK;
}
/* senza scrittura diretta registri
status_t
intel_configure_overlay(overlay_token overlayToken,
    const overlay_buffer* buffer, const overlay_window* window,
    const overlay_view* view)
{
    if (overlayToken != (overlay_token)gInfo->shared_info->overlay_token)
        return B_BAD_VALUE;

    if (window == NULL || view == NULL || window->width == 0 || window->height == 0 
        || view->width == 0 || view->height == 0) {
        debug_printf("Intel OVERLAY: window/view nullo o con dimensioni 0!\n");
        hide_overlay();
        return B_OK;
    }
    
    if (validate_overlay_registers() != B_OK)
        return B_ERROR;

    debug_printf("Intel OVERLAY IN: win(x=%d, y=%d, w=%u, h=%u) view(x=%d, y=%d, w=%u, h=%u)\n",
        window->h_start, window->v_start, window->width, window->height,
        view->h_start, view->v_start, view->width, view->height);

    struct overlay* overlay = (struct overlay*)buffer;
    overlay_registers* registers = gInfo->overlay_registers;
    intel_shared_info &sharedInfo = *gInfo->shared_info;
    bool updateCoefficients = false;
    uint32 bytesPerPixel = 2;

    switch (buffer->space) {
        case B_RGB15:
            registers->source_format = OVERLAY_FORMAT_RGB15;
            bytesPerPixel = 2;
            break;
        case B_RGB16:
            registers->source_format = OVERLAY_FORMAT_RGB16;
            bytesPerPixel = 2;
            break;
        case B_RGB32:
            registers->source_format = OVERLAY_FORMAT_RGB32;
            bytesPerPixel = 4;
            break;
        case B_YCbCr422:
            registers->source_format = OVERLAY_FORMAT_YCbCr422;
            bytesPerPixel = 2;
            break;
    }
    
    if (!gInfo->shared_info->overlay_active) {
        memset(&gInfo->last_overlay_view, 0, sizeof(overlay_view));
        memset(&gInfo->last_overlay_window, 0, sizeof(overlay_window));
    }

    // Controllo se i parametri di vista/finestra sono cambiati
    bool mustRecalculate = 
           gInfo->last_overlay_view.width    != view->width
        || gInfo->last_overlay_view.height   != view->height
        || gInfo->last_overlay_view.h_start  != view->h_start
        || gInfo->last_overlay_view.v_start  != view->v_start
        || gInfo->last_overlay_window.width  != window->width
        || gInfo->last_overlay_window.height != window->height
        || gInfo->last_overlay_window.h_start!= window->h_start
        || gInfo->last_overlay_window.v_start!= window->v_start;

    // Se è la prima volta o se la geometria non era stata salvata, ricalcola
    if (mustRecalculate || gInfo->overlay_source_width == 0) {
        int32 left = window->h_start;
        int32 right = window->h_start + window->width;
        int32 top = window->v_start;
        int32 bottom = window->v_start + window->height;

        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > sharedInfo.current_mode.timing.h_display)
            right = sharedInfo.current_mode.timing.h_display;
        if (bottom > sharedInfo.current_mode.timing.v_display)
            bottom = sharedInfo.current_mode.timing.v_display;

        if (left >= right || top >= bottom) {
            hide_overlay();
            return B_OK;
        }

        gInfo->overlay_window_left = left;
        gInfo->overlay_window_top = top;
        gInfo->overlay_window_width = right - left;
        gInfo->overlay_window_height = bottom - top;

        uint32 horizontalScale = (view->width << 12) / window->width;
        uint32 verticalScale = (view->height << 12) / window->height;
        uint32 horizontalScaleUV = horizontalScale >> 1;
        uint32 verticalScaleUV = verticalScale >> 1;
        horizontalScale = horizontalScaleUV << 1;
        verticalScale = verticalScaleUV << 1;

        left = view->h_start - (int32)((window->h_start - left)
            * (horizontalScale / 4096.0) + 0.5);
        top = view->v_start - (int32)((window->v_start - top)
            * (verticalScale / 4096.0) + 0.5);
        right = view->h_start + view->width;
        bottom = view->v_start + view->height;

        gInfo->overlay_position_buffer_offset = buffer->bytes_per_row * top
            + left * bytesPerPixel;

        gInfo->overlay_source_width = right - left;
        gInfo->overlay_source_height = bottom - top;

        if (gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_8xx)) {
            gInfo->overlay_source_bytes_per_row = (((overlay->buffer_offset
                + (view->width << 1) + 0x1f) >> 5)
                - (overlay->buffer_offset >> 5) - 1) << 2;
        } else {
            int yaddress = overlay->buffer_offset;
            int yswidth = view->width << 1;
            gInfo->overlay_source_bytes_per_row = (((((yaddress
                + yswidth + 0x3f) >> 6) - (yaddress >> 6)) << 1) - 1) << 2;
        }

        // Programmazione registri di Scaling
        registers->scale_rgb.horizontal_downscale_factor = horizontalScale >> 12;
        registers->scale_rgb.horizontal_scale_fraction = horizontalScale & 0xfff;
        registers->scale_uv.horizontal_downscale_factor = horizontalScaleUV >> 12;
        registers->scale_uv.horizontal_scale_fraction = horizontalScaleUV & 0xfff;

        registers->scale_rgb.vertical_scale_fraction = verticalScale & 0xfff;
        registers->scale_uv.vertical_scale_fraction = verticalScaleUV & 0xfff;
        registers->vertical_scale_rgb = verticalScale >> 12;
        registers->vertical_scale_uv = verticalScaleUV >> 12;

        if (verticalScale != gInfo->last_vertical_overlay_scale
            || horizontalScale != gInfo->last_horizontal_overlay_scale) {
            updateCoefficients = true;

            phase_coefficient coefficients[NUM_HORIZONTAL_TAPS * NUM_PHASES];
            update_coefficients(NUM_HORIZONTAL_TAPS, horizontalScale / 4096.0,
                true, true, coefficients);

            phase_coefficient coefficientsUV[NUM_HORIZONTAL_UV_TAPS * NUM_PHASES];
            update_coefficients(NUM_HORIZONTAL_UV_TAPS,
                horizontalScaleUV / 4096.0, true, false, coefficientsUV);

            int32 pos = 0;
            for (int32 i = 0; i < NUM_PHASES; i++) {
                for (int32 j = 0; j < NUM_HORIZONTAL_TAPS; j++) {
                    registers->horizontal_coefficients_rgb[pos]
                        = coefficients[pos].sign << 15
                            | coefficients[pos].exponent << 12
                            | coefficients[pos].mantissa;
                    pos++;
                }
            }

            pos = 0;
            for (int32 i = 0; i < NUM_PHASES; i++) {
                for (int32 j = 0; j < NUM_HORIZONTAL_UV_TAPS; j++) {
                    registers->horizontal_coefficients_uv[pos]
                        = coefficientsUV[pos].sign << 15
                            | coefficientsUV[pos].exponent << 12
                            | coefficientsUV[pos].mantissa;
                    pos++;
                }
            }

            gInfo->last_vertical_overlay_scale = verticalScale;
            gInfo->last_horizontal_overlay_scale = horizontalScale;
        }

        gInfo->last_overlay_view = *view;
        gInfo->last_overlay_window = *window;
    }

    // Scrittura costante dei registri hardware (Eseguita AD OGNI FRAME)
    registers->window_left = gInfo->overlay_window_left;
    registers->window_top = gInfo->overlay_window_top;
    registers->window_width = gInfo->overlay_window_width;
    registers->window_height = gInfo->overlay_window_height;

    registers->source_width_rgb = gInfo->overlay_source_width;
    registers->source_height_rgb = gInfo->overlay_source_height;
    registers->source_bytes_per_row_rgb = gInfo->overlay_source_bytes_per_row;

    registers->color_control_output_mode = true;
    registers->select_pipe = 0;

    // L'OFFSET DEL BUFFER DEVE ESSERE RICALCOLATO DINAMICAMENTE SU OGNI BUFFER DIVERSO
    uint32 finalOffset = overlay->buffer_offset + gInfo->overlay_position_buffer_offset;
    registers->buffer_rgb0 = finalOffset;
    registers->stride_rgb  = buffer->bytes_per_row;

    registers->mirroring_mode
        = (window->flags & B_OVERLAY_HORIZONTAL_MIRRORING) != 0
            ? OVERLAY_MIRROR_HORIZONTAL : OVERLAY_MIRROR_NORMAL;
    registers->ycbcr422_order = 0;

    // Log di verifica prima dell'update
    debug_printf("=== Intel OVERLAY CONFIGURE ===\n");
    debug_printf("  -> RAW overlay->buffer_offset = 0x%" B_PRIx32 "\n", overlay->buffer_offset);
    debug_printf("  -> position_buffer_offset     = 0x%" B_PRIx32 "\n", gInfo->overlay_position_buffer_offset);
    debug_printf("  -> FINAL REG buffer_rgb0      = 0x%" B_PRIx32 "\n", registers->buffer_rgb0);
    debug_printf("  -> REG stride_rgb             = %" B_PRIu32 "\n", registers->stride_rgb);
    debug_printf("  -> REG src (W x H)            = %" B_PRIu32 " x %" B_PRIu32 "\n", 
        registers->source_width_rgb, registers->source_height_rgb);
    debug_printf("  -> REG win (X, Y, W, H)       = %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32 "\n",
        registers->window_left, registers->window_top, registers->window_width, registers->window_height);

    if (!gInfo->shared_info->overlay_active) {
        set_color_key(window);
        show_overlay();
    } else {
        update_overlay(updateCoefficients);
    }

    gInfo->current_overlay = overlay;
    return B_OK;
}*/
status_t
legacy_configure_overlay(overlay_token overlayToken,
    const overlay_buffer* buffer, const overlay_window* window,
    const overlay_view* view)
{
    if (overlayToken != (overlay_token)gInfo->shared_info->overlay_token)
        return B_BAD_VALUE;

    if (buffer == NULL || window == NULL || view == NULL 
        || window->width == 0 || window->height == 0 
        || view->width == 0 || view->height == 0) {
        debug_printf("Intel OVERLAY: window/view nullo o dimensioni 0!\n");
        hide_overlay();
        return B_OK;
    }
    
    if (validate_overlay_registers() != B_OK)
        return B_ERROR;

    struct overlay* overlay = (struct overlay*)buffer;
    overlay_registers* registers = gInfo->overlay_registers;
    //intel_shared_info &sharedInfo = *gInfo->shared_info;

    // 1. FORMATO COLORE
    uint32 bytesPerPixel = 2;
    switch (buffer->space) {
        case B_RGB15:
            registers->source_format = OVERLAY_FORMAT_RGB15;
            bytesPerPixel = 2;
            break;
        case B_RGB16:
            registers->source_format = OVERLAY_FORMAT_RGB16;
            bytesPerPixel = 2;
            break;
        case B_RGB32:
            registers->source_format = OVERLAY_FORMAT_RGB32;
            bytesPerPixel = 4;
            break;
        case B_YCbCr422:
        default:
            registers->source_format = OVERLAY_FORMAT_YCbCr422;
            bytesPerPixel = 2;
            break;
    }

    // 2. DIMENSIONI E POSITION
    registers->window_left   = window->h_start;
    registers->window_top    = window->v_start;
    registers->window_width  = window->width;
    registers->window_height = window->height;

    registers->source_width_rgb  = view->width;
    registers->source_height_rgb = view->height;
    
    // 3. STRIDE E PITCH
    registers->stride_rgb = buffer->bytes_per_row;
    
    int yaddress = overlay->buffer_offset;
    int yswidth = view->width * bytesPerPixel;
    registers->source_bytes_per_row_rgb = (((((yaddress + yswidth + 0x3f) >> 6) 
        - (yaddress >> 6)) << 1) - 1) << 2;

    // 4. BUFFER OFFSET (SENZA CORRUZIONI PUNTATORE!)
    registers->buffer_rgb0 = overlay->buffer_offset;

    // 5. CONTROL BITS
    registers->color_control_output_mode = true;
    registers->select_pipe = 0;
    registers->overlay_enabled = true; // Imposta correttamente l'enable nella struct

    // 6. SCALING FACTORS
    uint32 horizontalScale = (view->width << 12) / window->width;
    uint32 verticalScale = (view->height << 12) / window->height;
    uint32 horizontalScaleUV = horizontalScale >> 1;
    uint32 verticalScaleUV = verticalScale >> 1;

    registers->scale_rgb.horizontal_downscale_factor = horizontalScale >> 12;
    registers->scale_rgb.horizontal_scale_fraction   = horizontalScale & 0xfff;
    registers->scale_uv.horizontal_downscale_factor  = horizontalScaleUV >> 12;
    registers->scale_uv.horizontal_scale_fraction    = horizontalScaleUV & 0xfff;

    registers->scale_rgb.vertical_scale_fraction = verticalScale & 0xfff;
    registers->scale_uv.vertical_scale_fraction  = verticalScaleUV & 0xfff;
    registers->vertical_scale_rgb = verticalScale >> 12;
    registers->vertical_scale_uv  = verticalScaleUV >> 12;

    registers->mirroring_mode = (window->flags & B_OVERLAY_HORIZONTAL_MIRRORING) != 0
        ? OVERLAY_MIRROR_HORIZONTAL : OVERLAY_MIRROR_NORMAL;
    registers->ycbcr422_order = 0;

    // Log pulito
    debug_printf("=== Intel OVERLAY CONFIGURE ===\n");
    debug_printf("  -> RAW overlay->buffer_offset = 0x%" B_PRIx32 "\n", overlay->buffer_offset);
    debug_printf("  -> FINAL REG buffer_rgb0      = 0x%" B_PRIx32 "\n", registers->buffer_rgb0);
    debug_printf("  -> REG stride_rgb             = %" B_PRIu32 "\n", registers->stride_rgb);
    debug_printf("  -> REG src (W x H)            = %" B_PRIu32 " x %" B_PRIu32 "\n", 
        registers->source_width_rgb, registers->source_height_rgb);
    debug_printf("  -> REG win (X, Y, W, H)       = %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32 "\n",
        registers->window_left, registers->window_top, registers->window_width, registers->window_height);

    bool updateCoefficients = false;
    if (!gInfo->shared_info->overlay_active) {
        set_color_key(window);
        show_overlay();
    } else {
        update_overlay(updateCoefficients);
    }

    gInfo->current_overlay = overlay;
    return B_OK;
}
