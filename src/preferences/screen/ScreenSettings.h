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


// Settings persisted for the Screen preflet. Most settings are per-GPU,
// keyed by PCI BDF (see "key" parameters below) rather than by the
// 'gedi'/'sedi'-style "screen" index used for *live* app_server
// messages -- see MULTIGPU_PLAN.md Fase F / EDID section. The two are
// deliberately different: the screen index only identifies which
// currently-running HWInterface instance to notify (meaningless once
// the system reboots and /dev/graphics/* enumeration order may differ,
// e.g. after adding/removing a card), while the BDF key is what
// actually persists correctly across reboots, matching GpuOrder() and
// EdidUnset()'s own convention. The compositor is the one setting with
// neither: it is a single Desktop-wide switch (see Fase D/E), not
// per-GPU at all.
//
// Every setter below therefore takes both a \a key (the BDF to persist
// under, or a caller-chosen fallback such as "idx0" for a card with no
// resolvable PCI location -- e.g. virtio -- see the gpu_settings_key()
// helper duplicated in ScreenWindow.cpp/GPUSettingsView.cpp/
// ScreenManager.cpp) and a \a screenIndex (which *currently* open
// HWInterface instance, if any, to notify live).
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
		bool HardwareCursorEnabled(const BString& key) const;
		void SetHardwareCursorEnabled(const BString& key, int32 screenIndex,
			bool enabled);

		// 2d acceleration preference
		bool Acceleration2DEnabled(const BString& key) const;
		void SetAcceleration2DEnabled(const BString& key, int32 screenIndex,
			bool enabled);

		// 2d visual debug preference
		bool Visual2DDebugEnabled(const BString& key) const;
		void SetVisual2DDebugEnabled(const BString& key, int32 screenIndex,
			bool enabled);

		// animated cursor preferences
		bool CursorAnimationEnabled(const BString& key) const;
		void SetCursorAnimationEnabled(const BString& key, int32 screenIndex,
			bool enabled);
		int32 CursorAnimationSpeedPercent(const BString& key) const;
		void SetCursorAnimationSpeedPercent(const BString& key,
			int32 screenIndex, int32 percent);
		int32 CursorAnimationIdleDelayMs(const BString& key) const;
		void SetCursorAnimationIdleDelayMs(const BString& key,
			int32 screenIndex, int32 delayMs);

	private:
		void _Load();
		void _Save() const;
		static BString _Key(const char* base, const BString& key);

		BRect fWindowFrame;
		bool fCompositor2DEnabled;
		BMessage fPerScreen;
			// flat key/value store, keys built by _Key(), e.g.
			// "hwcursor001200", "accel2d_idx1", "cursoranidelay001200", ...
};

#endif	// SCREEN_SETTINGS_H
