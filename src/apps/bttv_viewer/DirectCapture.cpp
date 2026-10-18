#include "DirectCapture.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "VideoView.h"


DirectCapture::DirectCapture()
	:
	fDriverFD(-1),
	fThread(-1),
	fRunning(false),
	fTargetView(NULL)
{
	memset(&fFormat, 0, sizeof(fFormat));
}


DirectCapture::~DirectCapture()
{
	Stop();
}


status_t
DirectCapture::Start(const char* devicePath, uint32 input, uint32 standard,
	uint32 width, uint32 height, VideoView* targetView)
{
	Stop();

	if (devicePath == NULL || targetView == NULL)
		return B_BAD_VALUE;

	fDriverFD = open(devicePath, O_RDWR);
	if (fDriverFD < 0) {
		status_t err = errno != 0 ? -errno : B_ERROR;
		return err;
	}

	memset(&fFormat, 0, sizeof(fFormat));
	fFormat.width = width;
	fFormat.height = height;
	fFormat.video_standard = standard;
	fFormat.input = input;
	fFormat.pixel_format = BTV_PIXEL_FORMAT_YUY2;
	fFormat.bytes_per_line = width * 2;
	fFormat.frame_size = fFormat.bytes_per_line * height;

	ioctl(fDriverFD, BTV_SET_CAPTURE_FORMAT, &fFormat);
	ioctl(fDriverFD, BTV_GET_CAPTURE_FORMAT, &fFormat);

	status_t status = ioctl(fDriverFD, BTV_START_CAPTURE, NULL);
	if (status != B_OK) {
		close(fDriverFD);
		fDriverFD = -1;
		return status;
	}

	fTargetView = targetView;
	fRunning = true;

	fThread = spawn_thread(_CaptureThreadEntry, "bttv direct capture",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fThread < 0) {
		ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL);
		close(fDriverFD);
		fDriverFD = -1;
		fRunning = false;
		return fThread;
	}

	resume_thread(fThread);
	return B_OK;
}


void
DirectCapture::Stop()
{
	if (!fRunning && fDriverFD < 0)
		return;

	fRunning = false;

	if (fDriverFD >= 0) {
		ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL);
	}

	if (fThread >= 0) {
		status_t ret;
		wait_for_thread(fThread, &ret);
		fThread = -1;
	}

	if (fDriverFD >= 0) {
		close(fDriverFD);
		fDriverFD = -1;
	}

	if (fTargetView != NULL)
		fTargetView->Clear();
}


status_t
DirectCapture::SetInput(uint32 input)
{
	if (fDriverFD < 0)
		return B_NO_INIT;
	return ioctl(fDriverFD, BTV_SET_INPUT, &input);
}


status_t
DirectCapture::SetStandard(uint32 standard)
{
	if (fDriverFD < 0)
		return B_NO_INIT;

	bttv_capture_format format = fFormat;
	format.video_standard = standard;
	status_t status = ioctl(fDriverFD, BTV_SET_CAPTURE_FORMAT, &format);
	if (status == B_OK)
		ioctl(fDriverFD, BTV_GET_CAPTURE_FORMAT, &fFormat);
	return status;
}


status_t
DirectCapture::SetResolution(uint32 width, uint32 height)
{
	if (fDriverFD < 0)
		return B_NO_INIT;

	bttv_capture_format format = fFormat;
	format.width = width;
	format.height = height;
	format.bytes_per_line = width * 2;
	format.frame_size = format.bytes_per_line * height;
	status_t status = ioctl(fDriverFD, BTV_SET_CAPTURE_FORMAT, &format);
	if (status == B_OK)
		ioctl(fDriverFD, BTV_GET_CAPTURE_FORMAT, &fFormat);
	return status;
}


status_t
DirectCapture::SetControls(const bttv_video_controls& controls)
{
	if (fDriverFD < 0)
		return B_NO_INIT;
	return ioctl(fDriverFD, BTV_SET_CONTROLS, (void*)&controls);
}


status_t
DirectCapture::GetControls(bttv_video_controls* outControls)
{
	if (fDriverFD < 0 || outControls == NULL)
		return B_BAD_VALUE;
	return ioctl(fDriverFD, BTV_GET_CONTROLS, outControls);
}


status_t
DirectCapture::GetCardInfo(bttv_card_info* outInfo)
{
	if (fDriverFD < 0 || outInfo == NULL)
		return B_BAD_VALUE;
	return ioctl(fDriverFD, BTV_GET_CARD_INFO, outInfo);
}


status_t
DirectCapture::_CaptureThreadEntry(void* data)
{
	DirectCapture* self = (DirectCapture*)data;
	self->_CaptureLoop();
	return B_OK;
}


void
DirectCapture::_CaptureLoop()
{
	size_t currentBufferSize = fFormat.frame_size > 0 ? fFormat.frame_size : 720 * 576 * 2;
	uint8* buffer = new uint8[currentBufferSize];

	while (fRunning) {
		if (fFormat.frame_size != currentBufferSize) {
			delete[] buffer;
			currentBufferSize = fFormat.frame_size;
			buffer = new uint8[currentBufferSize];
		}

		ssize_t bytes = read(fDriverFD, buffer, fFormat.frame_size);
		if (bytes > 0 && fRunning && fTargetView != NULL) {
			fTargetView->SetFrameData(buffer, fFormat.width, fFormat.height);
		} else if (bytes < 0) {
			if (!fRunning)
				break;
			snooze(10000);
		}
	}

	delete[] buffer;
}
