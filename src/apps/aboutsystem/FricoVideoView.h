#ifndef FRICO_VIDEO_VIEW_H
#define FRICO_VIDEO_VIEW_H

#include <View.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <MediaFile.h>
#include <MediaTrack.h>

enum {
    MSG_NEXT_FRAME = 'fnnf'
};

class FricoVideoView : public BView {
public:
    FricoVideoView(const char* name);
    virtual ~FricoVideoView();

    virtual void AttachedToWindow();
    virtual void DetachedFromWindow();
    virtual void Draw(BRect updateRect);
    virtual void FrameResized(float newWidth, float newHeight);
    virtual void MessageReceived(BMessage* message);

    void PlayVideo(const char* path);
    void StopVideo();

private:
    void _DecodeNextFrame();
    void _UpdateOverlay();

    BMediaFile*     fMediaFile;
    BMediaTrack*    fVideoTrack;
    BBitmap*        fCurrentFrame;

    BMessageRunner* fRunner;
    bigtime_t       fFrameDelay;

    bool            fUseOverlay;
    rgb_color       fOverlayKeyColor;
};

#endif // FRICO_VIDEO_VIEW_H
