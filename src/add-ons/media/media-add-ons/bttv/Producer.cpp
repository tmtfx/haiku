#include "Producer.h"

#include <fcntl.h>
#include <unistd.h>

#include <Autolock.h>
#include <Buffer.h>
#include <BufferGroup.h>
#include <Debug.h>
#include <MediaAddOn.h>
#include <MediaFormats.h>
#include <ParameterWeb.h>
#include <TimeSource.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>

static const uint32 kMaxFrameWidth = 720;
static const uint32 kMaxFrameHeightPAL = 576;
static const uint32 kMaxFrameHeightNTSC = 480;


BttvProducer::BttvProducer(BMediaAddOn* addOn, const char* name, int32 internalID,
	const char* driverPath)
	:
	BMediaNode(name),
	BBufferProducer(B_MEDIA_RAW_VIDEO),
	BControllable(),
	BMediaEventLooper(),
	fInitStatus(B_NO_INIT),
	fInternalID(internalID),
	fAddOn(addOn),
	fDriverPath(driverPath),
	fDriverFD(-1),
	fLock("bttv producer"),
	fBufferGroup(NULL),
	fOwnsBufferGroup(false),
	fCaptureThread(-1),
	fRunning(false),
	fConnected(false),
	fEnabled(false),
	fProcessingLatency(0),
	fHasPendingFormat(false),
	fSelectedInput(BTV_INPUT_COMPOSITE),
	fSelectedStandard(BTV_STD_PAL),
	fResolutionPreset(RESOLUTION_FULL),
	fInputLastChange(0),
	fStandardLastChange(0),
	fResolutionLastChange(0),
	fBrightnessLastChange(0),
	fContrastLastChange(0),
	fSaturationLastChange(0),
	fHueLastChange(0),
	fFrameSequence(0)
{
	AddNodeKind(B_PHYSICAL_INPUT);
	fOutput = media_output();
	memset(&fConnectedFormat, 0, sizeof(fConnectedFormat));
	memset(&fCaptureFormat, 0, sizeof(fCaptureFormat));

	fInitStatus = _OpenDriver();
	if (fInitStatus != B_OK)
		return;

	fInitStatus = _RefreshCaptureFormat();
	if (fInitStatus != B_OK)
		return;

	fInitStatus = _ApplyCaptureFormat();
	if (fInitStatus != B_OK)
		return;

	fInitStatus = _RefreshControls();
	if (fInitStatus != B_OK)
		return;

	_UpdateOutputFormat();
	fConnectedFormat = fOutput.format.u.raw_video;
	fSelectedInput = fCaptureFormat.input;
	fSelectedStandard = fCaptureFormat.video_standard;
	fResolutionPreset = _ResolutionPresetFor(fCaptureFormat);
	fInputLastChange = fStandardLastChange = fResolutionLastChange = system_time();
	fBrightnessLastChange = fContrastLastChange = fSaturationLastChange
		= fHueLastChange = fInputLastChange;
	fInitStatus = B_OK;
}


BttvProducer::~BttvProducer()
{
	if (fRunning)
		_HandleStop();
	if (fConnected)
		Disconnect(fOutput.source, fOutput.destination);
	else
		_DestroyBufferGroup();
	_CloseDriver();
}


port_id
BttvProducer::ControlPort() const
{
	return BMediaNode::ControlPort();
}


BMediaAddOn*
BttvProducer::AddOn(int32* internalID) const
{
	if (internalID != NULL)
		*internalID = fInternalID;
	return fAddOn;
}


status_t
BttvProducer::HandleMessage(int32 message, const void* data, size_t size)
{
	(void)message;
	(void)data;
	(void)size;
	return B_ERROR;
}


void
BttvProducer::Preroll()
{
}


void
BttvProducer::SetTimeSource(BTimeSource* timeSource)
{
	(void)timeSource;
}


status_t
BttvProducer::RequestCompleted(const media_request_info& info)
{
	return BMediaNode::RequestCompleted(info);
}


void
BttvProducer::NodeRegistered()
{
	if (fInitStatus != B_OK) {
		ReportError(B_NODE_IN_DISTRESS);
		return;
	}

	fOutput.node = Node();
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.destination = media_destination::null;
	snprintf(fOutput.name, sizeof(fOutput.name), "%s output", Name());
	fConnectedFormat = fOutput.format.u.raw_video;
	SetParameterWeb(_CreateParameterWeb());

	Run();
}


void
BttvProducer::Start(bigtime_t performanceTime)
{
	BMediaEventLooper::Start(performanceTime);
}


void
BttvProducer::Stop(bigtime_t performanceTime, bool immediate)
{
	BMediaEventLooper::Stop(performanceTime, immediate);
}


void
BttvProducer::Seek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	BMediaEventLooper::Seek(mediaTime, performanceTime);
}


void
BttvProducer::TimeWarp(bigtime_t atRealTime, bigtime_t toPerformanceTime)
{
	BMediaEventLooper::TimeWarp(atRealTime, toPerformanceTime);
}


status_t
BttvProducer::AddTimer(bigtime_t atPerformanceTime, int32 cookie)
{
	return BMediaEventLooper::AddTimer(atPerformanceTime, cookie);
}


void
BttvProducer::SetRunMode(run_mode mode)
{
	BMediaEventLooper::SetRunMode(mode);
}


void
BttvProducer::HandleEvent(const media_timed_event* event, bigtime_t lateness,
	bool realTimeEvent)
{
	(void)lateness;
	(void)realTimeEvent;

	switch (event->type) {
		case BTimedEventQueue::B_START:
			_HandleStart();
			break;
		case BTimedEventQueue::B_STOP:
			_HandleStop();
			break;
		default:
			break;
	}
}


void
BttvProducer::CleanUpEvent(const media_timed_event* event)
{
	BMediaEventLooper::CleanUpEvent(event);
}


bigtime_t
BttvProducer::OfflineTime()
{
	return BMediaEventLooper::OfflineTime();
}


void
BttvProducer::ControlLoop()
{
	BMediaEventLooper::ControlLoop();
}


status_t
BttvProducer::DeleteHook(BMediaNode* node)
{
	return BMediaEventLooper::DeleteHook(node);
}


status_t
BttvProducer::FormatSuggestionRequested(media_type type, int32 quality,
	media_format* format)
{
	(void)quality;

	if (format == NULL)
		return B_BAD_VALUE;
	if (type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	bttv_capture_format captureFormat = fCaptureFormat;
	if (_SpecializeFormat(format, &captureFormat) != B_OK)
		return B_MEDIA_BAD_FORMAT;

	return B_OK;
}


status_t
BttvProducer::FormatProposal(const media_source& output, media_format* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	bttv_capture_format captureFormat = fCaptureFormat;
	return _SpecializeFormat(format, &captureFormat);
}


status_t
BttvProducer::FormatChangeRequested(const media_source& source,
	const media_destination& destination, media_format* ioFormat,
	int32* deprecated)
{
	(void)source;
	(void)destination;
	(void)ioFormat;
	(void)deprecated;
	return B_ERROR;
}


status_t
BttvProducer::GetNextOutput(int32* cookie, media_output* outOutput)
{
	if (cookie == NULL || outOutput == NULL)
		return B_BAD_VALUE;
	if (*cookie != 0)
		return B_BAD_INDEX;

	*outOutput = fOutput;
	(*cookie)++;
	return B_OK;
}


status_t
BttvProducer::DisposeOutputCookie(int32 cookie)
{
	(void)cookie;
	return B_OK;
}


status_t
BttvProducer::SetBufferGroup(const media_source& forSource, BBufferGroup* group)
{
	if (forSource != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	BAutolock locker(&fLock);
	if (fBufferGroup != NULL && fOwnsBufferGroup)
		delete fBufferGroup;

	fBufferGroup = group;
	fOwnsBufferGroup = false;
	return B_OK;
}


status_t
BttvProducer::VideoClippingChanged(const media_source& forSource,
	int16 numShorts, int16* clipData, const media_video_display_info& display,
	int32* deprecated)
{
	(void)forSource;
	(void)numShorts;
	(void)clipData;
	(void)display;
	(void)deprecated;
	return B_ERROR;
}


status_t
BttvProducer::GetLatency(bigtime_t* outLatency)
{
	if (outLatency == NULL)
		return B_BAD_VALUE;

	*outLatency = EventLatency() + SchedulingLatency() + fProcessingLatency;
	return B_OK;
}


status_t
BttvProducer::PrepareToConnect(const media_source& source,
	const media_destination& destination, media_format* format,
	media_source* outSource, char* outName)
{
	if (format == NULL || outSource == NULL || outName == NULL)
		return B_BAD_VALUE;
	if (fConnected)
		return EALREADY;
	if (source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;
	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;

	bttv_capture_format captureFormat = fCaptureFormat;
	status_t status = _SpecializeFormat(format, &captureFormat);
	if (status != B_OK)
		return status;

	fOutput.destination = destination;
	fPendingCaptureFormat = captureFormat;
	fHasPendingFormat = true;
	*outSource = fOutput.source;
	snprintf(outName, B_MEDIA_NAME_LENGTH, "%s", fOutput.name);
	return B_OK;
}


void
BttvProducer::Connect(status_t error, const media_source& source,
	const media_destination& destination, const media_format& format,
	char* ioName)
{
	if (error != B_OK || source != fOutput.source)
		return;

	if (fHasPendingFormat) {
		if (_ApplyCaptureFormat(fPendingCaptureFormat) == B_OK) {
			_UpdateOutputFormat();
			fHasPendingFormat = false;
		}
	}

	fOutput.destination = destination;
	fConnectedFormat = fOutput.format.u.raw_video;
	snprintf(ioName, B_MEDIA_NAME_LENGTH, "%s", fOutput.name);

	if (_EnsureBufferGroup() != B_OK) {
		fOutput.destination = media_destination::null;
		return;
	}

	bigtime_t downstreamLatency = 0;
	media_node_id timeSourceID = 0;
	FindLatencyFor(fOutput.destination, &downstreamLatency, &timeSourceID);
	SetEventLatency(downstreamLatency + 2000);

	fConnected = true;
	fEnabled = true;
}


void
BttvProducer::Disconnect(const media_source& source,
	const media_destination& destination)
{
	if (!fConnected)
		return;
	if (source != fOutput.source || destination != fOutput.destination)
		return;

	fEnabled = false;
	fConnected = false;
	fHasPendingFormat = false;
	fOutput.destination = media_destination::null;
	_DestroyBufferGroup();
}


void
BttvProducer::LateNoticeReceived(const media_source& source, bigtime_t howMuch,
	bigtime_t performanceTime)
{
	(void)source;
	(void)howMuch;
	(void)performanceTime;
}


void
BttvProducer::EnableOutput(const media_source& source, bool enabled,
	int32* deprecated)
{
	(void)deprecated;

	if (source == fOutput.source)
		fEnabled = enabled;
}


status_t
BttvProducer::SetPlayRate(int32 numer, int32 denom)
{
	(void)numer;
	(void)denom;
	return B_ERROR;
}


void
BttvProducer::AdditionalBufferRequested(const media_source& source,
	media_buffer_id prevBuffer, bigtime_t prevTime, const media_seek_tag* prevTag)
{
	(void)source;
	(void)prevBuffer;
	(void)prevTime;
	(void)prevTag;
}


void
BttvProducer::LatencyChanged(const media_source& source,
	const media_destination& destination, bigtime_t newLatency, uint32 flags)
{
	BBufferProducer::LatencyChanged(source, destination, newLatency, flags);
}


status_t
BttvProducer::GetParameterValue(int32 id, bigtime_t* lastChange, void* value,
	size_t* size)
{
	if (lastChange == NULL || value == NULL || size == NULL)
		return B_BAD_VALUE;

	switch (id) {
		case P_INPUT:
			*lastChange = fInputLastChange;
			*size = sizeof(uint32);
			*((uint32*)value) = fSelectedInput;
			return B_OK;

		case P_STANDARD:
			*lastChange = fStandardLastChange;
			*size = sizeof(uint32);
			*((uint32*)value) = fSelectedStandard;
			return B_OK;

		case P_RESOLUTION:
			*lastChange = fResolutionLastChange;
			*size = sizeof(uint32);
			*((uint32*)value) = fResolutionPreset;
			return B_OK;

		case P_BRIGHTNESS:
			*lastChange = fBrightnessLastChange;
			*size = sizeof(float);
			*((float*)value) = fControls.brightness;
			return B_OK;

		case P_CONTRAST:
			*lastChange = fContrastLastChange;
			*size = sizeof(float);
			*((float*)value) = fControls.contrast;
			return B_OK;

		case P_SATURATION:
			*lastChange = fSaturationLastChange;
			*size = sizeof(float);
			*((float*)value) = fControls.saturation;
			return B_OK;

		case P_HUE:
			*lastChange = fHueLastChange;
			*size = sizeof(float);
			*((float*)value) = fControls.hue;
			return B_OK;

		default:
			return B_BAD_VALUE;
	}
}


void
BttvProducer::SetParameterValue(int32 id, bigtime_t when, const void* value,
	size_t size)
{
	if (value == NULL)
		return;

	uint32 newValue = size == sizeof(uint32) ? *((const uint32*)value) : 0;
	uint32 input = fSelectedInput;
	uint32 standard = fSelectedStandard;
	uint32 resolution = fResolutionPreset;

	switch (id) {
		case P_INPUT:
			if (newValue == fSelectedInput)
				return;
			input = newValue;
			break;

		case P_STANDARD:
			if (newValue == fSelectedStandard)
				return;
			standard = newValue;
			break;

		case P_RESOLUTION:
			if (newValue == fResolutionPreset)
				return;
			resolution = newValue;
			break;

		case P_BRIGHTNESS:
		{
			if (size != sizeof(float))
				return;
			bttv_video_controls controls = fControls;
			controls.brightness = (int32)(*((const float*)value));
			if (_ApplyControls(controls) != B_OK)
				return;
			fBrightnessLastChange = when;
			{
				float current = fControls.brightness;
				BroadcastNewParameterValue(fBrightnessLastChange, P_BRIGHTNESS,
					&current, sizeof(current));
			}
			return;
		}

		case P_CONTRAST:
		{
			if (size != sizeof(float))
				return;
			bttv_video_controls controls = fControls;
			controls.contrast = (int32)(*((const float*)value));
			if (_ApplyControls(controls) != B_OK)
				return;
			fContrastLastChange = when;
			{
				float current = fControls.contrast;
				BroadcastNewParameterValue(fContrastLastChange, P_CONTRAST,
					&current, sizeof(current));
			}
			return;
		}

		case P_SATURATION:
		{
			if (size != sizeof(float))
				return;
			bttv_video_controls controls = fControls;
			controls.saturation = (int32)(*((const float*)value));
			if (_ApplyControls(controls) != B_OK)
				return;
			fSaturationLastChange = when;
			{
				float current = fControls.saturation;
				BroadcastNewParameterValue(fSaturationLastChange, P_SATURATION,
					&current, sizeof(current));
			}
			return;
		}

		case P_HUE:
		{
			if (size != sizeof(float))
				return;
			bttv_video_controls controls = fControls;
			controls.hue = (int32)(*((const float*)value));
			if (_ApplyControls(controls) != B_OK)
				return;
			fHueLastChange = when;
			{
				float current = fControls.hue;
				BroadcastNewParameterValue(fHueLastChange, P_HUE,
					&current, sizeof(current));
			}
			return;
		}

		default:
			return;
	}

	if (size != sizeof(uint32))
		return;



	if (_ApplyCaptureSettings(input, standard, resolution) != B_OK)
		return;

	switch (id) {
		case P_INPUT:
			fInputLastChange = when;
			BroadcastNewParameterValue(fInputLastChange, P_INPUT, &fSelectedInput,
				sizeof(fSelectedInput));
			break;

		case P_STANDARD:
			fStandardLastChange = when;
			BroadcastNewParameterValue(fStandardLastChange, P_STANDARD,
				&fSelectedStandard, sizeof(fSelectedStandard));
			break;

		case P_RESOLUTION:
			fResolutionLastChange = when;
			BroadcastNewParameterValue(fResolutionLastChange, P_RESOLUTION,
				&fResolutionPreset, sizeof(fResolutionPreset));
			break;
	}
}


status_t
BttvProducer::StartControlPanel(BMessenger* outMessenger)
{
	return BControllable::StartControlPanel(outMessenger);
}


status_t
BttvProducer::_OpenDriver()
{
	fDriverFD = open(fDriverPath, O_RDWR);
	return fDriverFD >= 0 ? B_OK : B_ERROR;
}


void
BttvProducer::_CloseDriver()
{
	if (fDriverFD >= 0) {
		close(fDriverFD);
		fDriverFD = -1;
	}
}


status_t
BttvProducer::_RefreshCaptureFormat()
{
	if (ioctl(fDriverFD, BTV_GET_CAPTURE_FORMAT, &fCaptureFormat,
		sizeof(fCaptureFormat)) < 0) {
		return B_ERROR;
	}

	return B_OK;
}


status_t
BttvProducer::_ApplyCaptureFormat()
{
	return _ApplyCaptureFormat(fCaptureFormat);
}


status_t
BttvProducer::_ApplyCaptureFormat(const bttv_capture_format& format)
{
	bttv_capture_format desiredFormat = format;
	desiredFormat.input = BTV_INPUT_COMPOSITE;
	desiredFormat.pixel_format = BTV_PIXEL_FORMAT_YUY2;

	if (ioctl(fDriverFD, BTV_SET_CAPTURE_FORMAT, &desiredFormat,
		sizeof(desiredFormat)) < 0) {
		return B_ERROR;
	}

	if (ioctl(fDriverFD, BTV_SET_INPUT, &desiredFormat.input,
		sizeof(desiredFormat.input)) < 0) {
		return B_ERROR;
	}

	return _RefreshCaptureFormat();
}


void
BttvProducer::_UpdateOutputFormat()
{
	float fieldRate = fCaptureFormat.video_standard == BTV_STD_NTSC ? 29.97f : 25.0f;

	fOutput.format.type = B_MEDIA_RAW_VIDEO;
	fOutput.format.u.raw_video = media_raw_video_format::wildcard;
	fOutput.format.u.raw_video.field_rate = fieldRate;
	fOutput.format.u.raw_video.interlace = 1;
	fOutput.format.u.raw_video.first_active = 0;
	fOutput.format.u.raw_video.last_active = (int32)fCaptureFormat.height - 1;
	fOutput.format.u.raw_video.orientation = B_VIDEO_TOP_LEFT_RIGHT;
	fOutput.format.u.raw_video.pixel_width_aspect = 1;
	fOutput.format.u.raw_video.pixel_height_aspect = 1;
	fOutput.format.u.raw_video.display.format = B_YCbCr422;
	fOutput.format.u.raw_video.display.line_width = fCaptureFormat.width;
	fOutput.format.u.raw_video.display.line_count = fCaptureFormat.height;
	fOutput.format.u.raw_video.display.bytes_per_row = fCaptureFormat.bytes_per_line;
	fOutput.format.u.raw_video.display.pixel_offset = 0;
	fOutput.format.u.raw_video.display.line_offset = 0;
	fOutput.format.u.raw_video.display.flags = 0;
}


BParameterWeb*
BttvProducer::_CreateParameterWeb()
{
	BParameterWeb* web = new BParameterWeb();
	BParameterGroup* main = web->MakeGroup(Name());
	BParameterGroup* controls = main->MakeGroup("Capture");

	BDiscreteParameter* input = controls->MakeDiscreteParameter(P_INPUT,
		B_MEDIA_NO_TYPE, "Input", B_INPUT_MUX);
	input->AddItem(BTV_INPUT_COMPOSITE, "Composite");
	input->AddItem(BTV_INPUT_TUNER, "Tuner");

	BDiscreteParameter* standard = controls->MakeDiscreteParameter(P_STANDARD,
		B_MEDIA_NO_TYPE, "Standard", B_VIDEO_FORMAT);
	standard->AddItem(BTV_STD_PAL, "PAL");
	standard->AddItem(BTV_STD_NTSC, "NTSC");

	BDiscreteParameter* resolution = controls->MakeDiscreteParameter(P_RESOLUTION,
		B_MEDIA_NO_TYPE, "Resolution", B_RESOLUTION);
	resolution->AddItem(RESOLUTION_FULL, "Full");
	resolution->AddItem(RESOLUTION_HALF, "Half");

	BParameterGroup* image = main->MakeGroup("Image");
	image->MakeContinuousParameter(P_BRIGHTNESS, B_MEDIA_RAW_VIDEO,
		"Brightness", B_LEVEL, "", -128, 127, 1);
	image->MakeContinuousParameter(P_CONTRAST, B_MEDIA_RAW_VIDEO,
		"Contrast", B_LEVEL, "", 0, 255, 1);
	image->MakeContinuousParameter(P_SATURATION, B_MEDIA_RAW_VIDEO,
		"Saturation", B_LEVEL, "", 0, 255, 1);
	image->MakeContinuousParameter(P_HUE, B_MEDIA_RAW_VIDEO,
		"Hue", B_LEVEL, "", -128, 127, 1);

	return web;
}


uint32
BttvProducer::_ResolutionPresetFor(const bttv_capture_format& format) const
{
	uint32 halfHeight = format.video_standard == BTV_STD_PAL ? 288 : 240;
	uint32 halfWidth = format.video_standard == BTV_STD_PAL ? 360 : 320;

	if (format.width == halfWidth && format.height == halfHeight)
		return RESOLUTION_HALF;

	return RESOLUTION_FULL;
}


bttv_capture_format
BttvProducer::_CaptureFormatFor(uint32 input, uint32 standard,
	uint32 resolution) const
{
	bttv_capture_format format = fCaptureFormat;
	format.input = input;
	format.video_standard = standard;
	format.pixel_format = BTV_PIXEL_FORMAT_YUY2;

	if (resolution == RESOLUTION_HALF) {
		format.width = standard == BTV_STD_PAL ? 360 : 320;
		format.height = standard == BTV_STD_PAL ? 288 : 240;
	} else {
		format.width = 720;
		format.height = standard == BTV_STD_PAL ? 576 : 480;
	}

	format.bytes_per_line = format.width * 2;
	format.frame_size = format.bytes_per_line * format.height;
	return format;
}


status_t
BttvProducer::_ApplyCaptureSettings(uint32 input, uint32 standard,
	uint32 resolution)
{
	if (input != BTV_INPUT_COMPOSITE && input != BTV_INPUT_TUNER)
		return B_BAD_VALUE;
	if (standard != BTV_STD_PAL && standard != BTV_STD_NTSC)
		return B_BAD_VALUE;
	if (resolution != RESOLUTION_FULL && resolution != RESOLUTION_HALF)
		return B_BAD_VALUE;

	bttv_capture_format newCaptureFormat = _CaptureFormatFor(input, standard,
		resolution);
	media_format newFormat = fOutput.format;
	status_t status = _SpecializeFormat(&newFormat, &newCaptureFormat);
	if (status != B_OK)
		return status;

	bool restartCapture = fRunning;
	if (fConnected) {
		status = ChangeFormat(fOutput.source, fOutput.destination, &newFormat);
		if (status != B_OK)
			return status;
	}

	if (restartCapture)
		_HandleStop();

	status = _ApplyCaptureFormat(newCaptureFormat);
	if (status != B_OK) {
		if (restartCapture)
			_HandleStart();
		return status;
	}

	_UpdateOutputFormat();
	fConnectedFormat = fOutput.format.u.raw_video;
	fSelectedInput = fCaptureFormat.input;
	fSelectedStandard = fCaptureFormat.video_standard;
	fResolutionPreset = _ResolutionPresetFor(fCaptureFormat);

	if (fOwnsBufferGroup) {
		_DestroyBufferGroup();
		status = _EnsureBufferGroup();
		if (status != B_OK)
			return status;
	}

	if (restartCapture)
		_HandleStart();

	return B_OK;
}


status_t
BttvProducer::_RefreshControls()
{
	if (ioctl(fDriverFD, BTV_GET_CONTROLS, &fControls, sizeof(fControls)) < 0)
		return B_ERROR;

	return B_OK;
}


status_t
BttvProducer::_ApplyControls(const bttv_video_controls& controls)
{
	if (ioctl(fDriverFD, BTV_SET_CONTROLS, (void*)&controls, sizeof(controls)) < 0)
		return B_ERROR;

	fControls = controls;
	return _RefreshControls();
}


status_t
BttvProducer::_SpecializeFormat(media_format* format,
	bttv_capture_format* captureFormat)
{
	if (format == NULL || captureFormat == NULL)
		return B_BAD_VALUE;

	if (format->type == B_MEDIA_UNKNOWN_TYPE) {
		format->type = B_MEDIA_RAW_VIDEO;
		format->u.raw_video = media_raw_video_format::wildcard;
	}

	if (format->type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	if (format->u.raw_video.interlace != media_raw_video_format::wildcard.interlace
		&& format->u.raw_video.interlace != 1)
		return B_MEDIA_BAD_FORMAT;

	color_space requestedColor = format->u.raw_video.display.format;
	if (requestedColor != media_raw_video_format::wildcard.display.format
		&& requestedColor != B_YCbCr422)
		return B_MEDIA_BAD_FORMAT;

	uint32 requestedWidth = format->u.raw_video.display.line_width;
	if (requestedWidth == 0
		|| requestedWidth == media_raw_video_format::wildcard.display.line_width) {
		requestedWidth = fCaptureFormat.width;
	}

	uint32 requestedHeight = format->u.raw_video.display.line_count;
	if (requestedHeight == 0
		|| requestedHeight == media_raw_video_format::wildcard.display.line_count) {
		requestedHeight = fCaptureFormat.height;
	}

	uint32 requestedStandard = fCaptureFormat.video_standard;
	float requestedFieldRate = format->u.raw_video.field_rate;
	if (requestedFieldRate != 0.0f
		&& requestedFieldRate != media_raw_video_format::wildcard.field_rate) {
		requestedStandard = requestedFieldRate > 27.0f ? BTV_STD_NTSC : BTV_STD_PAL;
	}

	if (requestedHeight > kMaxFrameHeightNTSC)
		requestedStandard = BTV_STD_PAL;
	else if (requestedHeight <= kMaxFrameHeightNTSC && requestedFieldRate > 27.0f)
		requestedStandard = BTV_STD_NTSC;

	uint32 maxHeight = requestedStandard == BTV_STD_PAL
		? kMaxFrameHeightPAL : kMaxFrameHeightNTSC;

	if (requestedWidth == 0 || requestedWidth > kMaxFrameWidth
		|| (requestedWidth & 1) != 0)
		return B_MEDIA_BAD_FORMAT;
	if (requestedHeight == 0 || requestedHeight > maxHeight)
		return B_MEDIA_BAD_FORMAT;

	captureFormat->width = requestedWidth;
	captureFormat->height = requestedHeight;
	captureFormat->bytes_per_line = requestedWidth * 2;
	captureFormat->frame_size = captureFormat->bytes_per_line * requestedHeight;
	captureFormat->pixel_format = BTV_PIXEL_FORMAT_YUY2;
	captureFormat->video_standard = requestedStandard;
	captureFormat->input = BTV_INPUT_COMPOSITE;

	format->type = B_MEDIA_RAW_VIDEO;
	format->u.raw_video = media_raw_video_format::wildcard;
	format->u.raw_video.field_rate = requestedStandard == BTV_STD_NTSC ? 29.97f : 25.0f;
	format->u.raw_video.interlace = 1;
	format->u.raw_video.first_active = 0;
	format->u.raw_video.last_active = requestedHeight - 1;
	format->u.raw_video.orientation = B_VIDEO_TOP_LEFT_RIGHT;
	format->u.raw_video.pixel_width_aspect = 1;
	format->u.raw_video.pixel_height_aspect = 1;
	format->u.raw_video.display.format = B_YCbCr422;
	format->u.raw_video.display.line_width = requestedWidth;
	format->u.raw_video.display.line_count = requestedHeight;
	format->u.raw_video.display.bytes_per_row = requestedWidth * 2;
	format->u.raw_video.display.pixel_offset = 0;
	format->u.raw_video.display.line_offset = 0;
	format->u.raw_video.display.flags = 0;
	return B_OK;
}


void
BttvProducer::_HandleStart()
{
	if (fRunning)
		return;

	if (ioctl(fDriverFD, BTV_START_CAPTURE, NULL, 0) < 0)
		return;

	fFrameSequence = 0;
	fRunning = true;
	fCaptureThread = spawn_thread(&_CaptureThreadEntry, "bttv capture",
		B_REAL_TIME_PRIORITY, this);
	if (fCaptureThread < B_OK) {
		fRunning = false;
		ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL, 0);
		return;
	}

	resume_thread(fCaptureThread);
}


void
BttvProducer::_HandleStop()
{
	if (!fRunning)
		return;

	fRunning = false;
	ioctl(fDriverFD, BTV_STOP_CAPTURE, NULL, 0);

	if (fCaptureThread >= B_OK) {
		status_t result = B_OK;
		wait_for_thread(fCaptureThread, &result);
		fCaptureThread = -1;
	}
}


status_t
BttvProducer::_EnsureBufferGroup()
{
	if (fBufferGroup != NULL)
		return B_OK;

	fBufferGroup = new BBufferGroup(fCaptureFormat.frame_size, 4);
	if (fBufferGroup == NULL)
		return B_NO_MEMORY;
	if (fBufferGroup->InitCheck() != B_OK) {
		delete fBufferGroup;
		fBufferGroup = NULL;
		return B_ERROR;
	}
	fOwnsBufferGroup = true;

	return B_OK;
}


void
BttvProducer::_DestroyBufferGroup()
{
	BAutolock locker(&fLock);
	if (fBufferGroup != NULL && fOwnsBufferGroup)
		delete fBufferGroup;
	fBufferGroup = NULL;
	fOwnsBufferGroup = false;
}


int32
BttvProducer::_CaptureThreadEntry(void* cookie)
{
	return ((BttvProducer*)cookie)->_CaptureThread();
}


int32
BttvProducer::_CaptureThread()
{
	while (fRunning) {
		if (!fConnected || !fEnabled) {
			snooze(20000);
			continue;
		}

		BBufferGroup* bufferGroup = NULL;
		media_source source;
		media_destination destination;
		bttv_capture_format captureFormat;
		{
			BAutolock locker(&fLock);
			bufferGroup = fBufferGroup;
			if (bufferGroup == NULL)
				continue;
			source = fOutput.source;
			destination = fOutput.destination;
			captureFormat = fCaptureFormat;
		}

		BBuffer* buffer = bufferGroup->RequestBuffer(captureFormat.frame_size, 0);
		if (buffer == NULL) {
			snooze(2000);
			continue;
		}

		bigtime_t started = system_time();
		ssize_t bytesRead = read(fDriverFD, buffer->Data(), captureFormat.frame_size);
		if (bytesRead < 0) {
			buffer->Recycle();
			snooze(2000);
			continue;
		}
		if ((size_t)bytesRead != captureFormat.frame_size) {
			buffer->Recycle();
			continue;
		}

		fProcessingLatency = system_time() - started;

		media_header* header = buffer->Header();
		header->type = B_MEDIA_RAW_VIDEO;
		header->time_source = TimeSource()->ID();
		header->size_used = bytesRead;
		header->file_pos = 0;
		header->orig_size = 0;
		header->data_offset = 0;
		header->start_time = TimeSource()->PerformanceTimeFor(system_time());
		header->u.raw_video.field_gamma = 1.0f;
		header->u.raw_video.field_sequence = fFrameSequence++;
		header->u.raw_video.field_number = 0;
		header->u.raw_video.pulldown_number = 0;
		header->u.raw_video.first_active_line = 0;
		header->u.raw_video.line_count = captureFormat.height;

		if (SendBuffer(buffer, source, destination) != B_OK)
			buffer->Recycle();
	}

	return B_OK;
}
