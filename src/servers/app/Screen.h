/*
 * Copyright 2001-2009, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Adi Oanca <adioanca@myrealbox.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus, <superstippi@gmx.de>
 */
#ifndef SCREEN_H
#define SCREEN_H


#include <AutoDeleter.h>
#include <Accelerant.h>
#include <GraphicsDefs.h>
#include <Point.h>
#include <Region.h>


class DrawingEngine;
class HWInterface;

class Screen {
public:
								Screen(::HWInterface *interface, int32 id);
								Screen();
	virtual						~Screen();

			status_t			Initialize();
			void				Shutdown();

			int32				ID() const { return fID; }
			status_t			GetMonitorInfo(monitor_info& info) const;

			status_t			SetMode(const display_mode& mode);
			status_t			SetMode(uint16 width, uint16 height,
									uint32 colorspace,
									const display_timing& timing);
			status_t			SetPreferredMode();
			status_t			SetBestMode(uint16 width, uint16 height,
									uint32 colorspace, float frequency,
									bool strict = true);

			void				GetMode(display_mode& mode) const;
			void				GetMode(uint16 &width, uint16 &height,
									uint32 &colorspace, float &frequency) const;

			void				SetFrame(const BRect& rect);

			BRect				Frame() const;
			color_space			ColorSpace() const;

	inline	DrawingEngine*		GetDrawingEngine() const
									{ return fDriver.Get(); }
	inline	::HWInterface*		HWInterface() const
									{ return fHWInterface.Get(); }

			// Multi-GPU: which workspace this screen is currently showing
			// live, independent of whichever workspace has global
			// keyboard/mouse focus (Desktop::fCurrentWorkspace) -- see
			// Desktop::ScreenForWorkspace(). -1 means "not currently
			// assigned a workspace row" (not in Desktop::ActiveGpuScreens()).
			int32				DisplayedWorkspace() const
									{ return fDisplayedWorkspace; }
			void				SetDisplayedWorkspace(int32 workspace)
									{ fDisplayedWorkspace = workspace; }

			// Multi-GPU: this screen's own "what's not covered by any
			// window" staleness cache, used by Desktop::_SetBackground()
			// so each screen tracks its own background independently
			// instead of sharing one Desktop-wide region.
			BRegion&			BackgroundRegion()
									{ return fBackgroundRegion; }

private:
			int32				_FindBestMode(const display_mode* modeList,
									uint32 count, uint16 width, uint16 height,
									uint32 colorspace, float frequency) const;

			int32				fID;
			ObjectDeleter< ::HWInterface>
								fHWInterface;
			ObjectDeleter<DrawingEngine>
								fDriver;
			int32				fDisplayedWorkspace;
			BRegion				fBackgroundRegion;
};

#endif	/* SCREEN_H */
