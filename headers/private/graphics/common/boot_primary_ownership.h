/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BOOT_PRIMARY_OWNERSHIP_H
#define _BOOT_PRIMARY_OWNERSHIP_H


/*!	Multi-GPU: shared by every graphics KERNEL driver (not the
	accelerant side -- get_boot_item() is kernel-only, which is exactly
	why this needs to live here instead of in boot_primary_hook.h, the
	hook constant shared with userspace accelerants).

	Answers "does this device own the boot loader's framebuffer?" --
	i.e. is it the actual boot-primary card, the one the firmware POSTed
	and whose video BIOS/mode the boot loader actually used -- as
	opposed to merely being gScreenManager's discovery-order index 0,
	which has nothing to do with physical reality once more than one
	card is installed.

	FRAME_BUFFER_BOOT_INFO/VESA_EDID_BOOT_INFO/etc. describe exactly ONE
	physical framebuffer; every driver's InitDevice()-equivalent sees
	the same boot items regardless of which card actually owns them.
	Blindly trusting "I have a boot item, so it must be about me" is the
	root cause this project found (and fixed) three separate times on
	real multi-GPU hardware: a secondary ATI Rage XL reading a shadowed
	BIOS ROM that wasn't its own (wrong clock table), stealing another
	card's monitor's EDID, and corrupting memory by borrowing another
	card's boot splash geometry. This same BAR-vs-physical_frame_buffer
	check is the fix for all of them, and is also how
	ATI_IS_BOOT_PRIMARY/ENG_IS_BOOT_PRIMARY (see boot_primary_hook.h)
	answer the "am I the real primary?" question used as the last-resort
	fallback when the user hasn't manually configured a GPU order (see
	MULTIGPU_PLAN.md).

	Requires the caller to already have <PCI.h>, <boot_item.h>, and
	<frame_buffer_console.h> included (not included here, to avoid
	pulling kernel-only headers into any file that doesn't already need
	them).
*/
static inline bool
boot_framebuffer_belongs_to_this_device(const pci_info& pciInfo)
{
	struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)
		get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bi == NULL)
		return false;

	for (int i = 0; i < 6; i++) {
		uint32 base = pciInfo.u.h0.base_registers[i];
		uint32 size = pciInfo.u.h0.base_register_sizes[i];
		if (base == 0 || size == 0)
			continue;
		if ((pciInfo.u.h0.base_register_flags[i] & PCI_address_space) != 0)
			continue;		// I/O space BAR, not memory
		if (bi->physical_frame_buffer >= base
				&& bi->physical_frame_buffer < base + size) {
			return true;
		}
	}
	return false;
}


#endif	/* _BOOT_PRIMARY_OWNERSHIP_H */
