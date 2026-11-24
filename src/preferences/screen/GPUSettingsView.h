/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef GPU_SETTINGS_VIEW_H
#define GPU_SETTINGS_VIEW_H


#include <String.h>
#include <View.h>
#include <edid.h>

#include "ScreenMode.h"


class BBox;
class BButton;
class BCheckBox;
class BFilePanel;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BStringView;
class MonitorView;
class ScreenSettings;


// Per-GPU tab content for the Screen preflet's secondary cards (every
// screen index other than 0 -- see MULTIGPU_PLAN.md Fase F). The primary
// GPU's tab keeps using ScreenWindow's own original, more elaborate
// controls (combine/swap/laptop-panel/TV-standard, cursor animation
// speed/idle sliders, Apply/Revert staging with undo) unchanged; this
// view is deliberately a simpler "apply immediately" subset -- resolution,
// brightness, hardware cursor, 2D acceleration/debug, EDID dump/load --
// for the GPUs that don't have that machinery. It is a reasonable first
// cut, not a full parity re-implementation: it does not replicate
// combine/swap/laptop-panel/TV-standard or the Apply/Revert/undo staging.
class GPUSettingsView : public BView {
public:
							GPUSettingsView(const char* name, BWindow* window,
								int32 screenIndex, ScreenSettings* settings);
	virtual					~GPUSettingsView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_BuildResolutionMenu();
			void			_RefreshResolutionMenu();
			void			_PopulateResolutionMenu();
			void			_DumpEDID(const entry_ref* dirRef,
								const char* name);
			void			_LoadEDID(const entry_ref* ref);
			bool			_QueryHasEDID() const;
			void			_RefreshEdidGatedControls();
			void			_OfferToSaveStableEdid(const edid1_info& edid);

			BWindow*		fWindow;
			int32			fScreenIndex;
			BString			fBdf;
			ScreenSettings*	fSettings;
			ScreenMode		fScreenMode;

			// Hardware capability (fixed at construction -- these never
			// change at runtime): whether the accelerant exposes the
			// hooks at all. Combined with the dynamic EDID-presence
			// check (_QueryHasEDID(), re-checked after Load/Unset EDID)
			// to decide each control's actual enabled state.
			bool			fHasSetCursorBitmap;
			bool			fHas2DAcceleration;

			BStringView*	fDeviceInfo;
			MonitorView*	fMonitorView;

			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;

			BSlider*		fBrightnessSlider;
			BCheckBox*		fHardwareCursorCheckbox;
			BCheckBox*		f2DAccelerationCheckbox;
			BCheckBox*		f2DDebugCheckbox;

			BButton*		fDumpEDIDButton;
			BButton*		fLoadEDIDButton;
			BButton*		fUnsetEDIDButton;
			BFilePanel*		fDumpFilePanel;
			BFilePanel*		fLoadFilePanel;
};

#endif	/* GPU_SETTINGS_VIEW_H */
