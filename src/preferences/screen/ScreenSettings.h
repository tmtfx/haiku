/*
 * Copyright 2001-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
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

	private:
		BRect fWindowFrame;
		bool fHardwareCursorEnabled;
};

#endif	// SCREEN_SETTINGS_H
