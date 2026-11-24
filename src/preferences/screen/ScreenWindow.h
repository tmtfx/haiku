/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini, burton666@libero.it
 *		Axel Dörfler, axeld@pinc-software.de
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */
#ifndef SCREEN_WINDOW_H
#define SCREEN_WINDOW_H


#include <Window.h>

#include <String.h>
#include <StringList.h>
#include <edid.h>

#include "ScreenMode.h"


class BBox;
class BPopUpMenu;
class BMenuField;
class BSlider;
class BSpinner;
class BStringView;
class BCheckBox;
class BFilePanel;

class BButton;
class BListView;
class BTabView;

class RefreshWindow;
class MonitorView;
class ScreenSettings;
class WorkspaceGridView;


class ScreenWindow : public BWindow {
public:
							ScreenWindow(ScreenSettings *settings);
	virtual					~ScreenWindow();

	virtual	bool			QuitRequested();
	virtual	void			MessageReceived(BMessage *message);
	virtual	void			WorkspaceActivated(int32 ws, bool state);
	virtual	void			ScreenChanged(BRect frame, color_space mode);

private:
			void			_BuildSupportedColorSpaces();

			void			_CheckApplyEnabled();
			void			_CheckResolutionMenu();
			void			_CheckColorMenu();
			void			_CheckRefreshMenu();

			void			_UpdateActiveMode();
			void			_UpdateActiveMode(int32 workspace);
			void			_UpdateWorkspaceButtons();
			void			_UpdateRefreshControl();
			void			_UpdateMonitorView();
			void			_UpdateControls();
			void			_UpdateOriginal();
			void			_UpdateMonitor();
			void			_UpdateColorLabel();

			void			_Apply();

			void			_UpdateWorkspaceGridView();
			void			_GetEffectiveGpuOrder(BStringList& names,
								BStringList& bdfs) const;
			void			_RebuildGpuOrderListView();
			void			_MoveSelectedGpu(int32 delta);

			void			_DumpEDID(const entry_ref* dirRef, const char* name);
			void			_LoadEDID(const entry_ref* ref);
			void			_OfferToSaveStableEdid(const edid1_info& edid);
			bool			_QueryHasEDID(int32 screenIndex) const;
			void			_RefreshEdidGatedControls();
			bool			_AllActiveGpusHaveHardwareCursor() const;

			status_t		_WriteVesaModeFile(const screen_mode& mode) const;
			bool			_IsVesa() const { return fIsVesa; }

private:
			ScreenSettings*	fSettings;
			// Which GPU/screen index this window's per-card controls
			// target (gScreenManager::ScreenAt() order -- see
			// MULTIGPU_PLAN.md Fase F). Always 0: this window's full
			// control set (combine/swap/laptop-panel/TV-standard/Apply-
			// Revert-undo) is the primary GPU's tab; every other screen
			// gets a GPUSettingsView tab instead.
			int32			fScreenIndex;
			// Multi-GPU: the key ScreenSettings' per-GPU preferences are
			// persisted under for this screen -- its PCI BDF if
			// resolvable, else a fallback "idx<N>" (see
			// gpu_settings_key() and MULTIGPU_PLAN.md EDID section: this
			// is deliberately NOT the same thing as fScreenIndex, which
			// only addresses the currently-running HWInterface and isn't
			// stable across a reboot that changes /dev/graphics/*
			// enumeration order).
			BString			fScreenKey;
			bool			fIsVesa;
			bool			fBootWorkspaceApplied;
			bool			fHasCursorAnimationCapability;

			BBox*			fScreenBox;
			BStringView*	fDeviceInfo;
			MonitorView*	fMonitorView;
			BMenuItem*		fAllWorkspacesItem;

			// Multi-GPU (MULTIGPU_PLAN.md Fase F): the "Workspaces" tab
			// (rows/columns + grouped monitor-grid preview + the
			// Desktop-wide compositor checkbox) and one tab per detected
			// GPU (gScreenManager::ScreenAt() order). This window's own,
			// original controls below (fScreenBox, fResolutionField,
			// etc.) now live in the tab for screen index 0 only; every
			// other screen gets a simpler GPUSettingsView tab (see that
			// class) instead of a full duplicate of this window's
			// Apply/Revert/undo machinery.
			BTabView*			fTabView;
			WorkspaceGridView*	fWorkspaceGridView;

			// Multi-GPU (MULTIGPU_PLAN.md Fase A, user-configured
			// variant): lets the user pick which GPU is primary and the
			// order of the rest, persisted via ScreenSettings::
			// SetGpuOrder(). fGpuOrderBdfs is kept parallel to
			// fGpuOrderListView's rows (same index = same GPU).
			BListView*			fGpuOrderListView;
			BButton*			fGpuOrderUpButton;
			BButton*			fGpuOrderDownButton;
			BStringList			fGpuOrderBdfs;

			BSpinner*		fColumnsControl;
			BSpinner*		fRowsControl;

			uint32			fSupportedColorSpaces;
			BMenuItem*		fUserSelectedColorSpace;

			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;
			BPopUpMenu*		fColorsMenu;
			BMenuField*		fColorsField;
			BPopUpMenu*		fRefreshMenu;
			BMenuField*		fRefreshField;
			BMenuItem*		fOtherRefresh;

			BPopUpMenu*		fCombineMenu;
			BMenuField*		fCombineField;
			BPopUpMenu*		fSwapDisplaysMenu;
			BMenuField*		fSwapDisplaysField;
			BPopUpMenu*		fUseLaptopPanelMenu;
			BMenuField*		fUseLaptopPanelField;
			BPopUpMenu*		fTVStandardMenu;
			BMenuField*		fTVStandardField;

			BSlider*		fBrightnessSlider;
			BCheckBox*		fHardwareCursorCheckbox;
			BCheckBox*		fCursorAnimationCheckbox;
			BSlider*		fCursorAnimationSpeedSlider;
			BSlider*		fCursorAnimationIdleSlider;
			BCheckBox*		f2DAccelerationCheckbox;
			BCheckBox*		f2DDebugCheckbox;
			BCheckBox*		f2DCompositorCheckbox;

			BButton*		fDefaultsButton;
			BButton*		fApplyButton;
			BButton*		fRevertButton;

			ScreenMode		fScreenMode;
			ScreenMode		fUndoScreenMode;
				// screen modes for all workspaces

			screen_mode		fActive, fSelected, fOriginal;
				// screen modes for the current workspace

			uint32			fOriginalWorkspacesColumns;
			uint32			fOriginalWorkspacesRows;
			float			fOriginalBrightness;
			bool			fModified;

			BButton*		fDumpEDIDButton;
			BButton*		fLoadEDIDButton;
			BFilePanel*		fDumpFilePanel;
			BFilePanel*		fLoadFilePanel;
};

#endif	/* SCREEN_WINDOW_H */
