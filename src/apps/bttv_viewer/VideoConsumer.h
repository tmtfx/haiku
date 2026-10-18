#ifndef VIDEO_CONSUMER_H
#define VIDEO_CONSUMER_H

#include <BufferConsumer.h>
#include <MediaEventLooper.h>
#include <MediaNode.h>

class VideoView;


class VideoConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
								VideoConsumer(const char* name,
									VideoView* targetView);
	virtual						~VideoConsumer();

/* BMediaNode */
public:
	virtual	BMediaAddOn*		AddOn(int32* cookie) const;
	virtual	void				NodeRegistered();
	virtual	status_t			RequestCompleted(
									const media_request_info& info);
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);
	virtual	status_t			DeleteHook(BMediaNode* node);

/* BMediaEventLooper */
protected:
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

/* BBufferConsumer */
public:
	virtual	status_t			AcceptFormat(const media_destination& dest,
									media_format* format);
	virtual	status_t			GetNextInput(int32* cookie,
									media_input* outInput);
	virtual	void				DisposeInputCookie(int32 cookie);

protected:
	virtual	void				BufferReceived(BBuffer* buffer);
	virtual	void				ProducerDataStatus(
									const media_destination& forWhom,
									int32 status, bigtime_t atMediaTime);
	virtual	status_t			GetLatencyFor(
									const media_destination& forWhom,
									bigtime_t* outLatency,
									media_node_id* outTimesource);
	virtual	status_t			Connected(const media_source& producer,
									const media_destination& where,
									const media_format& withFormat,
									media_input* outInput);
	virtual	void				Disconnected(const media_source& producer,
									const media_destination& where);
	virtual	status_t			FormatChanged(
									const media_source& producer,
									const media_destination& consumer,
									int32 fromChangeCount,
									const media_format& format);

private:
			VideoView*			fTargetView;
			media_input			fInput;
			bool				fConnected;
			uint32				fWidth;
			uint32				fHeight;
};

#endif // VIDEO_CONSUMER_H
