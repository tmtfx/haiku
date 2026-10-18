#include <ByteOrder.h>
#include <Drivers.h>
#include <Errors.h>
#include <KernelExport.h>
#include <PCI.h>
#include <OS.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bttv_driver.h"

static bool sEnableKernelLog = true;
static const char* const kKernelLogPath = "/boot/home/bttv_kernel.log";


static void
bttv_klog(const char* fmt, ...)
{
	if (!sEnableKernelLog)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	dprintf("bttv: %s", buffer);

	int fd = open(kKernelLogPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd >= 0) {
		write(fd, buffer, strlen(buffer));
		fsync(fd);
		close(fd);
	}
}


#define DRIVER_NAME				"bttv"
#define DEVICE_NAME_FORMAT		"video/bttv/%" B_PRId32
#define MAX_DEVICES				4

#define BT878_VENDOR_ID			0x109e
#define BT848_DEVICE_ID			0x0350
#define BT849_DEVICE_ID			0x0351
#define BT878_DEVICE_ID			0x036e
#define BT879_DEVICE_ID			0x036f

#define ATI_VENDOR_ID			0x1002
#define ATI_TV_WONDER_ID		0x0001
#define ATI_TV_WONDER_VE_ID		0x0003

#define BT848_DSTATUS			0x000
#define BT848_IFORM				0x004
#define BT848_TDEC				0x008
#define BT848_E_CROP			0x00c
#define BT848_E_VDELAY_LO		0x010
#define BT848_E_VACTIVE_LO		0x014
#define BT848_E_HDELAY_LO		0x018
#define BT848_E_HACTIVE_LO		0x01c
#define BT848_E_HSCALE_HI		0x020
#define BT848_E_HSCALE_LO		0x024
#define BT848_BRIGHT			0x028
#define BT848_E_CONTROL			0x02c
#define BT848_CONTRAST_LO		0x030
#define BT848_SAT_U_LO			0x034
#define BT848_SAT_V_LO			0x038
#define BT848_HUE				0x03c
#define BT848_E_SCLOOP			0x040
#define BT848_OFORM				0x048
#define BT848_E_VSCALE_HI		0x04c
#define BT848_E_VSCALE_LO		0x050
#define BT848_TEST				0x054
#define BT848_ADELAY			0x060
#define BT848_BDELAY			0x064
#define BT848_ADC				0x068
#define BT848_E_VTC			0x06c
#define BT848_SRESET			0x07c
#define BT848_TGCTRL			0x084
#define BT848_O_CROP			0x08c
#define BT848_O_VDELAY_LO		0x090
#define BT848_O_VACTIVE_LO		0x094
#define BT848_O_HDELAY_LO		0x098
#define BT848_O_HACTIVE_LO		0x09c
#define BT848_O_HSCALE_HI		0x0a0
#define BT848_O_HSCALE_LO		0x0a4
#define BT848_O_CONTROL			0x0ac
#define BT848_O_SCLOOP			0x0c0
#define BT848_O_VSCALE_HI		0x0cc
#define BT848_O_VSCALE_LO		0x0d0
#define BT848_COLOR_FMT			0x0d4
#define BT848_COLOR_CTL			0x0d8
#define BT848_CAP_CTL			0x0dc
#define BT848_VTOTAL_LO			0x0b0
#define BT848_VTOTAL_HI			0x0b4
#define BT848_INT_STAT			0x100
#define BT848_INT_MASK			0x104
#define BT848_GPIO_DMA_CTL		0x10c
#define BT848_PLL_F_LO			0x110
#define BT848_PLL_F_HI			0x111
#define BT848_PLL_XCI			0x112
#define BT848_RISC_STRT_ADD		0x114
#define BT848_GPIO_OUT_EN		0x118
#define BT848_GPIO_DATA			0x200

#define BT848_DSTATUS_PRES		(1 << 6)
#define BT848_DSTATUS_HLOC		(1 << 5)
#define BT848_DSTATUS_NUML		(1 << 4)
#define BT848_DSTATUS_PLOCK		(1 << 0)

#define BT848_PLL_X				(1 << 4)

#define BT848_IFORM_HACTIVE		(1 << 7)
#define BT848_IFORM_MUXSEL		(3 << 5)
#define BT848_IFORM_MUX0		(0 << 5)
#define BT848_IFORM_MUX1		(1 << 5)
#define BT848_IFORM_MUX2		(2 << 5)
#define BT848_IFORM_MUX3		(3 << 5)
#define BT848_IFORM_XT0			(1 << 3)
#define BT848_IFORM_XT1			(2 << 3)
#define BT848_IFORM_PAL_BDGHI	3
#define BT848_IFORM_NTSC		1

#define BT848_CONTROL_COMP		(1 << 6)
#define BT848_SCLOOP_CAGC		(1 << 6)
#define BT848_SCLOOP_CKILL		(1 << 5)
#define BT848_OFORM_RANGE		(1 << 7)
#define BT848_ADC_RESERVED		(2 << 6)
#define BT848_ADC_AGC_EN		(1 << 4)

#define BT848_COLOR_FMT_YUY2	0x44

#define BT848_CAP_CTL_CAPTURE_ODD	(1 << 1)
#define BT848_CAP_CTL_CAPTURE_EVEN	(1 << 0)

#define BT848_INT_ETBF			(1 << 23)
#define BT848_INT_RISC_EN		(1 << 27)
#define BT848_INT_RISCI			(1 << 11)
#define BT848_INT_SCERR			(1 << 19)
#define BT848_INT_OCERR			(1 << 18)
#define BT848_INT_PABORT		(1 << 17)
#define BT848_INT_RIPERR		(1 << 16)
#define BT848_INT_PPERR			(1 << 15)
#define BT848_INT_FDSR			(1 << 14)
#define BT848_INT_FTRGT			(1 << 13)
#define BT848_INT_FBUS			(1 << 12)
#define BT848_INT_OFLOW			(1 << 3)
#define BT848_INT_FMTCHG		(1 << 0)

#define BT848_GPIO_DMA_CTL_RISC_ENABLE	(1 << 1)
#define BT848_GPIO_DMA_CTL_FIFO_ENABLE	(1 << 0)

#define BT848_RISC_IRQ			(1U << 24)
#define BT848_RISC_EOL			(1U << 26)
#define BT848_RISC_SOL			(1U << 27)
#define BT848_RISC_RESYNC		(1U << 15)
#define BT848_RISC_BYTE_ALL		(0x0fU << 12)
#define BT848_RISC_WRITE		(0x01U << 28)
#define BT848_RISC_JUMP			(0x07U << 28)
#define BT848_RISC_SYNC			(0x08U << 28)
#define BT848_FIFO_STATUS_VRE	0x04
#define BT848_FIFO_STATUS_VRO	0x0c
#define BT848_FIFO_STATUS_FM1	0x06
#define NUM_FRAME_BUFFERS		2

#define BT848_VSCALE_INT		(1 << 5)

#define FRAME_TIMEOUT			1000000
#define MAX_FRAME_WIDTH			720
#define MAX_FRAME_HEIGHT_PAL	576
#define MAX_FRAME_HEIGHT_NTSC	480
#define BYTES_PER_PIXEL			2


typedef struct {
	uint16	subsystem_vendor;
	uint16	subsystem_device;
	const char*	name;
	uint8	video_inputs;
	uint8	default_input;
	uint32	capabilities;
	uint32	gpio_mask;
	uint32	gpio_mux[4];
	uint8	input_mux[4];
} bt878_card_profile;


typedef struct {
	area_id	area;
	uint8*	virt;
	phys_addr_t phys;
	size_t	size;
} bt878_dma_buffer;


typedef struct {
	uint16	width;
	uint16	height;
	uint16	hdelay;
	uint16	vdelay;
	uint16	vtotal;
	uint16	hscale;
	uint16	vscale;
	uint8	crop;
	uint8	iform;
	uint8	adelay;
	uint8	bdelay;
} bt878_geometry;


typedef struct {
	pci_info	pci;
	const bt878_card_profile* profile;
	char		name[64];
	volatile uint8* regs;
	area_id		regs_area;
	size_t		regs_size;
	bt878_dma_buffer frame_buffers[NUM_FRAME_BUFFERS];
	bt878_dma_buffer risc_buffer;
	sem_id		frame_sem;
	int32		open_count;
	uint32		frame_size;
	uint16		width;
	uint16		height;
	bttv_video_controls controls;
	uint8		video_standard;
	uint8		input;
	int32		next_buffer;
	int32		last_completed_buffer;
	bool		capturing;
	bool		frame_ready;
	bool		non_blocking;
} bt878_device;


static const bt878_card_profile kWonderVE = {
	ATI_VENDOR_ID,
	ATI_TV_WONDER_VE_ID,
	"ATI TV Wonder VE",
	2,
	BTV_INPUT_COMPOSITE,
	BTV_CAP_VIDEO_CAPTURE | BTV_CAP_HAS_COMPOSITE,
	0x1,
	{ 0, 0, 0, 0 },
	{ BT848_IFORM_MUX2, BT848_IFORM_MUX3, 0, 0 }
};

static const bt878_card_profile kWonder = {
	ATI_VENDOR_ID,
	ATI_TV_WONDER_ID,
	"ATI TV Wonder",
	3,
	BTV_INPUT_COMPOSITE,
	BTV_CAP_VIDEO_CAPTURE | BTV_CAP_HAS_COMPOSITE | BTV_CAP_HAS_SVIDEO,
	0x1,
	{ 0, 0, 1, 0 },
	{ BT848_IFORM_MUX2, BT848_IFORM_MUX3, BT848_IFORM_MUX1, 0 }
};

static const bt878_card_profile kGenericBt878 = {
	0,
	0,
	"Generic Bt878",
	2,
	BTV_INPUT_COMPOSITE,
	BTV_CAP_VIDEO_CAPTURE | BTV_CAP_HAS_COMPOSITE,
	0,
	{ 0, 0, 0, 0 },
	{ BT848_IFORM_MUX2, BT848_IFORM_MUX1, 0, 0 }
};

static const bt878_card_profile kGenericBt848 = {
	0,
	0,
	"Generic Bt848",
	2,
	BTV_INPUT_COMPOSITE,
	BTV_CAP_VIDEO_CAPTURE | BTV_CAP_HAS_COMPOSITE,
	0,
	{ 0, 0, 0, 0 },
	{ BT848_IFORM_MUX2, BT848_IFORM_MUX1, 0, 0 }
};


static pci_module_info* sPCI;
static bt878_device sDevices[MAX_DEVICES];
static const char* sDeviceNames[MAX_DEVICES + 1];
static int32 sDeviceCount;

int32 api_version = B_CUR_DRIVER_API_VERSION;


static int32 bt878_interrupt(void* data);
static void bt878_apply_controls(bt878_device* device);


static inline int32
bt878_clamp_int32(int32 value, int32 min, int32 max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}


static inline uint8
bt878_read8(const bt878_device* device, uint32 offset)
{
	return *((volatile uint8*)(device->regs + offset));
}


static inline uint32
bt878_read32(const bt878_device* device, uint32 offset)
{
	return *((volatile uint32*)(device->regs + offset));
}


static inline void
bt878_write8(const bt878_device* device, uint32 offset, uint8 value)
{
	*((volatile uint8*)(device->regs + offset)) = value;
}


static inline void
bt878_write32(const bt878_device* device, uint32 offset, uint32 value)
{
	*((volatile uint32*)(device->regs + offset)) = value;
}


static status_t
bt878_map_bar0(bt878_device* device)
{
	uint32 flags = device->pci.u.h0.base_register_flags[0];
	if ((flags & PCI_address_space) != 0)
		return B_BAD_VALUE;
	if ((flags & PCI_address_type) == PCI_address_type_64)
		return B_NOT_SUPPORTED;

	phys_addr_t physical = device->pci.u.h0.base_registers[0]
		& PCI_address_memory_32_mask;
	size_t size = device->pci.u.h0.base_register_sizes[0];
	if (physical == 0 || size == 0)
		return B_BAD_VALUE;

	void* mapped = NULL;
	area_id area = map_physical_memory("bttv registers", physical, size,
		B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&mapped);
	if (area < B_OK)
		return area;

	device->regs = (volatile uint8*)mapped;
	device->regs_area = area;
	device->regs_size = size;
	return B_OK;
}


static status_t
bt878_alloc_dma_buffer(const char* name, bt878_dma_buffer* buffer, size_t size)
{
	physical_entry entry;
	void* address = NULL;

	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
	area_id area = create_area(name, &address, B_ANY_KERNEL_ADDRESS, size,
		B_32_BIT_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < B_OK)
		return area;

	if (get_memory_map(address, size, &entry, 1) != B_OK) {
		delete_area(area);
		return B_ERROR;
	}

	buffer->area = area;
	buffer->virt = (uint8*)address;
	buffer->phys = entry.address;
	buffer->size = size;
	memset(buffer->virt, 0, size);
	return B_OK;
}


static void
bt878_free_dma_buffer(bt878_dma_buffer* buffer)
{
	if (buffer->area >= B_OK)
		delete_area(buffer->area);

	buffer->area = -1;
	buffer->virt = NULL;
	buffer->phys = 0;
	buffer->size = 0;
}


static bool
bt878_is_supported(const pci_info& info)
{
	return info.vendor_id == BT878_VENDOR_ID
		&& (info.device_id == BT878_DEVICE_ID
			|| info.device_id == BT879_DEVICE_ID
			|| info.device_id == BT848_DEVICE_ID
			|| info.device_id == BT849_DEVICE_ID)
		&& (info.header_type & PCI_header_type_mask) == PCI_header_type_generic;
}


static const bt878_card_profile*
bt878_profile_for(const pci_info& info)
{
	if (info.u.h0.subsystem_vendor_id == ATI_VENDOR_ID) {
		if (info.u.h0.subsystem_id == ATI_TV_WONDER_VE_ID)
			return &kWonderVE;
		if (info.u.h0.subsystem_id == ATI_TV_WONDER_ID)
			return &kWonder;
	}

	if (info.device_id == BT848_DEVICE_ID || info.device_id == BT849_DEVICE_ID)
		return &kGenericBt848;

	return &kGenericBt878;
}


static bt878_device*
bt878_find_device(const char* name)
{
	for (int32 i = 0; i < sDeviceCount; i++) {
		if (strcmp(name, sDevices[i].name) == 0)
			return &sDevices[i];
	}

	return NULL;
}


static void
bt878_enable_pci(const bt878_device* device)
{
	uint16 command = sPCI->read_pci_config(device->pci.bus, device->pci.device,
		device->pci.function, PCI_command, 2);
	command |= PCI_command_memory | PCI_command_master;
	command &= ~PCI_command_io;
	sPCI->write_pci_config(device->pci.bus, device->pci.device, device->pci.function,
		PCI_command, 2, command);
}


static void
bt878_default_format(bt878_device* device)
{
	device->video_standard = BTV_STD_PAL;
	device->width = MAX_FRAME_WIDTH;
	device->height = MAX_FRAME_HEIGHT_PAL;
	device->input = min_c(device->profile->default_input,
		(uint8)(device->profile->video_inputs - 1));
	device->controls.brightness = 0;
	device->controls.contrast = 216;
	device->controls.saturation = 128;
	device->controls.hue = 0;
}


static void
bt878_current_format(const bt878_device* device, bttv_capture_format* format)
{
	memset(format, 0, sizeof(*format));
	format->width = device->width;
	format->height = device->height;
	format->bytes_per_line = device->width * BYTES_PER_PIXEL;
	format->frame_size = format->bytes_per_line * format->height;
	format->pixel_format = BTV_PIXEL_FORMAT_YUY2;
	format->video_standard = device->video_standard;
	format->input = device->input;
}


static status_t
bt878_validate_format(const bt878_device* device, const bttv_capture_format* format)
{
	uint32 maxHeight = format->video_standard == BTV_STD_PAL
		? MAX_FRAME_HEIGHT_PAL : MAX_FRAME_HEIGHT_NTSC;

	if (format->pixel_format != 0 && format->pixel_format != BTV_PIXEL_FORMAT_YUY2)
		return B_BAD_VALUE;
	if (format->video_standard > BTV_STD_NTSC)
		return B_BAD_VALUE;
	if (format->input >= device->profile->video_inputs)
		return B_BAD_VALUE;
	if (format->width == 0 || format->width > MAX_FRAME_WIDTH || (format->width & 1) != 0)
		return B_BAD_VALUE;
	if (format->height == 0 || format->height > maxHeight)
		return B_BAD_VALUE;

	return B_OK;
}


static void
bt878_compute_geometry(const bt878_device* device, bt878_geometry* geometry)
{
	uint32 sourceWidth;
	uint32 sourceHeight;

	memset(geometry, 0, sizeof(*geometry));

	if (device->video_standard == BTV_STD_PAL) {
		sourceWidth = 924;
		sourceHeight = 576;
		geometry->iform = BT848_IFORM_PAL_BDGHI | BT848_IFORM_XT1;
		geometry->adelay = 0x7f;
		geometry->bdelay = 0x72;
		geometry->hdelay = 186;
		geometry->vdelay = 0x20;
		geometry->vtotal = 624;
	} else {
		sourceWidth = 768;
		sourceHeight = 480;
		geometry->iform = BT848_IFORM_NTSC | BT848_IFORM_XT0;
		geometry->adelay = 0x68;
		geometry->bdelay = 0x5d;
		geometry->hdelay = 128;
		geometry->vdelay = 0x1a;
		geometry->vtotal = 524;
	}

	geometry->width = device->width;
	geometry->height = device->height;
	geometry->hscale = (uint16)((sourceWidth * 4096U + (device->width >> 1))
		/ device->width - 4096);
	geometry->vscale = (uint16)((0x10000UL
		- ((sourceHeight * 512U + (device->height >> 1)) / device->height - 512))
		& 0x1fff);
	geometry->vscale |= BT848_VSCALE_INT << 8;
	geometry->crop = (uint8)(((geometry->width >> 8) & 0x03)
		| ((geometry->hdelay >> 6) & 0x0c)
		| ((geometry->height >> 4) & 0x30)
		| ((geometry->vdelay >> 2) & 0xc0));
	geometry->iform = (uint8)((geometry->iform & ~BT848_IFORM_MUXSEL)
		| device->profile->input_mux[device->input]);
}


static void
bt878_apply_geometry(bt878_device* device, const bt878_geometry& geometry, bool odd)
{
	uint32 offset = odd ? 0x80 : 0x00;

	bt878_write8(device, BT848_E_HSCALE_HI + offset, geometry.hscale >> 8);
	bt878_write8(device, BT848_E_HSCALE_LO + offset, geometry.hscale & 0xff);
	bt878_write8(device, BT848_E_VSCALE_HI + offset, geometry.vscale >> 8);
	bt878_write8(device, BT848_E_VSCALE_LO + offset, geometry.vscale & 0xff);
	bt878_write8(device, BT848_E_HACTIVE_LO + offset, geometry.width & 0xff);
	bt878_write8(device, BT848_E_HDELAY_LO + offset, geometry.hdelay & 0xff);
	bt878_write8(device, BT848_E_VACTIVE_LO + offset, geometry.height & 0xff);
	bt878_write8(device, BT848_E_VDELAY_LO + offset, geometry.vdelay & 0xff);
	bt878_write8(device, BT848_E_CROP + offset, geometry.crop);
	bt878_write8(device, BT848_E_VTC + offset, 0);
}


static status_t
bt878_build_risc_program(bt878_device* device)
{
	bttv_capture_format format;
	bt878_current_format(device, &format);

	bool interlaced = (format.height > 288);
	uint32 linesPerField = interlaced ? (format.height / 2) : format.height;

	uint32 wordsPerBuffer;
	if (interlaced)
		wordsPerBuffer = 2 + linesPerField * 2 + 2 + linesPerField * 2;
	else
		wordsPerBuffer = 2 + format.height * 2;

	uint32 totalWords = wordsPerBuffer * NUM_FRAME_BUFFERS + 2;
	size_t requiredSize = totalWords * sizeof(uint32);
	if (device->risc_buffer.size < requiredSize)
		return B_BUFFER_OVERFLOW;

	uint32* program = (uint32*)device->risc_buffer.virt;

	for (int buf = 0; buf < NUM_FRAME_BUFFERS; buf++) {
		phys_addr_t phys = device->frame_buffers[buf].phys;

		if (interlaced) {
			// Field 1 (Even field): wait for VRE, write lines 0, 2, 4, ...
			*program++ = B_HOST_TO_LENDIAN_INT32(
				BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_FM1);
			*program++ = 0;

			for (uint32 line = 0; line < linesPerField; line++) {
				uint32 lineIdx = line * 2;
				uint32 command = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL
					| BT848_RISC_BYTE_ALL | format.bytes_per_line;
				*program++ = B_HOST_TO_LENDIAN_INT32(command);
				*program++ = B_HOST_TO_LENDIAN_INT32(
					(uint32)(phys + lineIdx * format.bytes_per_line));
			}

			// Field 2 (Odd field): wait for VRO, write lines 1, 3, 5, ...
			*program++ = B_HOST_TO_LENDIAN_INT32(
				BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_FM1);
			*program++ = 0;

			for (uint32 line = 0; line < linesPerField; line++) {
				uint32 lineIdx = line * 2 + 1;
				uint32 command = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL
					| BT848_RISC_BYTE_ALL | format.bytes_per_line;
				if (line + 1 == linesPerField)
					command |= BT848_RISC_IRQ;

				*program++ = B_HOST_TO_LENDIAN_INT32(command);
				*program++ = B_HOST_TO_LENDIAN_INT32(
					(uint32)(phys + lineIdx * format.bytes_per_line));
			}
		} else {
			// Progressive (half vertical resolution): wait for VRE, write single field
			*program++ = B_HOST_TO_LENDIAN_INT32(
				BT848_RISC_SYNC | BT848_RISC_RESYNC | BT848_FIFO_STATUS_FM1);
			*program++ = 0;

			for (uint32 line = 0; line < format.height; line++) {
				uint32 command = BT848_RISC_WRITE | BT848_RISC_SOL | BT848_RISC_EOL
					| BT848_RISC_BYTE_ALL | format.bytes_per_line;
				if (line + 1 == format.height)
					command |= BT848_RISC_IRQ;

				*program++ = B_HOST_TO_LENDIAN_INT32(command);
				*program++ = B_HOST_TO_LENDIAN_INT32(
					(uint32)(phys + line * format.bytes_per_line));
			}
		}
	}

	*program++ = B_HOST_TO_LENDIAN_INT32(BT848_RISC_JUMP);
	*program++ = B_HOST_TO_LENDIAN_INT32((uint32)device->risc_buffer.phys);

	device->next_buffer = 0;
	device->last_completed_buffer = 0;
	return B_OK;
}


static status_t
bt878_set_pll(bt878_device* device)
{
	bttv_klog("[KERNEL] bt878_set_pll: ENTER (dev_id=0x%04x, std=%d)\n",
		device->pci.device_id, device->video_standard);

	// Bt848 and Bt849 have no on-chip PLL; always run directly from crystal
	if (device->pci.device_id == BT848_DEVICE_ID
		|| device->pci.device_id == BT849_DEVICE_ID) {
		bttv_klog("[KERNEL] bt878_set_pll: Bt848/849 detected, using crystal clock (TGCTRL=0x00)\n");
		bt878_write8(device, BT848_TGCTRL, 0x00);
		bt878_write8(device, BT848_PLL_XCI, 0x00);
		return B_OK;
	}

	if (device->video_standard == BTV_STD_NTSC) {
		// NTSC crystal on ATI TV Wonder VE is 28.636363 MHz; PLL can sleep
		bttv_klog("[KERNEL] bt878_set_pll: NTSC standard, using crystal clock (TGCTRL=0x00)\n");
		bt878_write8(device, BT848_TGCTRL, 0x00);
		bt878_write8(device, BT848_PLL_XCI, 0x00);
		return B_OK;
	}

	bttv_klog("[KERNEL] bt878_set_pll: PAL standard on Bt878/879, programming PLL...\n");
	uint32 fin = 28636363 / 4;
	uint32 fout = 35468950 / 4;

	fout *= 12;
	uint8 fi = (uint8)(fout / fin);

	fout = (fout % fin) * 256;
	uint8 fh = (uint8)(fout / fin);

	fout = (fout % fin) * 256;
	uint8 fl = (uint8)(fout / fin);

	bt878_write8(device, BT848_PLL_F_LO, fl);
	bt878_write8(device, BT848_PLL_F_HI, fh);
	bt878_write8(device, BT848_PLL_XCI, fi | BT848_PLL_X);

	// Wait up to 100ms for PLL to lock
	for (int i = 0; i < 10; i++) {
		snooze(10000);
		if ((bt878_read8(device, BT848_DSTATUS) & BT848_DSTATUS_PLOCK) != 0) {
			bt878_write8(device, BT848_DSTATUS, 0x00);
		} else {
			bttv_klog("[KERNEL] bt878_set_pll: PLL locked successfully (TGCTRL=0x08)\n");
			bt878_write8(device, BT848_TGCTRL, 0x08);
			return B_OK;
		}
	}

	// If PLL failed to lock, fall back to crystal clock rather than killing the clock with 0x08
	bttv_klog("[KERNEL] bt878_set_pll: PLL timed out! Falling back to crystal clock (TGCTRL=0x00)\n");
	bt878_write8(device, BT848_TGCTRL, 0x00);
	return B_TIMED_OUT;
}


static void
bt878_program_capture(bt878_device* device)
{
	bttv_klog("[KERNEL] bt878_program_capture: ENTER\n");
	bt878_set_pll(device);

	bttv_klog("[KERNEL] bt878_program_capture: computing geometry...\n");
	bt878_geometry geometry;
	bt878_compute_geometry(device, &geometry);

	bttv_klog("[KERNEL] bt878_program_capture: writing geometry registers...\n");
	uint8 control = 0;
	if (device->input == BTV_INPUT_SVIDEO)
		control |= BT848_CONTROL_COMP;

	bt878_write8(device, BT848_IFORM, geometry.iform | BT848_IFORM_HACTIVE);
	bt878_write8(device, BT848_TDEC, 0);
	bt878_write8(device, BT848_ADELAY, geometry.adelay);
	bt878_write8(device, BT848_BDELAY, geometry.bdelay);
	bt878_write8(device, BT848_E_CONTROL, control);
	bt878_write8(device, BT848_O_CONTROL, control);
	bt878_write8(device, BT848_E_SCLOOP, BT848_SCLOOP_CAGC | BT848_SCLOOP_CKILL);
	bt878_write8(device, BT848_O_SCLOOP, BT848_SCLOOP_CAGC | BT848_SCLOOP_CKILL);
	bt878_write8(device, BT848_OFORM, BT848_OFORM_RANGE);
	bt878_write8(device, BT848_ADC, BT848_ADC_RESERVED | BT848_ADC_AGC_EN);
	bt878_write8(device, BT848_TEST, 0);
	bt878_write8(device, BT848_COLOR_FMT, BT848_COLOR_FMT_YUY2);
	bt878_write8(device, BT848_COLOR_CTL, 0);
	bt878_write8(device, BT848_VTOTAL_LO, geometry.vtotal & 0xff);
	bt878_write8(device, BT848_VTOTAL_HI, geometry.vtotal >> 8);

	bt878_apply_geometry(device, geometry, false);
	bt878_apply_geometry(device, geometry, true);

	if (device->profile->gpio_mask != 0) {
		bt878_write32(device, BT848_GPIO_OUT_EN, device->profile->gpio_mask);
		bt878_write32(device, BT848_GPIO_DATA,
			device->profile->gpio_mux[device->input]);
	}

	bt878_apply_controls(device);
}


static void
bt878_apply_controls(bt878_device* device)
{
	device->controls.brightness = bt878_clamp_int32(device->controls.brightness,
		-128, 127);
	device->controls.contrast = bt878_clamp_int32(device->controls.contrast,
		0, 255);
	device->controls.saturation = bt878_clamp_int32(device->controls.saturation,
		0, 255);
	device->controls.hue = bt878_clamp_int32(device->controls.hue, -128, 127);

	bt878_write8(device, BT848_BRIGHT, (uint8)(int8)device->controls.brightness);
	bt878_write8(device, BT848_CONTRAST_LO, (uint8)device->controls.contrast);
	bt878_write8(device, BT848_SAT_U_LO, (uint8)device->controls.saturation);
	bt878_write8(device, BT848_SAT_V_LO, (uint8)device->controls.saturation);
	bt878_write8(device, BT848_HUE, (uint8)(int8)device->controls.hue);
}


static void
bt878_reset(bt878_device* device)
{
	bt878_write32(device, BT848_INT_MASK, 0);
	bt878_write32(device, BT848_GPIO_DMA_CTL, 0);
	bt878_write8(device, BT848_CAP_CTL, 0);
	bt878_write8(device, BT848_SRESET, 0);
	bt878_write32(device, BT848_INT_STAT, 0xffffffff);
}


static status_t
bt878_start_capture_locked(bt878_device* device)
{
	bttv_klog("[KERNEL] bt878_start_capture_locked: ENTER (capturing=%d)\n", device->capturing);
	if (device->capturing) {
		bttv_klog("[KERNEL] bt878_start_capture_locked: already capturing -> B_BUSY\n");
		return B_BUSY;
	}

	device->next_buffer = 0;
	device->last_completed_buffer = 0;
	device->frame_ready = false;

	bttv_klog("[KERNEL] bt878_start_capture_locked: draining semaphores...\n");
	int32 semCount;
	get_sem_count(device->frame_sem, &semCount);
	if (semCount > 0)
		acquire_sem_etc(device->frame_sem, semCount, 0, 0);

	bttv_klog("[KERNEL] bt878_start_capture_locked: calling bt878_build_risc_program...\n");
	status_t status = bt878_build_risc_program(device);
	if (status != B_OK) {
		bttv_klog("[KERNEL] bt878_start_capture_locked: bt878_build_risc_program FAILED (%s)\n", strerror(status));
		return status;
	}
	bttv_klog("[KERNEL] bt878_start_capture_locked: risc program built, phys=0x%" B_PRIxPHYSADDR "\n",
		device->risc_buffer.phys);

	bttv_klog("[KERNEL] bt878_start_capture_locked: calling bt878_program_capture...\n");
	bt878_program_capture(device);
	bttv_klog("[KERNEL] bt878_start_capture_locked: bt878_program_capture done\n");

	bttv_klog("[KERNEL] bt878_start_capture_locked: clearing INT_STAT...\n");
	bt878_write32(device, BT848_INT_STAT, 0xffffffff);

	bttv_klog("[KERNEL] bt878_start_capture_locked: installing interrupt handler on irq %d...\n",
		device->pci.u.h0.interrupt_line);
	status = install_io_interrupt_handler(device->pci.u.h0.interrupt_line,
		&bt878_interrupt, device, 0);
	if (status != B_OK) {
		bttv_klog("[KERNEL] bt878_start_capture_locked: install_io_interrupt_handler FAILED (%s)\n", strerror(status));
		return status;
	}
	bttv_klog("[KERNEL] bt878_start_capture_locked: interrupt handler installed\n");

	// Enable only frame completion IRQ (RISCI), synchronization errors, and PCI burst fix.
	bttv_klog("[KERNEL] bt878_start_capture_locked: setting INT_MASK...\n");
	bt878_write32(device, BT848_INT_MASK,
		BT848_INT_RISCI | BT848_INT_SCERR | BT848_INT_OCERR | BT848_INT_ETBF);

	bttv_klog("[KERNEL] bt878_start_capture_locked: writing RISC_STRT_ADD (0x%" B_PRIx32 ")...\n",
		(uint32)device->risc_buffer.phys);
	bt878_write32(device, BT848_RISC_STRT_ADD, (uint32)device->risc_buffer.phys);

	bttv_klog("[KERNEL] bt878_start_capture_locked: writing GPIO_DMA_CTL (FIFO+RISC enable)...\n");
	bt878_write32(device, BT848_GPIO_DMA_CTL,
		BT848_GPIO_DMA_CTL_FIFO_ENABLE | BT848_GPIO_DMA_CTL_RISC_ENABLE);

	bttv_klog("[KERNEL] bt878_start_capture_locked: writing CAP_CTL (EVEN+ODD enable)...\n");
	bt878_write8(device, BT848_CAP_CTL,
		BT848_CAP_CTL_CAPTURE_EVEN | BT848_CAP_CTL_CAPTURE_ODD);

	device->capturing = true;
	device->frame_ready = false;
	bttv_klog("[KERNEL] bt878_start_capture_locked: EXIT SUCCESS\n");
	return B_OK;
}


static void
bt878_stop_capture_locked(bt878_device* device)
{
	bttv_klog("[KERNEL] bt878_stop_capture_locked: ENTER (capturing=%d)\n", device->capturing);
	if (!device->capturing) {
		bttv_klog("[KERNEL] bt878_stop_capture_locked: not capturing, exit\n");
		return;
	}

	bttv_klog("[KERNEL] bt878_stop_capture_locked: writing CAP_CTL=0, INT_MASK=0, DMA_CTL=0...\n");
	bt878_write8(device, BT848_CAP_CTL, 0);
	bt878_write32(device, BT848_INT_MASK, 0);
	bt878_write32(device, BT848_GPIO_DMA_CTL, 0);
	bt878_write32(device, BT848_INT_STAT, 0xffffffff);

	bttv_klog("[KERNEL] bt878_stop_capture_locked: removing interrupt handler on irq %d...\n",
		device->pci.u.h0.interrupt_line);
	remove_io_interrupt_handler(device->pci.u.h0.interrupt_line,
		&bt878_interrupt, device);

	device->capturing = false;
	device->frame_ready = false;

	bttv_klog("[KERNEL] bt878_stop_capture_locked: waking up waiting threads...\n");
	int32 semCount;
	if (get_sem_count(device->frame_sem, &semCount) == B_OK && semCount < 0)
		release_sem_etc(device->frame_sem, -semCount, 0);
	bttv_klog("[KERNEL] bt878_stop_capture_locked: EXIT DONE\n");
}


static status_t
bt878_initialize(bt878_device* device)
{
	if (device->regs_area >= B_OK)
		return B_OK;

	bt878_enable_pci(device);

	status_t status = bt878_map_bar0(device);
	if (status != B_OK)
		return status;

	for (int i = 0; i < NUM_FRAME_BUFFERS; i++) {
		status = bt878_alloc_dma_buffer("bttv frame", &device->frame_buffers[i],
			MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT_PAL * BYTES_PER_PIXEL);
		if (status != B_OK)
			goto fail;
	}

	status = bt878_alloc_dma_buffer("bttv risc", &device->risc_buffer, 32768);
	if (status != B_OK)
		goto fail;

	if (device->pci.u.h0.interrupt_line == 0 || device->pci.u.h0.interrupt_line == 0xff) {
		status = B_BAD_VALUE;
		goto fail;
	}

	bt878_reset(device);

	bt878_default_format(device);
	bttv_capture_format format;
	bt878_current_format(device, &format);
	device->frame_size = format.frame_size;
	device->next_buffer = 0;
	device->last_completed_buffer = 0;
	return B_OK;

fail:
	bt878_free_dma_buffer(&device->risc_buffer);
	for (int i = 0; i < NUM_FRAME_BUFFERS; i++)
		bt878_free_dma_buffer(&device->frame_buffers[i]);
	if (device->regs_area >= B_OK) {
		delete_area(device->regs_area);
		device->regs_area = -1;
		device->regs = NULL;
	}
	return status;
}


static void
bt878_shutdown(bt878_device* device)
{
	if (device->regs_area < B_OK)
		return;

	bt878_stop_capture_locked(device);
	bt878_reset(device);

	bt878_free_dma_buffer(&device->risc_buffer);
	for (int i = 0; i < NUM_FRAME_BUFFERS; i++)
		bt878_free_dma_buffer(&device->frame_buffers[i]);

	delete_area(device->regs_area);
	device->regs_area = -1;
	device->regs = NULL;
	device->regs_size = 0;
	device->frame_ready = false;
}


static int32
bt878_interrupt(void* data)
{
	bt878_device* device = (bt878_device*)data;
	if (device == NULL || device->regs == NULL)
		return B_UNHANDLED_INTERRUPT;

	int32 handled = B_UNHANDLED_INTERRUPT;

	for (int loop = 0; loop < 10; loop++) {
		uint32 stat = bt878_read32(device, BT848_INT_STAT);
		if (stat == 0 || stat == 0xffffffff)
			break;

		uint32 mask = bt878_read32(device, BT848_INT_MASK);
		uint32 astat = stat & mask;
		if (astat == 0)
			break;

		handled = B_HANDLED_INTERRUPT;

		// Clear only the asserted interrupt status bits (Write 1 to clear)
		bt878_write32(device, BT848_INT_STAT, astat);

		if ((astat & BT848_INT_RISCI) != 0) {
			device->last_completed_buffer = device->next_buffer;
			device->next_buffer = (device->next_buffer + 1) % NUM_FRAME_BUFFERS;
			device->frame_ready = true;

			int32 semCount;
			if (get_sem_count(device->frame_sem, &semCount) == B_OK && semCount < 1)
				release_sem_etc(device->frame_sem, 1, B_DO_NOT_RESCHEDULE);

			handled = B_INVOKE_SCHEDULER;
		}
	}

	return handled;
}


static status_t
bt878_open(const char* name, uint32 flags, void** cookie)
{
	bttv_klog("[KERNEL] bt878_open: ENTER (name=%s, flags=0x%" B_PRIx32 ")\n", name, flags);
	bt878_device* device = bt878_find_device(name);
	if (device == NULL) {
		bttv_klog("[KERNEL] bt878_open: device not found -> B_NAME_NOT_FOUND\n");
		return B_NAME_NOT_FOUND;
	}

	int32 prevCount = atomic_add(&device->open_count, 1);
	bttv_klog("[KERNEL] bt878_open: open_count was %" B_PRId32 "\n", prevCount);
	if (prevCount == 0) {
		device->non_blocking = (flags & O_NONBLOCK) != 0;
		bttv_klog("[KERNEL] bt878_open: first open, calling bt878_initialize...\n");
		status_t status = bt878_initialize(device);
		if (status != B_OK) {
			bttv_klog("[KERNEL] bt878_open: bt878_initialize FAILED (%s)\n", strerror(status));
			atomic_add(&device->open_count, -1);
			return status;
		}
		bttv_klog("[KERNEL] bt878_open: bt878_initialize SUCCESS\n");
	} else {
		if ((flags & O_NONBLOCK) != 0)
			device->non_blocking = true;
	}

	*cookie = device;
	bttv_klog("[KERNEL] bt878_open: EXIT SUCCESS (cookie=%p)\n", device);
	return B_OK;
}


static status_t
bt878_close(void* cookie)
{
	bttv_klog("[KERNEL] bt878_close: cookie=%p\n", cookie);
	return cookie != NULL ? B_OK : B_BAD_VALUE;
}


static status_t
bt878_free(void* cookie)
{
	bttv_klog("[KERNEL] bt878_free: cookie=%p\n", cookie);
	bt878_device* device = (bt878_device*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	int32 prevCount = atomic_add(&device->open_count, -1);
	bttv_klog("[KERNEL] bt878_free: open_count was %" B_PRId32 "\n", prevCount);
	if (prevCount == 1) {
		bttv_klog("[KERNEL] bt878_free: last open closed, calling bt878_shutdown...\n");
		bt878_shutdown(device);
		bttv_klog("[KERNEL] bt878_free: bt878_shutdown done\n");
	}

	return B_OK;
}


static status_t
bt878_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	bt878_device* device = (bt878_device*)cookie;
	if (device == NULL || buffer == NULL || numBytes == NULL)
		return B_BAD_VALUE;

	if (!device->capturing)
		return B_NOT_ALLOWED;

	if (position < 0 || (size_t)position >= device->frame_size) {
		*numBytes = 0;
		return B_OK;
	}

	if (position == 0) {
		status_t status;
		if (device->non_blocking) {
			status = acquire_sem_etc(device->frame_sem, 1,
				B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, 0);
			if (status != B_OK)
				return B_WOULD_BLOCK;
		} else {
			status = acquire_sem_etc(device->frame_sem, 1,
				B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, FRAME_TIMEOUT);
			if (status != B_OK)
				return status;
		}
	}

	int32 bufIdx = device->last_completed_buffer;
	if (bufIdx < 0 || bufIdx >= NUM_FRAME_BUFFERS)
		bufIdx = 0;

	size_t toCopy = min_c(*numBytes, device->frame_size - (size_t)position);
	if (user_memcpy(buffer, device->frame_buffers[bufIdx].virt + position, toCopy) != B_OK) {
		return B_BAD_ADDRESS;
	}

	*numBytes = toCopy;
	return B_OK;
}


static status_t
bt878_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	if (numBytes != NULL)
		*numBytes = 0;
	return B_NOT_ALLOWED;
}


static status_t
bt878_control(void* cookie, uint32 op, void* arg, size_t length)
{
	bt878_device* device = (bt878_device*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	bttv_klog("[KERNEL] bt878_control: op=0x%" B_PRIx32 "\n", op);

	switch (op) {
		case B_SET_NONBLOCKING_IO:
			device->non_blocking = true;
			return B_OK;

		case B_SET_BLOCKING_IO:
			device->non_blocking = false;
			return B_OK;

		case B_GET_DEVICE_SIZE:
			return user_memcpy(arg, &device->frame_size, sizeof(device->frame_size))
				== B_OK ? B_OK : B_BAD_ADDRESS;

		case B_GET_READ_STATUS:
		{
			int32 semCount = 0;
			get_sem_count(device->frame_sem, &semCount);
			bool canRead = (semCount > 0);
			return user_memcpy(arg, &canRead, sizeof(canRead)) == B_OK
				? B_OK : B_BAD_ADDRESS;
		}

		case BTV_GET_CARD_INFO:
		{
			bttv_card_info info;
			memset(&info, 0, sizeof(info));
			info.vendor_id = device->pci.vendor_id;
			info.device_id = device->pci.device_id;
			info.subsystem_vendor_id = device->pci.u.h0.subsystem_vendor_id;
			info.subsystem_device_id = device->pci.u.h0.subsystem_id;
			info.capabilities = device->profile->capabilities;
			info.video_inputs = device->profile->video_inputs;
			snprintf(info.card_name, sizeof(info.card_name), "%s",
				device->profile->name);
			snprintf(info.device_name, sizeof(info.device_name), "%s",
				device->name);
			return user_memcpy(arg, &info, sizeof(info)) == B_OK
				? B_OK : B_BAD_ADDRESS;
		}

		case BTV_GET_CAPTURE_FORMAT:
		{
			bttv_capture_format format;
			bt878_current_format(device, &format);
			return user_memcpy(arg, &format, sizeof(format)) == B_OK
				? B_OK : B_BAD_ADDRESS;
		}

		case BTV_SET_CAPTURE_FORMAT:
		{
			bttv_klog("[KERNEL] bt878_control: BTV_SET_CAPTURE_FORMAT\n");
			bttv_capture_format format;
			if (user_memcpy(&format, arg, sizeof(format)) != B_OK)
				return B_BAD_ADDRESS;

			status_t status = bt878_validate_format(device, &format);
			if (status != B_OK)
				return status;

			bool wasCapturing = device->capturing;
			bt878_stop_capture_locked(device);
			device->width = (uint16)format.width;
			device->height = (uint16)format.height;
			device->video_standard = (uint8)format.video_standard;
			device->input = (uint8)format.input;
			device->frame_size = format.width * format.height * BYTES_PER_PIXEL;
			device->frame_ready = false;
			if (wasCapturing)
				status = bt878_start_capture_locked(device);
			return status;
		}

		case BTV_SET_INPUT:
		{
			bttv_klog("[KERNEL] bt878_control: BTV_SET_INPUT\n");
			uint32 input = 0;
			if (user_memcpy(&input, arg, sizeof(input)) != B_OK)
				return B_BAD_ADDRESS;
			if (input >= device->profile->video_inputs)
				return B_BAD_VALUE;

			bool wasCapturing = device->capturing;
			bt878_stop_capture_locked(device);
			device->input = (uint8)input;
			if (wasCapturing)
				bt878_start_capture_locked(device);
			return B_OK;
		}

		case BTV_GET_CONTROLS:
			return user_memcpy(arg, &device->controls, sizeof(device->controls))
				== B_OK ? B_OK : B_BAD_ADDRESS;

		case BTV_SET_CONTROLS:
		{
			bttv_klog("[KERNEL] bt878_control: BTV_SET_CONTROLS\n");
			bttv_video_controls controls;
			if (user_memcpy(&controls, arg, sizeof(controls)) != B_OK)
				return B_BAD_ADDRESS;

			controls.brightness = bt878_clamp_int32(controls.brightness, -128, 127);
			controls.contrast = bt878_clamp_int32(controls.contrast, 0, 255);
			controls.saturation = bt878_clamp_int32(controls.saturation, 0, 255);
			controls.hue = bt878_clamp_int32(controls.hue, -128, 127);

			device->controls = controls;
			if (device->regs != NULL)
				bt878_apply_controls(device);
			return B_OK;
		}

		case BTV_START_CAPTURE:
			bttv_klog("[KERNEL] bt878_control: BTV_START_CAPTURE\n");
			return bt878_start_capture_locked(device);

		case BTV_STOP_CAPTURE:
			bttv_klog("[KERNEL] bt878_control: BTV_STOP_CAPTURE\n");
			bt878_stop_capture_locked(device);
			return B_OK;

		default:
			return B_DEV_INVALID_IOCTL;
	}
}


static device_hooks sDeviceHooks = {
	&bt878_open,
	&bt878_close,
	&bt878_free,
	&bt878_control,
	&bt878_read,
	&bt878_write,
	NULL,
	NULL,
	NULL,
	NULL
};


status_t
init_hardware(void)
{
	pci_module_info* pci = NULL;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return B_ERROR;

	status_t result = B_DEVICE_NOT_FOUND;
	pci_info info;
	for (int32 index = 0; pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (bt878_is_supported(info)) {
			result = B_OK;
			break;
		}
	}

	put_module(B_PCI_MODULE_NAME);
	return result;
}


status_t
init_driver(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI) != B_OK)
		return B_ERROR;

	memset(sDevices, 0, sizeof(sDevices));
	memset(sDeviceNames, 0, sizeof(sDeviceNames));
	sDeviceCount = 0;

	pci_info info;
	for (int32 index = 0; sPCI->get_nth_pci_info(index, &info) == B_OK
		&& sDeviceCount < MAX_DEVICES; index++) {
		if (!bt878_is_supported(info))
			continue;

		bt878_device& device = sDevices[sDeviceCount];
		memset(&device, 0, sizeof(device));
		device.pci = info;
		device.profile = bt878_profile_for(info);
		device.regs_area = -1;
		for (int i = 0; i < NUM_FRAME_BUFFERS; i++)
			device.frame_buffers[i].area = -1;
		device.risc_buffer.area = -1;
		device.frame_sem = create_sem(0, "bttv frame");
		if (device.frame_sem < B_OK)
			break;

		bt878_default_format(&device);
		device.frame_size = device.width * device.height * BYTES_PER_PIXEL;
		snprintf(device.name, sizeof(device.name), DEVICE_NAME_FORMAT, sDeviceCount);
		sDeviceNames[sDeviceCount] = device.name;
		sDeviceCount++;
	}

	sDeviceNames[sDeviceCount] = NULL;

	if (sDeviceCount == 0) {
		if (sPCI != NULL) {
			put_module(B_PCI_MODULE_NAME);
			sPCI = NULL;
		}
		return B_DEVICE_NOT_FOUND;
	}

	return B_OK;
}


void
uninit_driver(void)
{
	for (int32 i = 0; i < sDeviceCount; i++) {
		bt878_device& device = sDevices[i];
		bt878_shutdown(&device);
		if (device.frame_sem >= B_OK)
			delete_sem(device.frame_sem);
	}

	if (sPCI != NULL) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}
}


const char**
publish_devices(void)
{
	return sDeviceNames;
}


device_hooks*
find_device(const char* name)
{
	return bt878_find_device(name) != NULL ? &sDeviceHooks : NULL;
}
