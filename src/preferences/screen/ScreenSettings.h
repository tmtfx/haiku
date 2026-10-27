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


#include <Rect.h>


class ScreenSettings {
	public:
		ScreenSettings();
		virtual ~ScreenSettings();

		BRect WindowFrame() const { return fWindowFrame; };
		void SetWindowFrame(BRect);

		// hardware cursor preference
		bool HardwareCursorEnabled() const { return fHardwareCursorEnabled; };
		void SetHardwareCursorEnabled(bool enabled);

		// 2d acceleration preference
		bool Acceleration2DEnabled() const { return fAcceleration2DEnabled; }
		void SetAcceleration2DEnabled(bool enabled);

		// 2d compositor preference
		bool Compositor2DEnabled() const { return fCompositor2DEnabled; }
		void SetCompositor2DEnabled(bool enabled);
		
		// 2d visual debug preference
        bool Visual2DDebugEnabled() const { return fVisual2DDebugEnabled; }
        void SetVisual2DDebugEnabled(bool enabled);

	private:
		BRect fWindowFrame;
		bool fHardwareCursorEnabled;
		bool fAcceleration2DEnabled;
		bool fCompositor2DEnabled;
		bool fVisual2DDebugEnabled;
};

#endif	// SCREEN_SETTINGS_H
