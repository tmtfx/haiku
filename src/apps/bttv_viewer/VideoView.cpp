#include "VideoView.h"

#include <Autolock.h>
#include <Font.h>
#include <String.h>
#include <Window.h>


static inline uint8
clamp_u8(int32 v)
{
	if (v < 0)
		return 0;
	if (v > 255)
		return 255;
	return (uint8)v;
}


VideoView::VideoView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fBitmapLock("video bitmap lock"),
	fBitmap(NULL),
	fWidth(0),
	fHeight(0),
	fFrameCount(0),
	fLastFpsCalcTime(0),
	fFramesAtLastFps(0),
	fCurrentFps(0.0f)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


VideoView::~VideoView()
{
	BAutolock locker(&fBitmapLock);
	delete fBitmap;
	fBitmap = NULL;
}


void
VideoView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
VideoView::Clear()
{
	BAutolock locker(&fBitmapLock);
	delete fBitmap;
	fBitmap = NULL;
	fWidth = 0;
	fHeight = 0;
	fFrameCount = 0;
	fCurrentFps = 0.0f;
	fLastFpsCalcTime = 0;
	fFramesAtLastFps = 0;

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoView::SetFrameData(const uint8* yuy2Data, uint32 width, uint32 height)
{
	if (yuy2Data == NULL || width == 0 || height == 0)
		return;

	BAutolock locker(&fBitmapLock);

	if (fBitmap == NULL || fWidth != width || fHeight != height) {
		delete fBitmap;
		fWidth = width;
		fHeight = height;
		fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (fBitmap == NULL || fBitmap->InitCheck() != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
			return;
		}
	}

	_ConvertYUY2ToRGB32(yuy2Data, (uint8*)fBitmap->Bits(), width, height);

	fFrameCount++;
	bigtime_t now = system_time();
	if (fLastFpsCalcTime == 0) {
		fLastFpsCalcTime = now;
		fFramesAtLastFps = fFrameCount;
	} else if (now - fLastFpsCalcTime >= 500000) {
		bigtime_t elapsed = now - fLastFpsCalcTime;
		uint64 frames = fFrameCount - fFramesAtLastFps;
		fCurrentFps = (float)frames * 1000000.0f / (float)elapsed;
		fLastFpsCalcTime = now;
		fFramesAtLastFps = fFrameCount;
	}

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


void
VideoView::GetStats(uint64* outFrames, float* outFps, uint32* outWidth,
	uint32* outHeight)
{
	BAutolock locker(&fBitmapLock);
	if (outFrames != NULL)
		*outFrames = fFrameCount;
	if (outFps != NULL)
		*outFps = fCurrentFps;
	if (outWidth != NULL)
		*outWidth = fWidth;
	if (outHeight != NULL)
		*outHeight = fHeight;
}


void
VideoView::Draw(BRect updateRect)
{
	BAutolock locker(&fBitmapLock);

	if (fBitmap == NULL || !fBitmap->IsValid()) {
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
		FillRect(updateRect);

		SetHighUIColor(B_PANEL_TEXT_COLOR);
		const char* text = "No Video Signal / Capture Stopped";
		font_height fh;
		GetFontHeight(&fh);
		float strWidth = StringWidth(text);
		DrawString(text, BPoint((Bounds().Width() - strWidth) / 2.0f,
			(Bounds().Height() + fh.ascent) / 2.0f));
		return;
	}

	BRect bounds = Bounds();
	float viewRatio = bounds.Width() / bounds.Height();
	float bmpRatio = (float)fWidth / (float)fHeight;
	BRect dstRect = bounds;

	if (viewRatio > bmpRatio) {
		float scaledWidth = bounds.Height() * bmpRatio;
		dstRect.left = (bounds.Width() - scaledWidth) / 2.0f;
		dstRect.right = dstRect.left + scaledWidth;
	} else {
		float scaledHeight = bounds.Width() / bmpRatio;
		dstRect.top = (bounds.Height() - scaledHeight) / 2.0f;
		dstRect.bottom = dstRect.top + scaledHeight;
	}

	// Black background for letterbox / pillarbox
	SetHighColor(0, 0, 0);
	if (dstRect.top > bounds.top)
		FillRect(BRect(bounds.left, bounds.top, bounds.right, dstRect.top - 1));
	if (dstRect.bottom < bounds.bottom)
		FillRect(BRect(bounds.left, dstRect.bottom + 1, bounds.right, bounds.bottom));
	if (dstRect.left > bounds.left)
		FillRect(BRect(bounds.left, bounds.top, dstRect.left - 1, bounds.bottom));
	if (dstRect.right < bounds.right)
		FillRect(BRect(dstRect.right + 1, bounds.top, bounds.right, bounds.bottom));

	DrawBitmap(fBitmap, fBitmap->Bounds(), dstRect, B_FILTER_BITMAP_BILINEAR);
}


void
VideoView::_ConvertYUY2ToRGB32(const uint8* src, uint8* dst, uint32 width,
	uint32 height)
{
	uint32 pixelCount = width * height;

	for (uint32 i = 0; i < pixelCount; i += 2) {
		int32 y0 = src[0];
		int32 u  = (int32)src[1] - 128;
		int32 y1 = src[2];
		int32 v  = (int32)src[3] - 128;
		src += 4;

		int32 rOffset = (359 * v) >> 8;
		int32 gOffset = (88 * u + 183 * v) >> 8;
		int32 bOffset = (454 * u) >> 8;

		// Pixel 0 (B, G, R, A)
		dst[0] = clamp_u8(y0 + bOffset);
		dst[1] = clamp_u8(y0 - gOffset);
		dst[2] = clamp_u8(y0 + rOffset);
		dst[3] = 255;

		// Pixel 1 (B, G, R, A)
		dst[4] = clamp_u8(y1 + bOffset);
		dst[5] = clamp_u8(y1 - gOffset);
		dst[6] = clamp_u8(y1 + rOffset);
		dst[7] = 255;

		dst += 8;
	}
}
