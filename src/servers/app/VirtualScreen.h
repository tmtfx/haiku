/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef VIRTUAL_SCREEN_H
#define VIRTUAL_SCREEN_H


#include "ScreenConfigurations.h"
#include "ScreenManager.h"

#include <Message.h>


class Desktop;
class DrawingEngine;
class HWInterface;


class VirtualScreen {
public:
								VirtualScreen();
								~VirtualScreen();

			::DrawingEngine*	DrawingEngine() const
									{ return fDrawingEngine; }

			// Multi-GPU: NOT a multiplexing HWInterface -- deliberately
			// not attempted (tried once as a per-focused-screen resolver
			// on Desktop::HWInterface()/GetDrawingEngine() directly; it
			// broke every unrelated single-screen caller that depends on
			// this being one stable interface, see
			// feedback_narrow_fix_blast_radius). What WAS a real bug:
			// AddScreen() below used to leave this pointing at whichever
			// screen it happened to iterate last, pure discovery-order
			// noise with no relation to which GPU is actually the
			// primary. SetPrimaryInterface() lets Desktop pin this to
			// the screen it has already determined is primary
			// (ActiveGpuScreens().ItemAt(0)), right after
			// _UpdateActiveGpuScreens() computes that -- every existing
			// caller keeps the exact single-stable-interface contract it
			// always had, just now consistently pointed at the primary
			// instead of an arbitrary one.
			::HWInterface*		HWInterface() const
									{ return fHWInterface; }

			void				SetPrimaryInterface(Screen* screen);

			status_t			SetConfiguration(Desktop& desktop,
									const ScreenConfigurations& configurations,
									ScreenConfigurations& currentConfigurations,
									uint32* _changedScreens = NULL,
									Screen* onlyReconfigure = NULL);

			status_t			AddScreen(Screen* screen,
									const ScreenConfigurations& configurations,
									ScreenConfigurations& currentConfigurations,
									bool touchHardware = true);
			status_t			RemoveScreen(Screen* screen);

			void				UpdateFrame();
			BRect				Frame() const;

			// TODO: we need to play with a real multi-screen configuration to
			//	figure out the specifics here
			void				SetScreenFrame(int32 index, BRect frame);

			Screen*				ScreenAt(int32 index) const;
			Screen*				ScreenByID(int32 id) const;
			BRect				ScreenFrameAt(int32 index) const;
			int32				CountScreens() const;

private:
			status_t			_GetMode(Screen* screen,
									const ScreenConfigurations& configurations,
									display_mode& mode) const;
			void				_Reset();

	struct screen_item {
		Screen*	screen;
		BRect	frame;
		// TODO: do we want to have a different color per screen as well?
	};

			BRect				fFrame;
			BObjectList<screen_item, true> fScreenList;
			::DrawingEngine*	fDrawingEngine;
			::HWInterface*		fHWInterface;
};

#endif	/* VIRTUAL_SCREEN_H */
