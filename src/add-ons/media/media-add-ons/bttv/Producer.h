#ifndef _BTTV_MEDIA_PRODUCER_H
#define _BTTV_MEDIA_PRODUCER_H

#include <kernel/OS.h>
#include <media/BufferProducer.h>
#include <media/Controllable.h>
#include <media/MediaDefs.h>
#include <media/MediaEventLooper.h>
#include <media/MediaNode.h>
#include <support/Locker.h>

#include "DriverInterface.h"


class BMediaAddOn;
class BBufferGroup;
class BMessenger;
class BParameterWeb;


class BttvProducer : public virtual BBufferProducer,
	public virtual BControllable, public virtual BMediaEventLooper {
public:
								BttvProducer(BMediaAddOn* addOn,
									const char* name, int32 internalID,
									const char* driverPath);
	virtual						~BttvProducer();

	virtual	status_t			InitCheck() const { return fInitStatus; }

public:
	virtual	port_id				ControlPort() const;
	virtual	BMediaAddOn*		AddOn(int32* internalID) const;
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

protected:
	virtual	void				Preroll();
	virtual	void				SetTimeSource(BTimeSource* timeSource);
	virtual	status_t			RequestCompleted(const media_request_info& info);

protected:
	virtual	void				NodeRegistered();
	virtual	void				Start(bigtime_t performanceTime);
	virtual	void				Stop(bigtime_t performanceTime, bool immediate);
	virtual	void				Seek(bigtime_t mediaTime,
									bigtime_t performanceTime);
	virtual	void				TimeWarp(bigtime_t atRealTime,
									bigtime_t toPerformanceTime);
	virtual	status_t			AddTimer(bigtime_t atPerformanceTime,
									int32 cookie);
	virtual	void				SetRunMode(run_mode mode);
	virtual	void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual	void				CleanUpEvent(const media_timed_event* event);
	virtual	bigtime_t			OfflineTime();
	virtual	void				ControlLoop();
	virtual	status_t			DeleteHook(BMediaNode* node);

protected:
	virtual	status_t			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);
	virtual	status_t			FormatProposal(const media_source& output,
									media_format* format);
	virtual	status_t			FormatChangeRequested(const media_source& source,
									const media_destination& destination,
									media_format* ioFormat,
									int32* deprecated);
	virtual	status_t			GetNextOutput(int32* cookie,
									media_output* outOutput);
	virtual	status_t			DisposeOutputCookie(int32 cookie);
	virtual	status_t			SetBufferGroup(const media_source& forSource,
									BBufferGroup* group);
	virtual	status_t			VideoClippingChanged(
									const media_source& forSource,
									int16 numShorts, int16* clipData,
									const media_video_display_info& display,
									int32* deprecated);
	virtual	status_t			GetLatency(bigtime_t* outLatency);
	virtual	status_t			PrepareToConnect(const media_source& source,
									const media_destination& destination,
									media_format* format,
									media_source* outSource, char* outName);
	virtual	void				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* ioName);
	virtual	void				Disconnect(const media_source& source,
									const media_destination& destination);
	virtual	void				LateNoticeReceived(const media_source& source,
									bigtime_t howMuch,
									bigtime_t performanceTime);
	virtual	void				EnableOutput(const media_source& source,
									bool enabled, int32* deprecated);
	virtual	status_t			SetPlayRate(int32 numer, int32 denom);
	virtual	void				AdditionalBufferRequested(
									const media_source& source,
									media_buffer_id prevBuffer,
									bigtime_t prevTime,
									const media_seek_tag* prevTag);
	virtual	void				LatencyChanged(const media_source& source,
									const media_destination& destination,
									bigtime_t newLatency, uint32 flags);

protected:
	virtual	status_t			GetParameterValue(int32 id,
									bigtime_t* lastChange, void* value,
									size_t* size);
	virtual	void				SetParameterValue(int32 id, bigtime_t when,
									const void* value, size_t size);
	virtual	status_t			StartControlPanel(BMessenger* outMessenger);

private:
			enum {
				P_INPUT = 1,
				P_STANDARD,
				P_RESOLUTION,
				P_BRIGHTNESS,
				P_CONTRAST,
				P_SATURATION,
				P_HUE
			};

			enum {
				RESOLUTION_FULL = 0,
				RESOLUTION_HALF = 1
			};

			status_t			_OpenDriver();
			void				_CloseDriver();
			status_t			_RefreshCaptureFormat();
			status_t			_ApplyCaptureFormat();
			status_t			_ApplyCaptureFormat(
									const bttv_capture_format& format);
			void				_UpdateOutputFormat();
			status_t			_SpecializeFormat(media_format* format,
									bttv_capture_format* captureFormat);
			BParameterWeb*		_CreateParameterWeb();
			uint32				_ResolutionPresetFor(
									const bttv_capture_format& format) const;
			bttv_capture_format	_CaptureFormatFor(uint32 input,
									uint32 standard, uint32 resolution) const;
			status_t			_ApplyCaptureSettings(uint32 input,
									uint32 standard, uint32 resolution);
			status_t			_RefreshControls();
			status_t			_ApplyControls(
									const bttv_video_controls& controls);
			void				_HandleStart();
			void				_HandleStop();
			status_t			_EnsureBufferGroup();
			void				_DestroyBufferGroup();
	static	int32				_CaptureThreadEntry(void* cookie);
			int32				_CaptureThread();

private:
			status_t			fInitStatus;
			int32				fInternalID;
			BMediaAddOn*		fAddOn;
			const char*			fDriverPath;
			int					fDriverFD;
			BLocker				fLock;
			BBufferGroup*		fBufferGroup;
			bool				fOwnsBufferGroup;
			thread_id			fCaptureThread;
			volatile bool		fRunning;
			bool				fConnected;
			bool				fEnabled;
			bigtime_t			fProcessingLatency;
			media_output		fOutput;
			media_raw_video_format fConnectedFormat;
			bttv_capture_format	fCaptureFormat;
			bttv_capture_format	fPendingCaptureFormat;
			bool				fHasPendingFormat;
			uint32				fSelectedInput;
			uint32				fSelectedStandard;
			uint32				fResolutionPreset;
			bttv_video_controls	fControls;
			bigtime_t			fInputLastChange;
			bigtime_t			fStandardLastChange;
			bigtime_t			fResolutionLastChange;
			bigtime_t			fBrightnessLastChange;
			bigtime_t			fContrastLastChange;
			bigtime_t			fSaturationLastChange;
			bigtime_t			fHueLastChange;
			uint32				fFrameSequence;
};

#endif
