#ifndef VIDEO_VIEW_H
#define VIDEO_VIEW_H

#include <Bitmap.h>
#include <Locker.h>
#include <View.h>


class VideoView : public BView {
public:
								VideoView(const char* name);
	virtual						~VideoView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);

			void				SetFrameData(const uint8* yuy2Data,
									uint32 width, uint32 height);
			void				Clear();

			void				GetStats(uint64* outFrames, float* outFps,
									uint32* outWidth, uint32* outHeight);

private:
			void				_ConvertYUY2ToRGB32(const uint8* src,
									uint8* dst, uint32 width, uint32 height);

private:
			BLocker				fBitmapLock;
			BBitmap*			fBitmap;
			uint32				fWidth;
			uint32				fHeight;

			uint64				fFrameCount;
			bigtime_t			fLastFpsCalcTime;
			uint64				fFramesAtLastFps;
			float				fCurrentFps;
};

#endif // VIDEO_VIEW_H
