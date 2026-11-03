/*
 * Minimal example for the "Pirati Del Frico" Haiku fork's optional 2D
 * desktop compositor and per-window real alpha compositing.
 *
 * How to enable the compositor this app relies on:
 *   1. Open the Screen preferences app.
 *   2. Check "Enable hardware cursor bitmap".
 *   3. Check "Enable 2D compositor" (only selectable once the hardware
 *      cursor bitmap option above is on).
 * This can be toggled live, no reboot needed -- Desktop::SetCompositor2DEnabled()
 * (src/servers/app/Desktop.cpp) retroactively allocates/frees a backing
 * store for every existing window as soon as you flip the checkbox.
 *
 * What this app demonstrates:
 *   - A window opts into real per-pixel alpha compositing by setting
 *     kWindowAlphaCompositingFlag (headers/private/interface/WindowPrivate.h)
 *     in its BWindow flags. Without it, Haiku treats window content as
 *     fully opaque regardless of what you draw (see the comment on the
 *     flag itself for why that's the safe default).
 *   - Real, non-255 alpha only ever reaches a window's backing store via
 *     DrawBitmap() in B_OP_COPY mode with a genuine B_RGBA32 source
 *     bitmap. Normal drawing (FillRect, StrokeRect, SetHighColor, etc.)
 *     always writes alpha=255 in this renderer -- see the BLEND/BLEND_OVER
 *     macros in src/servers/app/drawing/Painter/drawing_modes/DrawingMode.h.
 *     That's why AlphaView below builds its own bitmap by hand instead of
 *     just filling a rect with a translucent color.
 *   - Dragging the slider below re-draws the whole view every time, so
 *     the previous frame is always fully replaced rather than blended
 *     with itself (compositing a window's content area always has to be
 *     a complete repaint, not an incremental patch, to look right).
 */

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Slider.h>
#include <Bitmap.h>
#include <Message.h>
#include <StringView.h>
#include <WindowPrivate.h>


#define MSG_ALPHA_CHANGED 'alfa'


class AlphaView : public BView {
public:
	AlphaView(BRect frame)
		: BView(frame, "alphaView", B_FOLLOW_ALL, B_WILL_DRAW)
		, mAlpha(128)
	{
		SetViewColor(B_TRANSPARENT_32_BIT);
	}

	void SetAlpha(uint8 alpha)
	{
		mAlpha = alpha;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds = Bounds();
		int32 width = bounds.IntegerWidth() + 1;
		int32 height = bounds.IntegerHeight() + 1;
		if (width <= 0 || height <= 0)
			return;

		BBitmap bitmap(bounds, B_RGBA32);
		uint8* bits = (uint8*)bitmap.Bits();
		int32 bpr = bitmap.BytesPerRow();

		for (int32 y = 0; y < height; y++) {
			uint32* row = (uint32*)(bits + y * bpr);
			for (int32 x = 0; x < width; x++) {
				// diagonal stripes, so it's obvious if an old frame
				// isn't being fully replaced by a new one
				bool stripe = ((x + y) / 16) % 2 == 0;
				uint8 r = stripe ? 220 : 60;
				uint8 g = stripe ? 90  : 140;
				uint8 b = stripe ? 60  : 220;
				row[x] = (uint32(mAlpha) << 24) | (uint32(r) << 16)
					| (uint32(g) << 8) | b;
			}
		}

		SetDrawingMode(B_OP_COPY);
		DrawBitmap(&bitmap, bounds);
	}

private:
	uint8 mAlpha;
};


class AlphaCompositingWindow : public BWindow {
public:
	AlphaCompositingWindow()
		: BWindow(BRect(120, 120, 560, 420), "Alpha Compositing Test",
			B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS | kWindowAlphaCompositingFlag)
	{
		BRect bounds = Bounds();

		BRect labelFrame(bounds.left + 10, bounds.bottom - 58,
			bounds.right - 10, bounds.bottom - 42);
		BStringView* label = new BStringView(labelFrame, "label",
			"kWindowAlphaCompositingFlag is set on this window");
		label->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);

		BRect sliderFrame(bounds.left + 10, bounds.bottom - 40,
			bounds.right - 10, bounds.bottom - 10);
		mSlider = new BSlider(sliderFrame, "alphaSlider",
			"Alpha (0 = see-through, 255 = opaque)", NULL, 0, 255);
		mSlider->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
		mSlider->SetValue(128);
		mSlider->SetModificationMessage(new BMessage(MSG_ALPHA_CHANGED));
		mSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		mSlider->SetHashMarkCount(9);

		mAlphaView = new AlphaView(BRect(bounds.left, bounds.top,
			bounds.right, labelFrame.top - 5));

		AddChild(mAlphaView);
		AddChild(label);
		AddChild(mSlider);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
		case MSG_ALPHA_CHANGED:
			mAlphaView->SetAlpha((uint8)mSlider->Value());
			break;
		default:
			BWindow::MessageReceived(message);
		}
	}

	virtual bool QuitRequested()
	{
		be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

private:
	AlphaView* mAlphaView;
	BSlider* mSlider;
};


int
main()
{
	BApplication app("application/x-vnd.Haiku-AlphaCompositingTest");
	(new AlphaCompositingWindow())->Show();
	app.Run();
	return 0;
}
