#ifndef DIRECT_CAPTURE_H
#define DIRECT_CAPTURE_H

#include <OS.h>
#include <SupportDefs.h>

#include "DriverInterface.h"

class VideoView;


class DirectCapture {
public:
								DirectCapture();
								~DirectCapture();

			status_t			Start(const char* devicePath,
									uint32 input, uint32 standard,
									uint32 width, uint32 height,
									VideoView* targetView);
			void				Stop();
			bool				IsRunning() const { return fRunning; }

			status_t			SetInput(uint32 input);
			status_t			SetStandard(uint32 standard);
			status_t			SetResolution(uint32 width, uint32 height);
			status_t			SetControls(const bttv_video_controls& controls);
			status_t			GetControls(bttv_video_controls* outControls);
			status_t			GetCardInfo(bttv_card_info* outInfo);

private:
	static	status_t			_CaptureThreadEntry(void* data);
			void				_CaptureLoop();

private:
			int					fDriverFD;
			thread_id			fThread;
			volatile bool		fRunning;

			bttv_capture_format	fFormat;
			VideoView*			fTargetView;
};

#endif // DIRECT_CAPTURE_H
