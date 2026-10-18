#include "DirectCapture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "VideoView.h"

static bool sEnableDirectCaptureLog = true;
static const char* const kDirectCaptureLogPath = "/boot/home/bttv_viewer.log";


static void
viewer_log(const char* fmt, ...)
{
	if (!sEnableDirectCaptureLog)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	FILE* f = fopen(kDirectCaptureLogPath, "a");
	if (f != NULL) {
		fputs(buffer, f);
		fflush(f);
		fsync(fileno(f));
		fclose(f);
	}
}


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
	viewer_log("[VIEWER] DirectCapture::~DirectCapture: calling Stop()\n");
	Stop();
}


status_t
DirectCapture::Start(const char* devicePath, uint32 input, uint32 standard,
	uint32 width, uint32 height, VideoView* targetView)
{
	viewer_log("[VIEWER] DirectCapture::Start: ENTER (path=%s, input=%" B_PRIu32 ", std=%" B_PRIu32 ", %" B_PRIu32 "x%" B_PRIu32 ")\n",
		devicePath, input, standard, width, height);

	Stop();

	if (devicePath == NULL || targetView == NULL) {
		viewer_log("[VIEWER] DirectCapture::Start: invalid argument -> B_BAD_VALUE\n");
		return B_BAD_VALUE;
	}

	viewer_log("[VIEWER] DirectCapture::Start: calling open(%s, O_RDWR)...\n", devicePath);
	fDriverFD = open(devicePath, O_RDWR);
	if (fDriverFD < 0) {
		status_t err = errno != 0 ? -errno : B_ERROR;
		viewer_log("[VIEWER] DirectCapture::Start: open FAILED, errno=%d (%s)\n", errno, strerror(err));
		return err;
	}
	viewer_log("[VIEWER] DirectCapture::Start: open SUCCESS, fd=%d\n", fDriverFD);

	memset(&fFormat, 0, sizeof(fFormat));
	fFormat.width = width;
	fFormat.height = height;
	fFormat.video_standard = standard;
	fFormat.input = input;
	fFormat.pixel_format = BTV_PIXEL_FORMAT_YUY2;
	fFormat.bytes_per_line = width * 2;
	fFormat.frame_size = fFormat.bytes_per_line * height;

	viewer_log("[VIEWER] DirectCapture::Start: calling ioctl(BTV_SET_CAPTURE_FORMAT)...\n");
	int ret = ioctl(fDriverFD, BTV_SET_CAPTURE_FORMAT, &fFormat);
	viewer_log("[VIEWER] DirectCapture::Start: BTV_SET_CAPTURE_FORMAT returned %d\n", ret);

	viewer_log("[VIEWER] DirectCapture::Start: calling ioctl(BTV_GET_CAPTURE_FORMAT)...\n");
	ret = ioctl(fDriverFD, BTV_GET_CAPTURE_FORMAT, &fFormat);
	viewer_log("[VIEWER] DirectCapture::Start: BTV_GET_CAPTURE_FORMAT returned %d (actual: %" B_PRIu32 "x%" B_PRIu32 ")\n",
		ret, fFormat.width, fFormat.height);

	viewer_log("[VIEWER] DirectCapture::Start: calling ioctl(BTV_START_CAPTURE)...\n");
	status_t status = ioctl(fDriverFD, BTV_START_CAPTURE, NULL);
	viewer_log("[VIEWER] DirectCapture::Start: BTV_START_CAPTURE returned %d (%s)\n",
		status, strerror(status));
	if (status != B_OK) {
		close(fDriverFD);
		fDriverFD = -1;
		return status;
	}

	fTargetView = targetView;
	fRunning = true;

	viewer_log("[VIEWER] DirectCapture::Start: spawning capture thread...\n");
	fThread = spawn_thread(_CaptureThreadEntry, "bttv direct capture",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fThread < 0) {
		viewer_log("[VIEWER] DirectCapture::Start: spawn_thread FAILED (%s)\n", strerror(fThread));
		ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL);
		close(fDriverFD);
		fDriverFD = -1;
		fRunning = false;
		return fThread;
	}

	viewer_log("[VIEWER] DirectCapture::Start: resuming capture thread (id=%" B_PRId32 ")...\n", fThread);
	resume_thread(fThread);
	viewer_log("[VIEWER] DirectCapture::Start: EXIT SUCCESS\n");
	return B_OK;
}


void
DirectCapture::Stop()
{
	viewer_log("[VIEWER] DirectCapture::Stop: ENTER (running=%d, fd=%d, thread=%" B_PRId32 ")\n",
		fRunning, fDriverFD, fThread);

	if (!fRunning && fDriverFD < 0) {
		viewer_log("[VIEWER] DirectCapture::Stop: already stopped\n");
		return;
	}

	fRunning = false;

	if (fDriverFD >= 0) {
		viewer_log("[VIEWER] DirectCapture::Stop: calling ioctl(BTV_STOP_CAPTURE)...\n");
		ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL);
		viewer_log("[VIEWER] DirectCapture::Stop: BTV_STOP_CAPTURE returned\n");
	}

	if (fThread >= 0) {
		viewer_log("[VIEWER] DirectCapture::Stop: waiting for thread...\n");
		status_t ret;
		wait_for_thread(fThread, &ret);
		viewer_log("[VIEWER] DirectCapture::Stop: thread terminated\n");
		fThread = -1;
	}

	if (fDriverFD >= 0) {
		viewer_log("[VIEWER] DirectCapture::Stop: closing fd %d...\n", fDriverFD);
		close(fDriverFD);
		fDriverFD = -1;
		viewer_log("[VIEWER] DirectCapture::Stop: fd closed\n");
	}

	if (fTargetView != NULL)
		fTargetView->Clear();

	viewer_log("[VIEWER] DirectCapture::Stop: EXIT\n");
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
	viewer_log("[VIEWER] DirectCapture::_CaptureLoop: thread started, bufferSize=%" B_PRIuSIZE "\n",
		fFormat.frame_size);

	size_t currentBufferSize = fFormat.frame_size > 0 ? fFormat.frame_size : 720 * 576 * 2;
	uint8* buffer = new uint8[currentBufferSize];
	uint64 frameIndex = 0;

	while (fRunning) {
		if (fFormat.frame_size != currentBufferSize) {
			delete[] buffer;
			currentBufferSize = fFormat.frame_size;
			buffer = new uint8[currentBufferSize];
		}

		if (frameIndex < 5) {
			viewer_log("[VIEWER] DirectCapture::_CaptureLoop: calling read(fd=%d, size=%" B_PRIuSIZE ")...\n",
				fDriverFD, fFormat.frame_size);
		}

		ssize_t bytes = read(fDriverFD, buffer, fFormat.frame_size);

		if (frameIndex < 5) {
			viewer_log("[VIEWER] DirectCapture::_CaptureLoop: read returned %" B_PRIdSSIZE "\n", bytes);
		}

		if (bytes > 0 && fRunning && fTargetView != NULL) {
			frameIndex++;
			fTargetView->SetFrameData(buffer, fFormat.width, fFormat.height);
		} else if (bytes < 0) {
			if (!fRunning)
				break;
			snooze(10000);
		}
	}

	viewer_log("[VIEWER] DirectCapture::_CaptureLoop: thread exiting, totalFrames=%" B_PRIu64 "\n",
		frameIndex);
	delete[] buffer;
}
