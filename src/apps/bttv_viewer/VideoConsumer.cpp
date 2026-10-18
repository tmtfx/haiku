#include "VideoConsumer.h"

#include <Buffer.h>
#include <TimeSource.h>
#include <stdio.h>
#include <string.h>

#include "VideoView.h"


VideoConsumer::VideoConsumer(const char* name, VideoView* targetView)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(B_MEDIA_RAW_VIDEO),
	fTargetView(targetView),
	fConnected(false),
	fWidth(0),
	fHeight(0)
{
	fInput = media_input();
}


VideoConsumer::~VideoConsumer()
{
	Quit();
}


BMediaAddOn*
VideoConsumer::AddOn(int32* cookie) const
{
	if (cookie != NULL)
		*cookie = 0;
	return NULL;
}


void
VideoConsumer::NodeRegistered()
{
	fInput.destination.port = ControlPort();
	fInput.destination.id = 0;
	fInput.source = media_source::null;
	fInput.node = Node();
	fInput.format.type = B_MEDIA_RAW_VIDEO;
	fInput.format.u.raw_video = media_raw_video_format::wildcard;
	fInput.format.u.raw_video.display.format = B_YCbCr422;
	snprintf(fInput.name, sizeof(fInput.name), "Video In");

	SetPriority(B_DISPLAY_PRIORITY);
	Run();
}


status_t
VideoConsumer::RequestCompleted(const media_request_info& info)
{
	return B_OK;
}


status_t
VideoConsumer::HandleMessage(int32 message, const void* data, size_t size)
{
	if (BBufferConsumer::HandleMessage(message, data, size) == B_OK)
		return B_OK;
	return BMediaNode::HandleMessage(message, data, size);
}


status_t
VideoConsumer::DeleteHook(BMediaNode* node)
{
	return BMediaEventLooper::DeleteHook(node);
}


void
VideoConsumer::HandleEvent(const media_timed_event* event, bigtime_t lateness,
	bool realTimeEvent)
{
	(void)lateness;
	(void)realTimeEvent;

	switch (event->type) {
		case BTimedEventQueue::B_HANDLE_BUFFER:
		{
			BBuffer* buffer = (BBuffer*)event->pointer;
			if (buffer != NULL) {
				if (RunState() == B_STARTED && fConnected && fTargetView != NULL) {
					fTargetView->SetFrameData((const uint8*)buffer->Data(),
						fWidth, fHeight);
				}
				buffer->Recycle();
			}
			break;
		}

		case BTimedEventQueue::B_STOP:
		{
			EventQueue()->FlushEvents(event->event_time,
				BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			break;
		}

		default:
			break;
	}
}


status_t
VideoConsumer::AcceptFormat(const media_destination& dest, media_format* format)
{
	if (dest != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (format->type == B_MEDIA_NO_TYPE || format->type == B_MEDIA_UNKNOWN_TYPE)
		format->type = B_MEDIA_RAW_VIDEO;

	if (format->type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	color_space cs = format->u.raw_video.display.format;
	if (cs != B_YCbCr422 && cs != media_raw_video_format::wildcard.display.format)
		return B_MEDIA_BAD_FORMAT;

	format->u.raw_video.display.format = B_YCbCr422;
	return B_OK;
}


status_t
VideoConsumer::GetNextInput(int32* cookie, media_input* outInput)
{
	if (cookie == NULL || outInput == NULL)
		return B_BAD_VALUE;
	if (*cookie != 0)
		return B_BAD_INDEX;

	*outInput = fInput;
	(*cookie)++;
	return B_OK;
}


void
VideoConsumer::DisposeInputCookie(int32 cookie)
{
	(void)cookie;
}


void
VideoConsumer::BufferReceived(BBuffer* buffer)
{
	if (buffer == NULL)
		return;

	if (RunState() == B_STOPPED) {
		buffer->Recycle();
		return;
	}

	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);
	EventQueue()->AddEvent(event);
}


void
VideoConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atMediaTime)
{
	(void)forWhom;
	(void)status;
	(void)atMediaTime;
}


status_t
VideoConsumer::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* outLatency, media_node_id* outTimesource)
{
	if (forWhom != fInput.destination || outLatency == NULL
		|| outTimesource == NULL)
		return B_BAD_VALUE;

	*outLatency = 10000; // 10 ms
	*outTimesource = TimeSource()->ID();
	return B_OK;
}


status_t
VideoConsumer::Connected(const media_source& producer,
	const media_destination& where, const media_format& withFormat,
	media_input* outInput)
{
	if (where != fInput.destination)
		return B_MEDIA_BAD_DESTINATION;

	fInput.source = producer;
	fInput.format = withFormat;
	*outInput = fInput;

	fWidth = withFormat.u.raw_video.display.line_width;
	fHeight = withFormat.u.raw_video.display.line_count;
	fConnected = true;

	return B_OK;
}


void
VideoConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	if (where == fInput.destination && producer == fInput.source) {
		fInput.source = media_source::null;
		fConnected = false;
		fWidth = 0;
		fHeight = 0;
	}
}


status_t
VideoConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 fromChangeCount,
	const media_format& format)
{
	if (consumer != fInput.destination || producer != fInput.source)
		return B_MEDIA_BAD_DESTINATION;

	fInput.format = format;
	fWidth = format.u.raw_video.display.line_width;
	fHeight = format.u.raw_video.display.line_count;
	return B_OK;
}
