/*
 * Copyright 2005-2008, Jérôme Duval, jerome.duval@free.fr.
 * Distributed under the terms of the MIT License.
 */
#ifndef _WINDOW_PRIVATE_H
#define _WINDOW_PRIVATE_H


#include <Window.h>


/* Private window looks */

const window_look kDesktopWindowLook = window_look(4);
const window_look kLeftTitledWindowLook = window_look(25);

/* Private window feels */

const window_feel kDesktopWindowFeel = window_feel(1024);
const window_feel kMenuWindowFeel = window_feel(1025);
const window_feel kWindowScreenFeel = window_feel(1026);
const window_feel kPasswordWindowFeel = window_feel(1027);
const window_feel kOffscreenWindowFeel = window_feel(1028);

/* Private window types */

const window_type kWindowScreenWindow = window_type(1026);

/* Private window flags */

const uint32 kWindowScreenFlag = 0x10000;
const uint32 kAcceptKeyboardFocusFlag = 0x40000;
	// Accept keyboard input even if B_AVOID_FOCUS is set
const uint32 kWindowAlphaCompositingFlag = 0x02000000;
	// Opt this window's content area into real per-pixel alpha
	// compositing against whatever is beneath it when the desktop's
	// 2D compositor is enabled, instead of the default behavior of
	// treating window content as fully opaque. Ordinary windows must
	// NOT set this: Haiku's vector renderer forces alpha=255 for all
	// normal drawing, but incrementally-redrawn/untouched backing
	// store regions can still contain stale or zero-alpha bytes that
	// are meant to be fully overwritten, not blended -- blending them
	// leaves visible trails (see Desktop::CompositeDirtyRegion). Only
	// a window that deliberately manages its own alpha channel for a
	// translucent appearance (e.g. a shaped/transparent overlay)
	// should request this.

#endif // _WINDOW_PRIVATE_H
