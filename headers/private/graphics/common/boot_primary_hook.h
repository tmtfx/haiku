/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BOOT_PRIMARY_HOOK_H
#define _BOOT_PRIMARY_HOOK_H


#include <add-ons/graphics/Accelerant.h>


/*!	Multi-GPU: an optional accelerant hook (private/vendor range, see
	B_ACCELERANT_PRIVATE_START) any accelerant may implement to answer
	"did the boot loader actually use THIS card?" -- i.e. is it the real
	boot-primary, not just whichever screen happens to be
	gScreenManager's discovery-order index 0.

	Only kernel-space code can call get_boot_item(), so an accelerant
	implementing this hook does so by asking its own kernel driver via a
	private ioctl (see e.g. ATI_IS_BOOT_PRIMARY in
	headers/private/graphics/ati/DriverInterface.h, and
	boot_framebuffer_belongs_to_this_device() in ati/driver.cpp, which
	answers the actual question).

	An accelerant that doesn't implement this hook simply isn't asked --
	get_accelerant_hook() returns NULL for it like any other unsupported
	optional hook, and callers (AccelerantHWInterface::IsBootPrimary())
	treat that as "unknown", not "false". AppServer.cpp's fallback
	primary-GPU logic only uses this as a last resort, when the user
	hasn't configured a GPU order manually (see MULTIGPU_PLAN.md Fase A).
*/
#define B_IS_BOOT_PRIMARY_PIRATI	((uint32)B_ACCELERANT_PRIVATE_START + 1)

typedef bool (*is_boot_primary)(void);


#endif	/* _BOOT_PRIMARY_HOOK_H */
