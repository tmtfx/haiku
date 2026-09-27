/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "vesa_private.h"

#include <boot_item.h>
#include <frame_buffer_console.h>
#include <driver_settings.h>
#include <KernelExport.h>
#include <PCI.h>
#include <util/AreaKeeper.h>
#include <string.h>

#include "driver.h"
#include "DriverInterface.h"

#define ROUND_TO_PAGE_SIZE(x) (((x) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1))
#define NVIDIA_GT7XX_PBUS_BAR0_WINDOW 0x00001700U
#define NVIDIA_GT7XX_PBUS_BAR0_WINDOW_TARGET_VRAM 0x00000000U


struct pci_bar_info {
	int32		index;
	uint8		flags;
	uint64		base;
	uint64		size;
	int32		consumed;
};


static bool
supports_device(uint16 vendorID, uint16 deviceID)
{
	return vendorID == 0x10de && deviceID == 0x1287;
}


static const char*
device_name(uint16 vendorID, uint16 deviceID)
{
	if (vendorID == 0x10de && deviceID == 0x1287)
		return "NVIDIA GeForce GT 730 (GK208)";

	return "NVIDIA GT7xx";
}


static color_space
get_color_space_for_depth(uint32 depth)
{
	switch (depth) {
		case 8:
			return B_CMAP8;
		case 15:
			return B_RGB15_LITTLE;
		case 16:
			return B_RGB16_LITTLE;
		case 24:
			return B_RGB24_LITTLE;
		case 32:
			return B_RGB32_LITTLE;
		default:
			return B_NO_COLOR_SPACE;
	}
}


static uint32
get_dpms_capabilities(const edid1_info* edid)
{
	uint32 capabilities = B_DPMS_ON;
	if (edid == NULL)
		return capabilities;

	if (edid->display.dpms_standby)
		capabilities |= B_DPMS_STAND_BY;
	if (edid->display.dpms_suspend)
		capabilities |= B_DPMS_SUSPEND;
	if (edid->display.dpms_off)
		capabilities |= B_DPMS_OFF;

	return capabilities;
}


static uint8
get_active_output_type(const edid1_info* edid)
{
	if (edid == NULL)
		return NVIDIA_GT7XX_OUTPUT_UNKNOWN;

	return edid->display.input_type != 0
		? NVIDIA_GT7XX_OUTPUT_DIGITAL : NVIDIA_GT7XX_OUTPUT_ANALOG;
}


static bool
get_bar_info(const pci_info& info, int32 index, pci_bar_info& bar)
{
	memset(&bar, 0, sizeof(bar));
	bar.index = index;
	bar.consumed = 1;

	if (index < 0 || index >= 6)
		return false;

	bar.flags = info.u.h0.base_register_flags[index];
	bar.size = info.u.h0.base_register_sizes[index];

	if ((bar.flags & PCI_address_space) != 0 || bar.size == 0)
		return false;

	bar.base = info.u.h0.base_registers[index];
	if ((bar.flags & PCI_address_type) == PCI_address_type_64 && index < 5) {
		bar.base |= (uint64)info.u.h0.base_registers[index + 1] << 32;
		bar.size |= (uint64)info.u.h0.base_register_sizes[index + 1] << 32;
		bar.consumed = 2;
	}

	return bar.base != 0 && bar.size != 0;
}


static status_t
find_graphics_card(addr_t frameBuffer, pci_info& match, pci_bar_info& mmioBar,
	pci_bar_info& frameBufferBar)
{
	pci_module_info* pci;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	status = B_ENTRY_NOT_FOUND;
	pci_info info;
	for (int32 index = 0; pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.class_base != PCI_display)
			continue;
		if (!supports_device(info.vendor_id, info.device_id))
			continue;

		pci_bar_info chosenFrameBuffer = {};
		for (int32 barIndex = 0; barIndex < 6; barIndex++) {
			pci_bar_info bar;
			if (!get_bar_info(info, barIndex, bar))
				continue;
			if (bar.base <= frameBuffer && frameBuffer < bar.base + bar.size) {
				chosenFrameBuffer = bar;
				break;
			}
			barIndex += bar.consumed - 1;
		}

		if (chosenFrameBuffer.size == 0)
			continue;

		pci_bar_info chosenMmio = {};
		for (int32 barIndex = 0; barIndex < 6; barIndex++) {
			pci_bar_info bar;
			if (!get_bar_info(info, barIndex, bar))
				continue;
			if (bar.index == chosenFrameBuffer.index) {
				barIndex += bar.consumed - 1;
				continue;
			}

			if ((bar.flags & PCI_address_prefetchable) == 0) {
				chosenMmio = bar;
				break;
			}

			if (chosenMmio.size == 0)
				chosenMmio = bar;

			barIndex += bar.consumed - 1;
		}

		if (chosenMmio.size == 0)
			continue;

		match = info;
		mmioBar = chosenMmio;
		frameBufferBar = chosenFrameBuffer;
		status = B_OK;
		break;
	}

	put_module(B_PCI_MODULE_NAME);
	return status;
}


static status_t
get_area_physical_address(void* address, size_t size, uint64& physicalAddress)
{
	physical_entry entry;
	status_t status = get_memory_map(address, size, &entry, 1);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: get_area_physical_address address=%p size=%" B_PRIuSIZE
			" get_memory_map failed status=%d\n", address, size, status);
		return B_ERROR;
	}

	if (entry.size < size) {
		dprintf("nvidia_gt7xx: get_area_physical_address address=%p size=%" B_PRIuSIZE
			" entry too small phys=0x%016" B_PRIxPHYSADDR " entry_size=%" B_PRIuPHYSADDR "\n",
			address, size, entry.address, entry.size);
		return B_ERROR;
	}

	physicalAddress = entry.address;
	dprintf("nvidia_gt7xx: get_area_physical_address address=%p size=%" B_PRIuSIZE
		" phys=0x%016" B_PRIx64 "\n", address, size, physicalAddress);
	return B_OK;
}


static status_t
init_evo_channel(vesa_info& info, uint32 channelID, const char* name, uint32 classID)
{
	if (channelID >= NVIDIA_GT7XX_EVO_CHANNEL_COUNT)
		return B_BAD_VALUE;

	dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] class=0x%08" B_PRIx32 " name=%s\n",
		channelID, classID, name);

	AreaKeeper pushKeeper;
	area_id pushArea = pushKeeper.Create(name,
		(void**)&info.channel_push_buffer[channelID], B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(NVIDIA_GT7XX_EVO_PUSH_WORDS * sizeof(uint32)),
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (pushArea < B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] push area create failed status=%"
			B_PRId32 "\n", channelID, pushArea);
		return pushArea;
	}

	AreaKeeper notifierKeeper;
	area_id notifierArea = notifierKeeper.Create("nvidia_gt7xx notifier",
		(void**)&info.channel_notifier[channelID], B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS * sizeof(uint32)),
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (notifierArea < B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] notifier area create failed status=%"
			B_PRId32 "\n", channelID, notifierArea);
		return notifierArea;
	}

	AreaKeeper instanceKeeper;
	area_id instanceArea = instanceKeeper.Create("nvidia_gt7xx instance",
		(void**)&info.channel_instance_data[channelID], B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(NVIDIA_GT7XX_EVO_RAMFC_WORDS * sizeof(uint32)),
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (instanceArea < B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] instance area create failed status=%"
			B_PRId32 "\n", channelID, instanceArea);
		return instanceArea;
	}

	memset(info.channel_push_buffer[channelID], 0,
		NVIDIA_GT7XX_EVO_PUSH_WORDS * sizeof(uint32));
	memset(info.channel_notifier[channelID], 0,
		NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS * sizeof(uint32));
	memset(info.channel_instance_data[channelID], 0,
		NVIDIA_GT7XX_EVO_RAMFC_WORDS * sizeof(uint32));

	info.channel_push_area[channelID] = pushArea;
	info.channel_notifier_area[channelID] = notifierArea;
	info.channel_instance_area[channelID] = instanceArea;

	vesa_shared_info& sharedInfo = *info.shared_info;
	sharedInfo.channels[channelID].class_id = classID;
	sharedInfo.channels[channelID].push_area = info.channel_push_area[channelID];
	sharedInfo.channels[channelID].notifier_area
		= info.channel_notifier_area[channelID];
	sharedInfo.channels[channelID].instance_area
		= info.channel_instance_area[channelID];
	sharedInfo.channels[channelID].push_words = NVIDIA_GT7XX_EVO_PUSH_WORDS;
	sharedInfo.channels[channelID].notifier_dwords = NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS;
	sharedInfo.channels[channelID].args.version = 0;
	sharedInfo.channels[channelID].args.id = channelID;
	sharedInfo.channels[channelID].put = 0;
	sharedInfo.channels[channelID].get = 0;
	sharedInfo.channels[channelID].last_submit_words = 0;
	sharedInfo.channels[channelID].submit_count = 0;
	sharedInfo.channels[channelID].notifier_status = 0;

	status_t status = get_area_physical_address(info.channel_push_buffer[channelID],
		NVIDIA_GT7XX_EVO_PUSH_WORDS * sizeof(uint32),
		sharedInfo.channels[channelID].memory.pushbuf_physical);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] push phys resolve failed status=%"
			B_PRId32 "\n", channelID, status);
		return status;
	}

	status = get_area_physical_address(info.channel_notifier[channelID],
		NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS * sizeof(uint32),
		sharedInfo.channels[channelID].memory.notifier_physical);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] notifier phys resolve failed status=%"
			B_PRId32 "\n", channelID, status);
		return status;
	}

	status = get_area_physical_address(info.channel_instance_data[channelID],
		NVIDIA_GT7XX_EVO_RAMFC_WORDS * sizeof(uint32),
		sharedInfo.channels[channelID].memory.instance_physical);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: init_evo_channel[%" B_PRIu32 "] instance phys resolve failed status=%"
			B_PRId32 "\n", channelID, status);
		return status;
	}

	sharedInfo.channels[channelID].memory.pushbuf_size
		= NVIDIA_GT7XX_EVO_PUSH_WORDS * sizeof(uint32);
	sharedInfo.channels[channelID].memory.notifier_size
		= NVIDIA_GT7XX_EVO_NOTIFIER_DWORDS * sizeof(uint32);
	sharedInfo.channels[channelID].memory.instance_size
		= NVIDIA_GT7XX_EVO_RAMFC_WORDS * sizeof(uint32);
	sharedInfo.channels[channelID].memory.flags = 0x1;
	sharedInfo.channels[channelID].args.pushbuf
		= sharedInfo.channels[channelID].memory.pushbuf_physical;
	sharedInfo.channels[channelID].user_aperture_offset
		= NVIDIA_GT7XX_EVO_DMA_USER_BASE
		+ channelID * NVIDIA_GT7XX_EVO_DMA_USER_STRIDE;
	sharedInfo.channels[channelID].pramin_offset
		= NVIDIA_GT7XX_EVO_PRAMIN_BASE + channelID * 0x200;
	sharedInfo.channels[channelID].materialized = 0;

	uint32* ramfc = info.channel_instance_data[channelID];
	ramfc[0] = classID;
	ramfc[1] = sharedInfo.channels[channelID].args.id;
	ramfc[2] = (uint32)sharedInfo.channels[channelID].memory.pushbuf_physical;
	ramfc[3] = (uint32)(sharedInfo.channels[channelID].memory.pushbuf_physical >> 32);
	ramfc[4] = (uint32)sharedInfo.channels[channelID].memory.notifier_physical;
	ramfc[5] = (uint32)(sharedInfo.channels[channelID].memory.notifier_physical >> 32);
	ramfc[6] = (uint32)sharedInfo.channels[channelID].memory.instance_physical;
	ramfc[7] = (uint32)(sharedInfo.channels[channelID].memory.instance_physical >> 32);
	ramfc[8] = sharedInfo.channels[channelID].push_words;
	ramfc[9] = sharedInfo.channels[channelID].notifier_dwords;
	ramfc[10] = sharedInfo.channels[channelID].user_aperture_offset;
	ramfc[11] = sharedInfo.channels[channelID].pramin_offset;

	dprintf("nvidia_gt7xx: evo[%" B_PRIu32 "] push_area=%" B_PRId32 " notifier_area=%" B_PRId32
		" instance_area=%" B_PRId32 "\n",
		channelID, info.channel_push_area[channelID],
		info.channel_notifier_area[channelID], info.channel_instance_area[channelID]);
	dprintf("nvidia_gt7xx: evo[%" B_PRIu32 "] push_phys=0x%016" B_PRIx64 " notifier_phys=0x%016"
		B_PRIx64 " instance_phys=0x%016" B_PRIx64 "\n",
		channelID, sharedInfo.channels[channelID].memory.pushbuf_physical,
		sharedInfo.channels[channelID].memory.notifier_physical,
		sharedInfo.channels[channelID].memory.instance_physical);
	dprintf("nvidia_gt7xx: evo[%" B_PRIu32 "] user_aperture=0x%08" B_PRIx32
		" pramin_offset=0x%08" B_PRIx32 " pushbuf_arg=0x%016" B_PRIx64 "\n",
		channelID, sharedInfo.channels[channelID].user_aperture_offset,
		sharedInfo.channels[channelID].pramin_offset,
		sharedInfo.channels[channelID].args.pushbuf);

	pushKeeper.Detach();
	notifierKeeper.Detach();
	instanceKeeper.Detach();
	return B_OK;
}


static uint32
encode_dma_method_header(uint32 method, uint32 count)
{
	return NVIDIA_GT7XX_EVO_DMA_OPCODE_METHOD
		| ((count & 0x3ff) << NVIDIA_GT7XX_EVO_DMA_METHOD_COUNT_SHIFT)
		| (((method & 0xfff) >> 2) << NVIDIA_GT7XX_EVO_DMA_METHOD_OFFSET_SHIFT);
}


static status_t
write_dma_push(vesa_info& info, const nvidia_gt7xx_evo_push& push)
{
	if (push.channel >= NVIDIA_GT7XX_EVO_CHANNEL_COUNT)
		return B_BAD_VALUE;

	vesa_shared_info& sharedInfo = *info.shared_info;
	nvidia_gt7xx_evo_channel_state& channel = sharedInfo.channels[push.channel];
	uint32* words = info.channel_push_buffer[push.channel];
	uint32* notifier = info.channel_notifier[push.channel];
	if (words == NULL || notifier == NULL)
		return B_ERROR;

	const uint32 wordsNeeded = push.count * 2;
	if (wordsNeeded == 0 || wordsNeeded > channel.push_words)
		return B_BAD_VALUE;

	uint32 put = channel.put;
	if (put + wordsNeeded > channel.push_words)
		put = 0;

	dprintf("nvidia_gt7xx: write_dma_push channel=%" B_PRIu32
		" methods=%" B_PRIu32 " words=%" B_PRIu32 " put_in=%" B_PRIu32 "\n",
		push.channel, push.count, wordsNeeded, channel.put);

	for (uint32 i = 0; i < push.count; i++) {
		words[put++] = encode_dma_method_header(push.methods[i].method, 1);
		words[put++] = push.methods[i].value;
		dprintf("nvidia_gt7xx:   m[%02" B_PRIu32 "] method=0x%08" B_PRIx32
			" value=0x%08" B_PRIx32 "\n",
			i, push.methods[i].method, push.methods[i].value);
	}

	channel.last_submit_words = wordsNeeded;
	channel.put = put;
	channel.get = put;
	channel.submit_count++;
	channel.notifier_status = 0;
	channel.materialized = 1;

	notifier[0] = channel.submit_count;
	notifier[1] = wordsNeeded;
	notifier[2] = channel.put;
	notifier[3] = channel.get;

	if (info.channel_instance_data[push.channel] != NULL) {
		uint32* ramfc = info.channel_instance_data[push.channel];
		ramfc[12] = channel.put;
		ramfc[13] = channel.get;
		ramfc[14] = channel.last_submit_words;
		ramfc[15] = channel.submit_count;
	}
	dprintf("nvidia_gt7xx: write_dma_push channel=%" B_PRIu32 " put_out=%" B_PRIu32
		" get=%" B_PRIu32 " submits=%" B_PRIu32 "\n",
		push.channel, channel.put, channel.get, channel.submit_count);
	return B_OK;
}


static bool
mmio_range_valid(const vesa_info& info, uint32 offset, size_t size)
{
	return info.registers != NULL
		&& offset <= info.shared_info->registers_size
		&& size <= info.shared_info->registers_size - offset;
}


static uint32
mmio_read32(const vesa_info& info, uint32 offset)
{
	volatile uint32* address = (volatile uint32*)(info.registers + offset);
	return *address;
}


static void
mmio_write32(vesa_info& info, uint32 offset, uint32 value)
{
	volatile uint32* address = (volatile uint32*)(info.registers + offset);
	*address = value;
}


static status_t
program_pramin_window(vesa_info& info, uint32 vramAddress)
{
	if (!mmio_range_valid(info, NVIDIA_GT7XX_PBUS_BAR0_WINDOW, sizeof(uint32)))
		return B_ERROR;

	uint32 windowBase = (vramAddress >> 16) & 0x00ffffff;
	uint32 value = NVIDIA_GT7XX_PBUS_BAR0_WINDOW_TARGET_VRAM | windowBase;
	dprintf("nvidia_gt7xx: program_pramin_window vram=0x%08" B_PRIx32
		" window_base=0x%08" B_PRIx32 "\n", vramAddress, value);
	mmio_write32(info, NVIDIA_GT7XX_PBUS_BAR0_WINDOW, value);
	return B_OK;
}


static status_t
sync_channel_instance_to_pramin(vesa_info& info, uint32 channelID)
{
	if (channelID >= NVIDIA_GT7XX_EVO_CHANNEL_COUNT)
		return B_BAD_VALUE;

	nvidia_gt7xx_evo_channel_state& channel = info.shared_info->channels[channelID];
	if (info.channel_instance_data[channelID] == NULL)
		return B_ERROR;
	if (!mmio_range_valid(info, channel.pramin_offset,
			NVIDIA_GT7XX_EVO_RAMFC_WORDS * sizeof(uint32))) {
		return B_ERROR;
	}

	status_t status = program_pramin_window(info, 0);
	if (status != B_OK)
		return status;

	volatile uint32* pramin = (volatile uint32*)(info.registers + channel.pramin_offset);
	for (uint32 i = 0; i < NVIDIA_GT7XX_EVO_RAMFC_WORDS; i++)
		pramin[i] = info.channel_instance_data[channelID][i];

	dprintf("nvidia_gt7xx: sync_channel_instance_to_pramin channel=%" B_PRIu32
		" pramin_off=0x%08" B_PRIx32 " class=0x%08" B_PRIx32
		" push_lo=0x%08" B_PRIx32 " notif_lo=0x%08" B_PRIx32
		" put=%" B_PRIu32 " get=%" B_PRIu32 "\n",
		channelID, channel.pramin_offset, channel.class_id,
		info.channel_instance_data[channelID][2],
		info.channel_instance_data[channelID][4],
		channel.put, channel.get);

	channel.materialized = 1;
	return B_OK;
}


static status_t
kick_dma_user_channel(vesa_info& info, uint32 channelID)
{
	if (channelID >= NVIDIA_GT7XX_EVO_CHANNEL_COUNT)
		return B_BAD_VALUE;

	nvidia_gt7xx_evo_channel_state& channel = info.shared_info->channels[channelID];
	uint32 putOffset = channel.user_aperture_offset;
	uint32 getOffset = channel.user_aperture_offset + 0x4;
	if (!mmio_range_valid(info, putOffset, sizeof(uint32))
		|| !mmio_range_valid(info, getOffset, sizeof(uint32))) {
		dprintf("nvidia_gt7xx: kick_dma_user_channel channel=%" B_PRIu32
			" invalid aperture put=0x%08" B_PRIx32 " get=0x%08" B_PRIx32 "\n",
			channelID, putOffset, getOffset);
		return B_ERROR;
	}

	dprintf("nvidia_gt7xx: kick_dma_user_channel channel=%" B_PRIu32
		" put_off=0x%08" B_PRIx32 " get_off=0x%08" B_PRIx32
		" put_words=%" B_PRIu32 "\n",
		channelID, putOffset, getOffset, channel.put);
	mmio_write32(info, putOffset, channel.put << 2);
	channel.get = mmio_read32(info, getOffset) >> 2;
	dprintf("nvidia_gt7xx: kick_dma_user_channel channel=%" B_PRIu32
		" hw_get_words=%" B_PRIu32 "\n",
		channelID, channel.get);
	channel.materialized = 2;
	return B_OK;
}


static status_t
materialize_hardware_channel(vesa_info& info, uint32 channelID)
{
	dprintf("nvidia_gt7xx: materialize_hardware_channel channel=%" B_PRIu32 "\n",
		channelID);
	status_t status = sync_channel_instance_to_pramin(info, channelID);
	if (status != B_OK)
		dprintf("nvidia_gt7xx: materialize_hardware_channel channel=%" B_PRIu32
			" pramin sync failed: %" B_PRId32 "\n", channelID, status);
	if (status != B_OK)
		return status;

	status = kick_dma_user_channel(info, channelID);
	if (status != B_OK)
		dprintf("nvidia_gt7xx: materialize_hardware_channel channel=%" B_PRIu32
			" dma kick failed: %" B_PRId32 "\n", channelID, status);
	return status;
}


static bool
same_mode(const display_mode& left, const display_mode& right)
{
	return left.space == right.space
		&& left.virtual_width == right.virtual_width
		&& left.virtual_height == right.virtual_height
		&& left.timing.pixel_clock == right.timing.pixel_clock
		&& left.timing.h_display == right.timing.h_display
		&& left.timing.h_sync_start == right.timing.h_sync_start
		&& left.timing.h_sync_end == right.timing.h_sync_end
		&& left.timing.h_total == right.timing.h_total
		&& left.timing.v_display == right.timing.v_display
		&& left.timing.v_sync_start == right.timing.v_sync_start
		&& left.timing.v_sync_end == right.timing.v_sync_end
		&& left.timing.v_total == right.timing.v_total
		&& left.timing.flags == right.timing.flags;
}


static color_space
color_space_from_evo_format(uint32 baseParams)
{
	switch ((baseParams >> 8) & 0xff) {
		case 0x1e:
			return B_CMAP8;
		case 0xe8:
			return B_RGB16_LITTLE;
		case 0xe9:
			return B_RGB15_LITTLE;
		case 0xcf:
			return B_RGB32_LITTLE;
		default:
			return B_NO_COLOR_SPACE;
	}
}


static uint32
bytes_per_row_from_evo_storage(uint32 baseStorage, uint32 width, color_space space)
{
	uint32 pitch = ((baseStorage >> 8) & 0x3ff) << 8;
	if (pitch != 0)
		return pitch;

	uint32 bytesPerPixel = 4;
	switch (space) {
		case B_CMAP8:
			bytesPerPixel = 1;
			break;
		case B_RGB15_LITTLE:
		case B_RGB16_LITTLE:
			bytesPerPixel = 2;
			break;
		case B_RGB24_LITTLE:
			bytesPerPixel = 3;
			break;
		case B_RGB32_LITTLE:
		default:
			bytesPerPixel = 4;
			break;
	}
	return width * bytesPerPixel;
}


static uint32
timing_flags_from_evo_state(const vesa_shared_info& sharedInfo)
{
	uint32 flags = 0;
	uint32 polarity = sharedInfo.evo.output_polarity;
	if (sharedInfo.active_output == NVIDIA_GT7XX_OUTPUT_DIGITAL)
		polarity = sharedInfo.evo.output_control;

	if ((polarity & (1 << 12)) != 0 || (polarity & (1 << 0)) != 0)
		flags |= B_POSITIVE_HSYNC;
	if ((polarity & (1 << 13)) != 0 || (polarity & (1 << 1)) != 0)
		flags |= B_POSITIVE_VSYNC;
	if ((sharedInfo.evo.head_control & 0x6) != 0)
		flags |= B_TIMING_INTERLACED;
	return flags;
}


static void
update_mode_from_evo_state(vesa_shared_info& sharedInfo)
{
	display_mode& mode = sharedInfo.current_mode;
	mode.timing.pixel_clock = sharedInfo.evo.pixel_clock & 0x003fffff;
	mode.timing.h_display = sharedInfo.evo.raster_size & 0x7fff;
	mode.timing.v_display = (sharedInfo.evo.raster_size >> 16) & 0x7fff;
	mode.timing.h_sync_end = sharedInfo.evo.raster_sync_end & 0x7fff;
	mode.timing.v_sync_end = (sharedInfo.evo.raster_sync_end >> 16) & 0x7fff;
	mode.timing.h_sync_start = sharedInfo.evo.raster_blank_end & 0x7fff;
	mode.timing.v_sync_start = (sharedInfo.evo.raster_blank_end >> 16) & 0x7fff;
	mode.timing.h_total = sharedInfo.evo.raster_blank_start & 0x7fff;
	mode.timing.v_total = (sharedInfo.evo.raster_blank_start >> 16) & 0x7fff;
	mode.timing.flags = timing_flags_from_evo_state(sharedInfo);
	mode.virtual_width = mode.timing.h_display;
	mode.virtual_height = mode.timing.v_display;

	color_space space = color_space_from_evo_format(sharedInfo.evo.base_params);
	if (space != B_NO_COLOR_SPACE)
		mode.space = space;

	sharedInfo.bytes_per_row = bytes_per_row_from_evo_storage(
		sharedInfo.evo.base_storage, mode.virtual_width, (color_space)mode.space);
	sharedInfo.fbc.bytes_per_row = sharedInfo.bytes_per_row;
}


static bool
decode_head_index(uint32 method, uint32 baseMethod, uint8& headIndex)
{
	if (method < baseMethod)
		return false;

	uint32 delta = method - baseMethod;
	if ((delta % 0x400) != 0)
		return false;

	headIndex = delta / 0x400;
	return headIndex < 2;
}


static bool
decode_output_index(uint32 method, uint32 baseMethod, uint32 stride, uint8& outputIndex)
{
	if (method < baseMethod)
		return false;

	uint32 delta = method - baseMethod;
	if ((delta % stride) != 0)
		return false;

	outputIndex = delta / stride;
	return outputIndex < 4;
}


static status_t
apply_evo_method(vesa_shared_info& sharedInfo, const nvidia_gt7xx_evo_method& method)
{
	uint8 index;
	if (method.method == NVIDIA_GT7XX_EVO_UPDATE) {
		sharedInfo.evo.last_update = method.value;
		return B_OK;
	}

	if (decode_output_index(method.method, NVIDIA_GT7XX_EVO_DAC_SET_CONTROL(0),
			0x80, index)) {
		sharedInfo.active_output = NVIDIA_GT7XX_OUTPUT_ANALOG;
		sharedInfo.evo.output_control = method.value;
		return B_OK;
	}

	if (decode_output_index(method.method, NVIDIA_GT7XX_EVO_DAC_SET_POLARITY(0),
			0x80, index)) {
		sharedInfo.active_output = NVIDIA_GT7XX_OUTPUT_ANALOG;
		sharedInfo.evo.output_polarity = method.value;
		return B_OK;
	}

	if (decode_output_index(method.method, NVIDIA_GT7XX_EVO_SOR_SET_CONTROL(0),
			0x40, index)) {
		sharedInfo.active_output = NVIDIA_GT7XX_OUTPUT_DIGITAL;
		sharedInfo.evo.output_control = method.value;
		return B_OK;
	}

	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_PIXEL_CLOCK(0), index)) {
		sharedInfo.active_head = index;
		sharedInfo.evo.pixel_clock = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_CONTROL(0), index)) {
		sharedInfo.active_head = index;
		sharedInfo.evo.head_control = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_OVERSCAN_COLOR(0), index)) {
		sharedInfo.evo.overscan_color = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SIZE(0), index)) {
		sharedInfo.evo.raster_size = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_SYNC_END(0), index)) {
		sharedInfo.evo.raster_sync_end = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_END(0), index)) {
		sharedInfo.evo.raster_blank_end = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_BLANK_START(0), index)) {
		sharedInfo.evo.raster_blank_start = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK2(0), index)) {
		sharedInfo.evo.raster_vert_blank2 = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_RASTER_VERT_BLANK_DMI(0), index)) {
		sharedInfo.evo.raster_vert_blank_dmi = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_DEFAULT_BASE_COLOR(0), index)) {
		sharedInfo.evo.default_base_color = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_OFFSET(0, 0), index)) {
		sharedInfo.evo.base_offset = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_SIZE(0), index)) {
		sharedInfo.evo.base_size = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_STORAGE(0), index)) {
		sharedInfo.evo.base_storage = method.value;
		return B_OK;
	}
	if (decode_head_index(method.method, NVIDIA_GT7XX_EVO_HEAD_SET_PARAMS(0), index)) {
		sharedInfo.evo.base_params = method.value;
		return B_OK;
	}

	return B_NOT_SUPPORTED;
}


status_t
nvidia_gt7xx_init(vesa_info& info)
{
	frame_buffer_boot_info* bootInfo
		= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bootInfo == NULL) {
		dprintf("nvidia_gt7xx: vesa_init no FRAME_BUFFER_BOOT_INFO\n");
		return B_ERROR;
	}

	pci_bar_info mmioBar = {};
	pci_bar_info frameBufferBar = {};
	status_t status = find_graphics_card(bootInfo->physical_frame_buffer,
		info.pci, mmioBar, frameBufferBar);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: vesa_init find_graphics_card failed status=%" B_PRId32 "\n",
			status);
		return status;
	}

	dprintf("nvidia_gt7xx: vesa_init matched pci %02x:%02x.%01x vendor=%04x device=%04x"
		" mmio_bar=%" B_PRId32 " mmio_base=0x%016" B_PRIx64 " mmio_size=0x%016" B_PRIx64
		" fb_bar=%" B_PRId32 " fb_base=0x%016" B_PRIx64 " fb_size=0x%016" B_PRIx64 "\n",
		info.pci.bus, info.pci.device, info.pci.function,
		info.pci.vendor_id, info.pci.device_id,
		mmioBar.index, mmioBar.base, mmioBar.size,
		frameBufferBar.index, frameBufferBar.base, frameBufferBar.size);

	AreaKeeper sharedKeeper;
	info.shared_area = sharedKeeper.Create("nvidia_gt7xx shared info",
		(void**)&info.shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(vesa_shared_info)), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info.shared_area < B_OK) {
		dprintf("nvidia_gt7xx: vesa_init shared area create failed status=%" B_PRId32 "\n",
			info.shared_area);
		return info.shared_area;
	}

	memset(info.shared_info, 0, sizeof(vesa_shared_info));
	vesa_shared_info& sharedInfo = *info.shared_info;

	sharedInfo.vendor_id = info.pci.vendor_id;
	sharedInfo.device_id = info.pci.device_id;
	strlcpy(sharedInfo.name, device_name(info.pci.vendor_id, info.pci.device_id),
		sizeof(sharedInfo.name));
	sharedInfo.registers_base = mmioBar.base;
	sharedInfo.registers_size = mmioBar.size > UINT32_MAX
		? UINT32_MAX : (uint32)mmioBar.size;
	sharedInfo.mmio_bar = mmioBar.index;
	sharedInfo.frame_buffer_bar = frameBufferBar.index;
	sharedInfo.frame_buffer_base = frameBufferBar.base;
	sharedInfo.frame_buffer_size = frameBufferBar.size > UINT32_MAX
		? UINT32_MAX : (uint32)frameBufferBar.size;
	sharedInfo.physical_frame_buffer = bootInfo->physical_frame_buffer;
	sharedInfo.vram_size = sharedInfo.frame_buffer_size;
	sharedInfo.boot_width = bootInfo->width;
	sharedInfo.boot_height = bootInfo->height;
	sharedInfo.boot_depth = bootInfo->depth;
	sharedInfo.current_mode.virtual_width = bootInfo->width;
	sharedInfo.current_mode.virtual_height = bootInfo->height;
	sharedInfo.current_mode.space = get_color_space_for_depth(bootInfo->depth);
	sharedInfo.current_mode.timing.h_display = bootInfo->width;
	sharedInfo.current_mode.timing.v_display = bootInfo->height;
	sharedInfo.bytes_per_row = bootInfo->bytes_per_row;
	sharedInfo.fbc.frame_buffer = (void*)bootInfo->frame_buffer;
	sharedInfo.fbc.frame_buffer_dma = (void*)bootInfo->physical_frame_buffer;
	sharedInfo.fbc.bytes_per_row = bootInfo->bytes_per_row;
	sharedInfo.frame_buffer_area = info.frame_buffer_area = bootInfo->area;
	info.frame_buffer = bootInfo->frame_buffer;
	info.physical_frame_buffer = bootInfo->physical_frame_buffer;
	info.physical_frame_buffer_size = frameBufferBar.size;
	info.mmio_bar_index = mmioBar.index;
	info.frame_buffer_bar_index = frameBufferBar.index;
	sharedInfo.active_head = 0;
	sharedInfo.dpms_mode = B_DPMS_ON;
	sharedInfo.native_mode_set = false;

	for (uint32 i = 0; i < NVIDIA_GT7XX_EVO_CHANNEL_COUNT; i++) {
		info.channel_push_area[i] = -1;
		info.channel_push_buffer[i] = NULL;
		info.channel_notifier_area[i] = -1;
		info.channel_notifier[i] = NULL;
		info.channel_instance_area[i] = -1;
		info.channel_instance_data[i] = NULL;
	}

	edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (edidInfo != NULL) {
		sharedInfo.has_edid = true;
		sharedInfo.edid_info = *edidInfo;
		sharedInfo.active_output = get_active_output_type(edidInfo);
		sharedInfo.dpms_capabilities = get_dpms_capabilities(edidInfo);
	} else {
		sharedInfo.active_output = NVIDIA_GT7XX_OUTPUT_UNKNOWN;
		sharedInfo.dpms_capabilities = B_DPMS_ON;
	}

	AreaKeeper mmioKeeper;
	info.registers_area = mmioKeeper.Map("nvidia_gt7xx mmio", mmioBar.base,
		mmioBar.size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&info.registers);
	if (info.registers_area < B_OK) {
		dprintf("nvidia_gt7xx: vesa_init mmio map failed status=%" B_PRId32 "\n",
			info.registers_area);
		return info.registers_area;
	}

	sharedInfo.registers_area = info.registers_area;
	dprintf("nvidia_gt7xx: vesa_init mmio mapped area=%" B_PRId32 " regs=%p size=0x%08"
		B_PRIx32 "\n", info.registers_area, info.registers, sharedInfo.registers_size);

	status = init_evo_channel(info, NVIDIA_GT7XX_EVO_CHANNEL_CORE,
		"nvidia_gt7xx evo core", NVIDIA_GT7XX_EVO_CLASS_CORE);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: vesa_init core channel init failed status=%" B_PRId32 "\n",
			status);
		return status;
	}

	status = init_evo_channel(info, NVIDIA_GT7XX_EVO_CHANNEL_BASE,
		"nvidia_gt7xx evo base", NVIDIA_GT7XX_EVO_CLASS_BASE);
	if (status != B_OK) {
		dprintf("nvidia_gt7xx: vesa_init base channel init failed status=%" B_PRId32 "\n",
			status);
		return status;
	}

	dprintf("nvidia_gt7xx: vesa_init success boot=%ux%u depth=%u active_output=%u\n",
		sharedInfo.boot_width, sharedInfo.boot_height, sharedInfo.boot_depth,
		sharedInfo.active_output);

	sharedKeeper.Detach();
	mmioKeeper.Detach();
	return B_OK;
}


void
nvidia_gt7xx_uninit(vesa_info& info)
{
	for (uint32 i = 0; i < NVIDIA_GT7XX_EVO_CHANNEL_COUNT; i++) {
		if (info.channel_push_area[i] >= B_OK) {
			delete_area(info.channel_push_area[i]);
			info.channel_push_area[i] = -1;
		}
		if (info.channel_notifier_area[i] >= B_OK) {
			delete_area(info.channel_notifier_area[i]);
			info.channel_notifier_area[i] = -1;
		}
		if (info.channel_instance_area[i] >= B_OK) {
			delete_area(info.channel_instance_area[i]);
			info.channel_instance_area[i] = -1;
		}
		info.channel_push_buffer[i] = NULL;
		info.channel_notifier[i] = NULL;
		info.channel_instance_data[i] = NULL;
	}

	if (info.registers_area >= B_OK) {
		delete_area(info.registers_area);
		info.registers_area = -1;
	}

	if (info.shared_area >= B_OK) {
		delete_area(info.shared_area);
		info.shared_area = -1;
	}
}


status_t
nvidia_gt7xx_set_display_mode(vesa_info& info, const display_mode& mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	if (!same_mode(mode, info.shared_info->current_mode)) {
		dprintf("nvidia_gt7xx: native modesetting for GK208 is not implemented yet;"
			" requested %ux%u space 0x%08" B_PRIx32 "\n",
			mode.virtual_width, mode.virtual_height, mode.space);
		return B_NOT_SUPPORTED;
	}

	info.shared_info->native_mode_set = true;
	return B_OK;
}


status_t
nvidia_gt7xx_submit_evo(vesa_info& info, const nvidia_gt7xx_evo_push& push)
{
	if (info.shared_info == NULL)
		return B_ERROR;
	if (push.magic != NVIDIA_GT7XX_EVO_MAGIC)
		return B_BAD_VALUE;
	if (push.channel >= NVIDIA_GT7XX_EVO_CHANNEL_COUNT)
		return B_BAD_VALUE;
	if (push.count == 0 || push.count > NVIDIA_GT7XX_EVO_MAX_METHODS)
		return B_BAD_VALUE;

	status_t status = write_dma_push(info, push);
	if (status != B_OK)
		return status;

	for (uint32 i = 0; i < push.count; i++) {
		status = apply_evo_method(*info.shared_info, push.methods[i]);
		if (status != B_OK)
			return status;
	}

	info.shared_info->evo.submit_count
		= info.shared_info->channels[push.channel].submit_count;
	info.shared_info->evo.last_method_count = push.count;
	update_mode_from_evo_state(*info.shared_info);
	status = materialize_hardware_channel(info, push.channel);
	if (status != B_OK)
		return status;
	dprintf("nvidia_gt7xx: submit_evo channel=%" B_PRIu32 " done mode=%ux%u space=0x%08"
		B_PRIx32 " bpr=%" B_PRIu32 "\n",
		push.channel,
		info.shared_info->current_mode.virtual_width,
		info.shared_info->current_mode.virtual_height,
		info.shared_info->current_mode.space,
		info.shared_info->bytes_per_row);
	info.shared_info->native_mode_set = true;
	return B_OK;
}


status_t
nvidia_gt7xx_get_dpms_mode(vesa_info& info, uint32& mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	mode = info.shared_info->dpms_mode;
	return B_OK;
}


status_t
nvidia_gt7xx_set_dpms_mode(vesa_info& info, uint32 mode)
{
	if (info.shared_info == NULL)
		return B_ERROR;

	if (mode != B_DPMS_ON)
		return B_NOT_SUPPORTED;

	info.shared_info->dpms_mode = mode;
	return B_OK;
}
