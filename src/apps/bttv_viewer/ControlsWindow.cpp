#include "ControlsWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>


ControlsWindow::ControlsWindow(BMessenger target,
	const bttv_video_controls& initial)
	:
	BWindow(BRect(150, 150, 420, 430), "Picture Controls",
		B_FLOATING_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target)
{
	fBrightnessSlider = new BSlider("brightness", "Brightness",
		new BMessage(kMsgControlsChanged), -128, 127, B_HORIZONTAL);
	fBrightnessSlider->SetValue(initial.brightness);
	fBrightnessSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fBrightnessSlider->SetHashMarkCount(9);

	fContrastSlider = new BSlider("contrast", "Contrast",
		new BMessage(kMsgControlsChanged), 0, 255, B_HORIZONTAL);
	fContrastSlider->SetValue(initial.contrast);
	fContrastSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fContrastSlider->SetHashMarkCount(9);

	fSaturationSlider = new BSlider("saturation", "Saturation",
		new BMessage(kMsgControlsChanged), 0, 255, B_HORIZONTAL);
	fSaturationSlider->SetValue(initial.saturation);
	fSaturationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fSaturationSlider->SetHashMarkCount(9);

	fHueSlider = new BSlider("hue", "Hue",
		new BMessage(kMsgControlsChanged), -128, 127, B_HORIZONTAL);
	fHueSlider->SetValue(initial.hue);
	fHueSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fHueSlider->SetHashMarkCount(9);

	BButton* resetButton = new BButton("reset", "Defaults",
		new BMessage(kMsgResetControls));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fBrightnessSlider)
		.Add(fContrastSlider)
		.Add(fSaturationSlider)
		.Add(fHueSlider)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(resetButton)
		.End();
}


ControlsWindow::~ControlsWindow()
{
}


void
ControlsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgControlsChanged:
			_NotifyTarget();
			break;

		case kMsgResetControls:
			fBrightnessSlider->SetValue(0);
			fContrastSlider->SetValue(216);
			fSaturationSlider->SetValue(128);
			fHueSlider->SetValue(0);
			_NotifyTarget();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
ControlsWindow::QuitRequested()
{
	Hide();
	return false;
}


void
ControlsWindow::SetControls(const bttv_video_controls& controls)
{
	fBrightnessSlider->SetValue(controls.brightness);
	fContrastSlider->SetValue(controls.contrast);
	fSaturationSlider->SetValue(controls.saturation);
	fHueSlider->SetValue(controls.hue);
}


void
ControlsWindow::_NotifyTarget()
{
	BMessage msg(kMsgControlsChanged);
	msg.AddInt32("brightness", fBrightnessSlider->Value());
	msg.AddInt32("contrast", fContrastSlider->Value());
	msg.AddInt32("saturation", fSaturationSlider->Value());
	msg.AddInt32("hue", fHueSlider->Value());
	fTarget.SendMessage(&msg);
}
