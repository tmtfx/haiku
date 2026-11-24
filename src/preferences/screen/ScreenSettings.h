/*
 * Copyright 2001-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Fabio Tomat, f.t.public@gmail.com
 */
#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H


#include <Message.h>
#include <Rect.h>
#include <StringList.h>


// Settings persisted for the Screen preflet. Most settings are per-GPU
// (identified by the same "screen" index used by the 'gedi'/'sedi'
// app_server messages, i.e. gScreenManager->ScreenAt(index)); see
// MULTIGPU_PLAN.md Fase F. The compositor is the one exception: it is a
// single Desktop-wide switch (see Fase D/E), so it has no per-screen
// variant.
//
// Stored as a flattened BMessage (Screen_data) instead of the old fixed-
// field-order binary blob, since the set of per-GPU entries is now
// variable-length. This intentionally breaks compatibility with the old
// format -- acceptable here since this is a personal fork, not aiming for
// on-disk compatibility with stock Haiku.
class ScreenSettings {
	public:
		ScreenSettings();
		virtual ~ScreenSettings();

		BRect WindowFrame() const { return fWindowFrame; };
		void SetWindowFrame(BRect);

		// 2D compositor preference (Desktop-wide, not per-screen)
		bool Compositor2DEnabled() const { return fCompositor2DEnabled; }
		void SetCompositor2DEnabled(bool enabled);

		// Multi-GPU (MULTIGPU_PLAN.md Fase A, user-configured variant):
		// manual ordering of GPUs by PCI bus/device/function, formatted
		// as 6 hex digits "bbddff" (same convention as
		// AccelerantHWInterface::GetPciLocation()'s path parsing). The
		// first entry is the primary GPU (row 0's owner); any screen not
		// listed here keeps its discovery-order position, appended after
		// every listed screen. Empty list = no manual order configured,
		// i.e. today's plain discovery-order behavior.
		BStringList GpuOrder() const;
		void SetGpuOrder(const BStringList& order);

		// Multi-GPU: user override forcing a screen to be treated as
		// having no usable EDID regardless of what the driver actually
		// reports (the "Unset EDID" button, secondary GPUs only -- see
		// MULTIGPU_PLAN.md). Keyed by BDF, same convention as GpuOrder().
		bool EdidUnset(const BString& bdf) const;
		void SetEdidUnset(const BString& bdf, bool unset);

		// hardware cursor preference
		bool HardwareCursorEnabled(int32 screen) const;
		void SetHardwareCursorEnabled(int32 screen, bool enabled);

		// 2d acceleration preference
		bool Acceleration2DEnabled(int32 screen) const;
		void SetAcceleration2DEnabled(int32 screen, bool enabled);

		// 2d visual debug preference
		bool Visual2DDebugEnabled(int32 screen) const;
		void SetVisual2DDebugEnabled(int32 screen, bool enabled);

		// animated cursor preferences
		bool CursorAnimationEnabled(int32 screen) const;
		void SetCursorAnimationEnabled(int32 screen, bool enabled);
		int32 CursorAnimationSpeedPercent(int32 screen) const;
		void SetCursorAnimationSpeedPercent(int32 screen, int32 percent);
		int32 CursorAnimationIdleDelayMs(int32 screen) const;
		void SetCursorAnimationIdleDelayMs(int32 screen, int32 delayMs);

	private:
		void _Load();
		void _Save() const;
		static BString _Key(const char* base, int32 screen);

		BRect fWindowFrame;
		bool fCompositor2DEnabled;
		BMessage fPerScreen;
			// flat key/value store, keys built by _Key(), e.g.
			// "hwcursor0", "accel2d1", "cursoranidelay3", ...
};

#endif	// SCREEN_SETTINGS_H
