/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SERVER_CURSOR_H
#define SERVER_CURSOR_H


#include "ServerBitmap.h"

#include <Point.h>
#include <String.h>


class CursorManager;


class ServerCursor : public ServerBitmap {
public:
	enum {
		B_CURSOR_ANI_TRIGGER_IMMEDIATE = 0,
		B_CURSOR_ANI_TRIGGER_IDLE = 1,
		B_CURSOR_MAX_ANIMATION_FRAMES = 16
	};

								ServerCursor(BRect r, color_space space,
									int32 flags, BPoint hotspot,
									int32 bytesPerRow = -1,
									screen_id screen = B_MAIN_SCREEN_ID);
								ServerCursor(const uint8* cursorDataFromR5);
								ServerCursor(const uint8* alreadyPaddedData,
									uint32 width, uint32 height,
									color_space format);
								ServerCursor(const ServerCursor* cursor);

	virtual						~ServerCursor();

	//! Returns the cursor's hot spot
			void				SetHotSpot(BPoint pt);
			BPoint				GetHotSpot() const
									{ return fHotSpot; }

			void				SetOwningTeam(team_id tid)
									{ fOwningTeam = tid; }
			team_id				OwningTeam() const
									{ return fOwningTeam; }

			int32				Token() const
									{ return fToken; }

			void				AttachedToManager(CursorManager* manager);

			const uint8*		CursorData() const
									{ return fCursorData; }

			status_t			ConfigureAnimation(uint32 frameCount,
									uint32 frameDelayMs, uint32 startDelayMs,
									uint32 idleDelayMs, uint32 trigger);
			status_t			SetAnimationFrame(uint32 frameIndex,
									const uint8* data, uint32 bytesPerRow,
									color_space colorSpace, size_t dataSize);
			bool				HasAnimation() const
									{ return fAnimationFrameCount > 1; }
			uint32				AnimationFrameCount() const
									{ return fAnimationFrameCount; }
			uint32				AnimationFrameDelayMs() const
									{ return fAnimationFrameDelayMs; }
			uint32				AnimationStartDelayMs() const
									{ return fAnimationStartDelayMs; }
			uint32				AnimationIdleDelayMs() const
									{ return fAnimationIdleDelayMs; }
			uint32				AnimationTrigger() const
									{ return fAnimationTrigger; }
			bool				GetAnimationFrame(uint32 frameIndex,
									const uint8*& bits, uint16& width,
									uint16& height, uint16& bytesPerRow,
									color_space& colorSpace) const;

private:
	friend class CursorManager;

	struct animation_frame {
		animation_frame()
			:
			bits(NULL),
			bitsLength(0),
			bytesPerRow(0),
			colorSpace(B_NO_COLOR_SPACE)
		{
		}

		uint8*		bits;
		size_t		bitsLength;
		uint16		bytesPerRow;
		color_space	colorSpace;
	};

			void				_ResetAnimationFrames();
			void				_CopyAnimationFrom(const ServerCursor& other);

			BPoint				fHotSpot;
			team_id				fOwningTeam;
			uint8*				fCursorData;
			CursorManager*		fManager;
			uint32				fAnimationFrameCount;
			uint32				fAnimationFrameDelayMs;
			uint32				fAnimationStartDelayMs;
			uint32				fAnimationIdleDelayMs;
			uint32				fAnimationTrigger;
			animation_frame		fAnimationFrames[B_CURSOR_MAX_ANIMATION_FRAMES - 1];
};


typedef BReference<ServerCursor> ServerCursorReference;


#endif	// SERVER_CURSOR_H
