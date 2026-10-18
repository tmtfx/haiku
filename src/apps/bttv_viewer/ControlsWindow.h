#ifndef CONTROLS_WINDOW_H
#define CONTROLS_WINDOW_H

#include <Messenger.h>
#include <Slider.h>
#include <Window.h>

#include "DriverInterface.h"

enum {
	kMsgControlsChanged = 'ctch',
	kMsgResetControls   = 'ctrs'
};


class ControlsWindow : public BWindow {
public:
								ControlsWindow(BMessenger target,
									const bttv_video_controls& initial);
	virtual						~ControlsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			void				SetControls(const bttv_video_controls& controls);

private:
			void				_NotifyTarget();

private:
			BMessenger			fTarget;
			BSlider*			fBrightnessSlider;
			BSlider*			fContrastSlider;
			BSlider*			fSaturationSlider;
			BSlider*			fHueSlider;
};

#endif // CONTROLS_WINDOW_H
