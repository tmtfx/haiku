#include "accelerant_protos.h"
#include "accelerant.h"
#include <string.h>

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)

extern accelerant_info* gInfo;

static int32 sOverlayChannelUsed = 0;
static int32 sOverlayToken = 0;
enum { INTEL_ARC_MAX_OVERLAY_BUFFERS = 8 };

static intel_arc_overlay_state sOverlayState = {};
static intel_arc_overlay_buffer sOverlayBuffers[INTEL_ARC_MAX_OVERLAY_BUFFERS];



static uint32
overlay_plane_register(uint32 base, int8 pipe)
{
	return base + (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
}

status_t
init_overlay_memory_manager(void)
{
	debug_printf("intel_arc.accelerant: init_overlay_memory_manager(frame_buffer=%p, mode_list=%p, current=%ux%u)\n",
		gInfo != NULL ? gInfo->frame_buffer : NULL,
		gInfo != NULL ? gInfo->mode_list : NULL,
		gInfo != NULL && gInfo->shared_info != NULL ? gInfo->shared_info->current_mode.virtual_width : 0,
		gInfo != NULL && gInfo->shared_info != NULL ? gInfo->shared_info->current_mode.virtual_height : 0);
	if (gInfo == NULL || gInfo->shared_info == NULL || gInfo->frame_buffer == NULL){
		debug_printf("intel_arc.accelerant OVERLAY: ERROR no shared_info or frame_buffer\n");
		return B_NO_INIT;
	}
	if (gInfo->overlay_mem_mgr != NULL){
		debug_printf("intel_arc.accelerant OVERLAY: memory manager already instantiated\n");
		return B_OK;
	}

	uint32 maxWidth = gInfo->shared_info->current_mode.virtual_width;
	uint32 maxHeight = gInfo->shared_info->current_mode.virtual_height;
	for (uint32 i = 0; gInfo->mode_list != NULL
		&& i < gInfo->shared_info->mode_count; i++) {
		maxWidth = max_c(maxWidth, gInfo->mode_list[i].virtual_width);
		maxHeight = max_c(maxHeight, gInfo->mode_list[i].virtual_height);
	}

	const uint32 reserveBytesPerRow = (maxWidth * 4 + 63) & ~63;
	const uint64 reserveSize = (uint64)gInfo->shared_info->frame_buffer_offset
		+ (uint64)reserveBytesPerRow * maxHeight;
	uint32 heapStart = ((uint32)reserveSize + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);
	if (heapStart >= gInfo->shared_info->frame_buffer_size){
		debug_printf("intel_arc.accelerant OVERLAY: ERROR no memory left for our overlay manager\n");
		return B_NO_MEMORY;
	}

	uint32 heapSize = (uint32)min_c(
		gInfo->shared_info->frame_buffer_size - heapStart,
		(uint64)0xffffffffU);
	if (heapSize < 4096) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR not enough (4096) memory for you overlay manager\n");
		return B_NO_MEMORY;
	}

	gInfo->overlay_mem_mgr = mem_init("intel_arc_overlay_vram", heapStart,
		heapSize, 64, 128);
	if (gInfo->overlay_mem_mgr == NULL) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR memory manager not initialized\n");
		return B_NO_MEMORY;
	}

	memset(sOverlayBuffers, 0, sizeof(sOverlayBuffers));
	debug_printf("intel_arc.accelerant: overlay VRAM heap start=0x%08" B_PRIx32
		", size=%" B_PRIu32 "\n", heapStart, heapSize);
	return B_OK;
}

status_t
intel_arc_configure_overlay(overlay_token token, const overlay_buffer* buffer,
	const overlay_window* window, const overlay_view* view)
{
	debug_printf("intel_arc.accelerant: configure_overlay(token=%p, buffer=%p, window=%p, view=%p)\n",
		token, buffer, window, view);
	if (token == NULL || token != sOverlayState.token) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR no token or incorrect\n");
		return B_BAD_VALUE;
	}

	if (buffer == NULL) {
		const int8 pipe = gInfo->shared_info->active_pipe;
		if (pipe >= 0) {
			const uint32 planeCtlReg = overlay_plane_register(
				INTEL_ARC_MMIO_PLANE_B_CONTROL, pipe);
			const uint32 planeSurfReg = overlay_plane_register(
				INTEL_ARC_MMIO_PLANE_B_SURFACE, pipe);
			uint32 control = 0;
			(void)read_register(planeCtlReg, control);
			debug_printf("intel_arc.accelerant: configure_overlay disable plane pipe=%" B_PRId8
				" ctlReg=0x%08" B_PRIx32 " oldCtl=0x%08" B_PRIx32 "\n",
				pipe, planeCtlReg, control);
			write_register(planeCtlReg, control & ~INTEL_ARC_DISPLAY_CONTROL_ENABLED);
			write_register(planeSurfReg, 0);
		}
		sOverlayState.buffer = NULL;
		sOverlayState.configured = false;
		debug_printf("intel_arc.accelerant OVERLAY: cannot configure overlay with null buffer\n");
		return B_OK;
	}

	if (window == NULL || view == NULL) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR window or view null\n");
		return B_BAD_VALUE;
	}

	debug_printf("intel_arc.accelerant: overlay params space=0x%08" B_PRIx32
		" bpr=%" B_PRIu32 " dma=%p win=[%d,%d %ux%u off LTRB=%u,%u,%u,%u flags=0x%08" B_PRIx32
		"] view=[%u,%u %ux%u]\n",
		buffer->space, buffer->bytes_per_row, buffer->buffer_dma,
		window->h_start, window->v_start, window->width, window->height,
		window->offset_left, window->offset_top, window->offset_right,
		window->offset_bottom, window->flags,
		view->h_start, view->v_start, view->width, view->height);

	const uint32 supportedFeatures
		= intel_arc_overlay_supported_features(buffer->space);
	if ((window->flags & ~supportedFeatures) != 0) {
		debug_printf("intel_arc.accelerant: configure_overlay rejected flags 0x%08" B_PRIx32
			" supported=0x%08" B_PRIx32 "\n", window->flags, supportedFeatures);
		return B_BAD_VALUE;
	}

	display_mode currentMode;
	status_t status = intel_arc_get_display_mode(&currentMode);
	if (status != B_OK) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR on intel_arc_get_display_mode\n");
		return status;
	}

	overlay_constraints constraints;
	status = intel_arc_get_overlay_constraints(&currentMode, buffer, &constraints);
	if (status != B_OK) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR on intel_arc_get_overlay_constraints\n");
		return status;
	}

	if (view->width < constraints.view.width.min
		|| view->width > constraints.view.width.max
		|| view->height < constraints.view.height.min
		|| view->height > constraints.view.height.max
		|| window->width < constraints.window.width.min
		|| window->width > constraints.window.width.max
		|| window->height < constraints.window.height.min
		|| window->height > constraints.window.height.max) {
		debug_printf("intel_arc.accelerant: configure_overlay rejected by constraints\n");
		return B_BAD_VALUE;
	}

	sOverlayState.buffer = buffer;
	sOverlayState.window = *window;
	sOverlayState.view = *view;
	sOverlayState.configured = true;

	const int8 pipe = gInfo->shared_info->active_pipe;
	if (pipe < 0) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR pipe less than 0\n");
		return B_UNSUPPORTED;
	}

	uint32 planeCtl = INTEL_ARC_DISPLAY_CONTROL_ENABLED | INTEL_ARC_PLANE_LINEAR;
	uint32 colorCtl = 0;
	switch (buffer->space) {
		case B_YCbCr422:
			planeCtl |= INTEL_ARC_PLANE_CTL_FORMAT_YUV422
				| INTEL_ARC_PLANE_CTL_YUV422_ORDER_YUYV;
			colorCtl |= INTEL_ARC_PLANE_CTL_COLOR_KEY_ALPHA_ENABLE
				| INTEL_ARC_PLANE_COLOR_CSC_MODE_YUV601_TO_RGB601;
			break;
		case B_RGB32:
			planeCtl |= INTEL_ARC_PLANE_CTL_FORMAT_XRGB_8888;
			break;
		default:
			debug_printf("intel_arc.accelerant: configure_overlay unsupported programmed space=0x%08" B_PRIx32 "\n",
				buffer->space);
			return B_UNSUPPORTED;
	}

	if ((window->flags & B_OVERLAY_COLOR_KEY) != 0) {
		planeCtl |= INTEL_ARC_PLANE_CTL_KEY_ENABLE_DESTINATION;
	}

	const uint32 planeCtlReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_CONTROL, pipe);
	const uint32 planeStrideReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_STRIDE, pipe);
	const uint32 planePosReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_POS, pipe);
	const uint32 planeSizeReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_IMAGE_SIZE, pipe);
	const uint32 planeKeyValReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_KEYVAL, pipe);
	const uint32 planeKeyMskReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_KEYMSK, pipe);
	const uint32 planeKeyMaxReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_KEYMAX, pipe);
	const uint32 planeSurfReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_SURFACE, pipe);
	const uint32 planeOffsetReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_OFFSET, pipe);
	const uint32 planeColorCtlReg = overlay_plane_register(INTEL_ARC_MMIO_PLANE_B_COLOR_CTL, pipe);

	const uint32 pos = ((uint32)(window->v_start & 0xffff) << 16)
		| (uint16)window->h_start;
	const uint32 sizeReg = ((uint32)(window->height - 1) << 16)
		| (uint32)(window->width - 1);
	const uint32 keyVal = ((uint32)window->red.value << 16)
		| ((uint32)window->green.value << 8)
		| (uint32)window->blue.value;
	const uint32 keyMask = ((uint32)window->red.mask << 16)
		| ((uint32)window->green.mask << 8)
		| (uint32)window->blue.mask;

	write_register(planeStrideReg, buffer->bytes_per_row / 64);
	write_register(planePosReg, pos);
	write_register(planeSizeReg, sizeReg);
	write_register(planeOffsetReg,
		((uint32)view->v_start << 16) | (uint32)view->h_start);
	write_register(planeKeyValReg, keyVal);
	write_register(planeKeyMskReg, keyMask);
	write_register(planeKeyMaxReg, 0);
	write_register(planeColorCtlReg, colorCtl);
	write_register(planeCtlReg, planeCtl);
	write_register(planeSurfReg, (uint32)(addr_t)buffer->buffer_dma);
	uint32 verifyCtl = 0;
	uint32 verifySurf = 0;
	uint32 verifyStride = 0;
	(void)read_register(planeCtlReg, verifyCtl);
	(void)read_register(planeSurfReg, verifySurf);
	(void)read_register(planeStrideReg, verifyStride);

	debug_printf("intel_arc.accelerant: configure_overlay token=%p space=0x%08" B_PRIx32
		" view=%ux%u window=%ux%u flags=0x%08" B_PRIx32
		" planeCtl=0x%08" B_PRIx32 " surface=0x%08" B_PRIx32
		" verifyCtl=0x%08" B_PRIx32 " verifySurf=0x%08" B_PRIx32
		" verifyStride=0x%08" B_PRIx32 "\n",
		token, buffer->space, view->width, view->height, window->width,
		window->height, window->flags, planeCtl,
		(uint32)(addr_t)buffer->buffer_dma, verifyCtl, verifySurf, verifyStride);
	return B_OK;
}

uint32
intel_arc_overlay_count(const display_mode* mode)
{
	debug_printf("intel_arc.accelerant: overlay_count(mode=%p", mode);
	if (mode != NULL) {
		debug_printf(", %ux%u space=0x%08" B_PRIx32,
			mode->virtual_width, mode->virtual_height, mode->space);
	}
	debug_printf(") -> 4\n");
	return 4;
}


const uint32*
intel_arc_overlay_supported_spaces(const display_mode* mode)
{
	static const uint32 kSupportedSpaces[] = {
		B_YCbCr422,
		B_RGB32,
		B_YCbCr420,
		0
	};
	//debug_printf("intel_arc.accelerant: overlay_supported_spaces(mode=%p", mode);
	//if (mode != NULL) {
	//	debug_printf(", %ux%u space=0x%08" B_PRIx32,
	//		mode->virtual_width, mode->virtual_height, mode->space);
	//}
	debug_printf("intel_arc.accelerant: overlay_supported_spaces [0x%08" B_PRIx32 ", 0x%08" B_PRIx32 ", 0x%08" B_PRIx32 "]\n",
		kSupportedSpaces[0], kSupportedSpaces[1], kSupportedSpaces[2]);
	//debug_printf(") -> [0x%08" B_PRIx32 ", 0x%08" B_PRIx32 ", 0x%08" B_PRIx32 "]\n",
	//	kSupportedSpaces[0], kSupportedSpaces[1], kSupportedSpaces[2]);
	return kSupportedSpaces;
}

uint32
intel_arc_overlay_supported_features(uint32 colorSpace)
{
	uint32 features = 0;
	switch (colorSpace) {
		case B_YCbCr422:
		case B_YCbCr420:
		case B_RGB32:
			features = B_OVERLAY_COLOR_KEY
				| B_OVERLAY_HORIZONTAL_FILTERING
				| B_OVERLAY_VERTICAL_FILTERING;
			break;
		default:
			break;
	}
	debug_printf("intel_arc.accelerant: overlay_supported_features(space=0x%08" B_PRIx32
		") -> 0x%08" B_PRIx32 "\n", colorSpace, features);
	return features;
}

overlay_buffer*
intel_arc_allocate_overlay_buffer(color_space colorSpace, uint16 width, uint16 height)
{
	debug_printf("intel_arc.accelerant: allocate_overlay_buffer(space=0x%08" B_PRIx32
		", width=%u, height=%u)\n", (uint32)colorSpace, width, height);
	if (width == 0 || height == 0) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR cannot allocate buffer if width or height are 0\n");
		return NULL;
	}
	if (gInfo->overlay_mem_mgr == NULL && init_overlay_memory_manager() != B_OK) {
		debug_printf("intel_arc.accelerant: allocate_overlay_buffer failed: overlay_mem_mgr unavailable\n");
		return NULL;
	}

	size_t bytesPerRow = 0;
	size_t size = 0;
	switch (colorSpace) {
		case B_YCbCr422:
			bytesPerRow = ((size_t)width * 2 + 63) & ~63;
			size = bytesPerRow * height;
			break;
		case B_RGB32:
			bytesPerRow = ((size_t)width * 4 + 63) & ~63;
			size = bytesPerRow * height;
			break;
		case B_YCbCr420:
			bytesPerRow = ((size_t)width + 63) & ~63;
			size = bytesPerRow * height + ((bytesPerRow * height) / 2);
			break;
		default:
			debug_printf("intel_arc.accelerant: allocate_overlay_buffer unsupported space 0x%08" B_PRIx32 "\n",
				(uint32)colorSpace);
			return NULL;
	}

	int slot = -1;
	for (int i = 0; i < INTEL_ARC_MAX_OVERLAY_BUFFERS; i++) {
		if (sOverlayBuffers[i].blockID == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		debug_printf("intel_arc.accelerant: allocate_overlay_buffer failed: no free overlay slots\n");
		return NULL;
	}

	uint32 blockID = 0;
	uint32 offset = 0;
	if (mem_alloc(gInfo->overlay_mem_mgr, size + 63, (void*)'OVLY',
		&blockID, &offset) != B_OK) {
		debug_printf("intel_arc.accelerant: allocate_overlay_buffer failed: mem_alloc(%zu) failed\n",
			size + 63);
		return NULL;
	}
	const uint32 alignedOffset = (offset + 63) & ~63U;

	intel_arc_overlay_buffer* buffer = &sOverlayBuffers[slot];
	memset(buffer, 0, sizeof(*buffer));
	buffer->blockID = blockID;
	buffer->offset = alignedOffset;
	buffer->size = size;
	buffer->publicBuffer.space = colorSpace;
	buffer->publicBuffer.width = width;
	buffer->publicBuffer.height = height;
	buffer->publicBuffer.bytes_per_row = bytesPerRow;
	buffer->publicBuffer.buffer = (void*)((addr_t)gInfo->frame_buffer + alignedOffset);
	buffer->publicBuffer.buffer_dma = (void*)(addr_t)(
		gInfo->shared_info->frame_buffer_base + alignedOffset);
	debug_printf("intel_arc.accelerant: allocate_overlay_buffer success slot=%d blockID=%" B_PRIu32
		" offset=0x%08" B_PRIx32 " aligned=0x%08" B_PRIx32 " bpr=%zu size=%zu buffer=%p dma=%p\n",
		slot, blockID, offset, alignedOffset, bytesPerRow, size,
		buffer->publicBuffer.buffer, buffer->publicBuffer.buffer_dma);
	return &buffer->publicBuffer;
}

status_t
intel_arc_release_overlay_buffer(const overlay_buffer* buffer)
{
	debug_printf("intel_arc.accelerant: release_overlay_buffer(buffer=%p)\n", buffer);
	if (buffer == NULL) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR cannot release a null buffer\n");
		return B_BAD_VALUE;
	}

	if (sOverlayState.buffer == buffer) {
		sOverlayState.buffer = NULL;
		sOverlayState.configured = false;
	}

	intel_arc_overlay_buffer* privateBuffer = (intel_arc_overlay_buffer*)buffer;
	status_t status = mem_free(gInfo->overlay_mem_mgr, privateBuffer->blockID,
		(void*)'OVLY');
	debug_printf("intel_arc.accelerant: release_overlay_buffer blockID=%" B_PRIu32
		" offset=0x%08" B_PRIx32 " status=%s\n", privateBuffer->blockID,
		privateBuffer->offset, strerror(status));
	memset(privateBuffer, 0, sizeof(*privateBuffer));
	return status;
}

status_t
intel_arc_get_overlay_constraints(const display_mode* mode,
	const overlay_buffer* buffer, overlay_constraints* constraints)
{
	debug_printf("intel_arc.accelerant: get_overlay_constraints(mode=%p, buffer=%p, constraints=%p)\n",
		mode, buffer, constraints);
	if (mode == NULL || buffer == NULL || constraints == NULL){
		debug_printf("intel_arc.accelerant OVERLAY: ERROR mode, buffer or constraints are NULL\n");
		return B_BAD_VALUE;
	}

	memset(constraints, 0, sizeof(*constraints));

	constraints->view.h_alignment = 1;
	constraints->view.v_alignment = 0;
	constraints->view.width_alignment = 1;
	constraints->view.height_alignment = 0;
	constraints->view.width.min = 16;
	constraints->view.height.min = 16;
	constraints->view.width.max = buffer->width;
	constraints->view.height.max = buffer->height;

	constraints->window.h_alignment = 1;
	constraints->window.v_alignment = 0;
	constraints->window.width_alignment = 1;
	constraints->window.height_alignment = 0;
	constraints->window.width.min = 16;
	constraints->window.height.min = 16;
	constraints->window.width.max = mode->virtual_width;
	constraints->window.height.max = mode->virtual_height;

	constraints->h_scale.min = 0.25f;
	constraints->h_scale.max = 8.0f;
	constraints->v_scale.min = 0.25f;
	constraints->v_scale.max = 8.0f;
	debug_printf("intel_arc.accelerant: overlay constraints view=%ux%u..%ux%u window=%ux%u..%ux%u scale=[%.2f..%.2f]/[%.2f..%.2f]\n",
		constraints->view.width.min, constraints->view.height.min,
		constraints->view.width.max, constraints->view.height.max,
		constraints->window.width.min, constraints->window.height.min,
		constraints->window.width.max, constraints->window.height.max,
		constraints->h_scale.min, constraints->h_scale.max,
		constraints->v_scale.min, constraints->v_scale.max);
	return B_OK;
}

overlay_token
intel_arc_allocate_overlay(void)
{
	debug_printf("intel_arc.accelerant: allocate_overlay() channelUsed=%" B_PRId32
		" currentToken=%" B_PRId32 "\n", sOverlayChannelUsed, sOverlayToken);
	if (atomic_or(&sOverlayChannelUsed, 1) != B_OK) {
		debug_printf("intel_arc.accelerant OVERLAY: ERROR atomic_or failed\n");
		return NULL;
	}
	sOverlayState.token = (overlay_token)(addr_t)++sOverlayToken;
	sOverlayState.configured = false;
	sOverlayState.buffer = NULL;
	debug_printf("intel_arc.accelerant: allocate_overlay() -> token=%p\n",
		sOverlayState.token);
	return sOverlayState.token;
}

status_t
intel_arc_release_overlay(overlay_token token)
{
	debug_printf("intel_arc.accelerant: release_overlay(token=%p, current=%p)\n",
		token, sOverlayState.token);
	if (token == NULL || token != sOverlayState.token){
		debug_printf("intel_arc.accelerant OVERLAY: ERROR missing or wrong token on release overlay\n");
		return B_BAD_VALUE;
	}
	atomic_and(&sOverlayChannelUsed, 0);
	sOverlayState.token = NULL;
	sOverlayState.configured = false;
	sOverlayState.buffer = NULL;
	debug_printf("intel_arc.accelerant: release_overlay() done\n");
	return B_OK;
}
