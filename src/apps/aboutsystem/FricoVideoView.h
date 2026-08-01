#ifndef FRICO_VIDEO_VIEW_H
#define FRICO_VIDEO_VIEW_H

#include <View.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <SoundPlayer.h>

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

    static void _AudioCallback(void* cookie, void* buffer, size_t size,
                               const media_raw_audio_format& format);

    BMediaFile*     fMediaFile;
    BMediaTrack*    fVideoTrack;
    BMediaTrack*    fAudioTrack;
    BBitmap*        fCurrentFrame;

    BSoundPlayer*   fSoundPlayer;
    BMessageRunner* fRunner;
    bigtime_t       fFrameDelay;

    bool            fUseOverlay;
    uint8*  fAudioBuffer;
    size_t  fAudioBufferPos;
    size_t  fAudioBufferSize;
    rgb_color       fOverlayKeyColor;
};

#endif // FRICO_VIDEO_VIEW_H
