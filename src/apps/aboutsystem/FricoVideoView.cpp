#include "FricoVideoView.h"

#include <Window.h>
#include <Entry.h>
#include <MediaDefs.h>
#include <string.h>
#include <DataIO.h>
#include <Application.h>
#include <AppFileInfo.h>
#include <Roster.h>
#include <Resources.h>
#include <File.h>

#include <algorithm>

FricoVideoView::FricoVideoView(const char* name)
    :
    BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
    fMediaFile(NULL),
    fVideoTrack(NULL),
    fAudioTrack(NULL),
    fCurrentFrame(NULL),
    fSoundPlayer(NULL),
    fRunner(NULL),
    fFrameDelay(40000),
    fUseOverlay(false),
    fOverlayKeyColor((rgb_color){ 255, 0, 255, 255 }),
    fDataIO(NULL),
    fAudioBuffer(NULL),
    fAudioBufferPos(0),
    fAudioBufferSize(0)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    fOverlayKeyColor = (rgb_color){ 255, 0, 255, 255 }; // Magenta key di fallback
}


FricoVideoView::~FricoVideoView()
{
    StopVideo();
}


void
FricoVideoView::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetHighColor(0, 0, 0);
}


void
FricoVideoView::DetachedFromWindow()
{
    StopVideo();
    BView::DetachedFromWindow();
}


void
FricoVideoView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_NEXT_FRAME:
            _DecodeNextFrame();
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}


void
FricoVideoView::PlayVideo(int32 resourceID)
{
    app_info info;
    be_app->GetAppInfo(&info);
    BFile file(&info.ref, B_READ_ONLY);
    BResources res(&file);

    size_t size = 0;
    const void* data = res.LoadResource(B_RAW_TYPE, resourceID, &size);
    if (data == NULL || size == 0)
        return;

    PlayVideo(data, size);
}


void
FricoVideoView::PlayVideo(const char* path)
{
    StopVideo();

    BFile* file = new BFile(path, B_READ_ONLY);
    if (file->InitCheck() != B_OK) {
        delete file;
        return;
    }

    fDataIO = file;
    _InitMediaPlayback();
}


void
FricoVideoView::PlayVideo(const void* data, size_t size)
{
    StopVideo();

    if (data == NULL || size == 0)
        return;

    BMallocIO* mallocIO = new BMallocIO();
    ssize_t written = mallocIO->Write(data, size);
    if (written < 0 || (size_t)written != size) {
        delete mallocIO;
        return;
    }

    // Riporta la testina all'inizio per la lettura di BMediaFile
    mallocIO->Seek(0, SEEK_SET);

    fDataIO = mallocIO;
    _InitMediaPlayback();
}


void
FricoVideoView::_InitMediaPlayback()
{
    fMediaFile = new BMediaFile(fDataIO);
    if (fMediaFile->InitCheck() != B_OK) {
        StopVideo();
        return;
    }

    int32 numTracks = fMediaFile->CountTracks();
    for (int32 i = 0; i < numTracks; i++) {
        BMediaTrack* track = fMediaFile->TrackAt(i);
        if (track == NULL)
            continue;

        media_format format;
        if (track->EncodedFormat(&format) == B_OK) {
            if (fVideoTrack == NULL
                && (format.type == B_MEDIA_RAW_VIDEO
                    || format.type == B_MEDIA_ENCODED_VIDEO)) {
                fVideoTrack = track;
            } else if (fAudioTrack == NULL
                && (format.type == B_MEDIA_RAW_AUDIO
                    || format.type == B_MEDIA_ENCODED_AUDIO)) {
                fAudioTrack = track;
            } else {
                fMediaFile->ReleaseTrack(track);
            }
        } else {
            fMediaFile->ReleaseTrack(track);
        }
    }

    if (fVideoTrack == NULL) {
        StopVideo();
        return;
    }

    // 1. Chiediamo il formato RAW YUV422 nativo per l'Overlay HW
    media_format decodedFormat = {};
    decodedFormat.type = B_MEDIA_RAW_VIDEO;
    decodedFormat.u.raw_video = media_raw_video_format::wildcard;
    decodedFormat.u.raw_video.display.format = B_YCbCr422;

    if (fVideoTrack->DecodedFormat(&decodedFormat) != B_OK) {
        decodedFormat.u.raw_video.display.format = B_RGB32;
        if (fVideoTrack->DecodedFormat(&decodedFormat) != B_OK) {
            StopVideo();
            return;
        }
    }

    color_space space = decodedFormat.u.raw_video.display.format;

    float frameRate = decodedFormat.u.raw_video.field_rate;
    if (frameRate > 0)
        fFrameDelay = (bigtime_t)(1000000.0f / frameRate);

    uint32 width = decodedFormat.u.raw_video.display.line_width;
    uint32 height = decodedFormat.u.raw_video.display.line_count;
    BRect frameRect(0, 0, width - 1, height - 1);

    // 2. Overlay HW o fallback software
    if (space == B_YUV422 || space == B_YCbCr422) {
        fCurrentFrame = new BBitmap(frameRect, B_BITMAP_WILL_OVERLAY, space);
        if (fCurrentFrame->InitCheck() == B_OK) {
            fUseOverlay = true;
        } else {
            delete fCurrentFrame;
            fCurrentFrame = NULL;
            fUseOverlay = false;
        }
    }

    if (!fUseOverlay) {
        if (space != B_RGB32) {
            decodedFormat.u.raw_video.display.format = B_RGB32;
            fVideoTrack->DecodedFormat(&decodedFormat);
        }
        fCurrentFrame = new BBitmap(frameRect, B_RGB32);
    }

    if (fUseOverlay) {
        _UpdateOverlay();
    }

    // 3. Riproduzione audio
    if (fAudioTrack != NULL) {
        media_format audioFormat = {};
        audioFormat.type = B_MEDIA_RAW_AUDIO;
        audioFormat.u.raw_audio = media_raw_audio_format::wildcard;
        if (fAudioTrack->DecodedFormat(&audioFormat) == B_OK) {
            fSoundPlayer = new BSoundPlayer(&audioFormat.u.raw_audio,
                "FricoVideoAudio", _AudioCallback, NULL, this);
            if (fSoundPlayer->InitCheck() == B_OK) {
                fSoundPlayer->SetHasData(true);
                fSoundPlayer->Start();
            } else {
                delete fSoundPlayer;
                fSoundPlayer = NULL;
            }
        }
    }

    BMessage msg(MSG_NEXT_FRAME);
    fRunner = new BMessageRunner(BMessenger(this), &msg, fFrameDelay);
}


void
FricoVideoView::_DecodeNextFrame()
{
    if (fVideoTrack == NULL || fCurrentFrame == NULL)
        return;

    int64 frameCount = 0;
    media_header header;

    status_t err = fVideoTrack->ReadFrames(fCurrentFrame->Bits(), &frameCount, &header);

    if (err != B_OK || frameCount < 1) {
        // Fine video: riavvolgiamo SIA il video SIA l'audio simultaneamente
        int64 frame = 0;
        fVideoTrack->SeekToFrame(&frame);

        if (fAudioTrack != NULL) {
            fAudioTrack->SeekToFrame(&frame);
            fAudioBufferPos = 0;
            fAudioBufferSize = 0;
        }
        return;
    }

    if (!fUseOverlay) {
        Invalidate();
    }
}


void
FricoVideoView::StopVideo()
{
    delete fRunner;
    fRunner = NULL;

    if (fSoundPlayer != NULL) {
        fSoundPlayer->Stop();
        delete fSoundPlayer;
        fSoundPlayer = NULL;
    }

    delete[] fAudioBuffer;
    fAudioBuffer = NULL;
    fAudioBufferPos = 0;
    fAudioBufferSize = 0;

    if (fUseOverlay) {
        ClearViewOverlay();
        fUseOverlay = false;
    }

    if (fMediaFile != NULL) {
        if (fVideoTrack != NULL)
            fMediaFile->ReleaseTrack(fVideoTrack);
        if (fAudioTrack != NULL)
            fMediaFile->ReleaseTrack(fAudioTrack);
        delete fMediaFile;
        fMediaFile = NULL;
        fVideoTrack = NULL;
        fAudioTrack = NULL;
    }

    delete fCurrentFrame;
    fCurrentFrame = NULL;

    delete fDataIO;
    fDataIO = NULL;
}


void
FricoVideoView::_UpdateOverlay()
{
    if (!fUseOverlay || fCurrentFrame == NULL)
        return;

    BRect bounds = Bounds();
    BRect bitmapBounds = fCurrentFrame->Bounds();

    float scale = std::min(bounds.Width() / bitmapBounds.Width(),
                           bounds.Height() / bitmapBounds.Height());

    float destWidth = bitmapBounds.Width() * scale;
    float destHeight = bitmapBounds.Height() * scale;

    BRect destRect(
        (bounds.Width() - destWidth) / 2.0f,
        (bounds.Height() - destHeight) / 2.0f,
        (bounds.Width() + destWidth) / 2.0f,
        (bounds.Height() + destHeight) / 2.0f
    );

    SetViewOverlay(fCurrentFrame, bitmapBounds, destRect, 
                   &fOverlayKeyColor, B_FOLLOW_ALL, B_OVERLAY_FILTER_HORIZONTAL | B_OVERLAY_FILTER_VERTICAL);
}


void
FricoVideoView::FrameResized(float newWidth, float newHeight)
{
    BView::FrameResized(newWidth, newHeight);
    if (fUseOverlay) {
        _UpdateOverlay();
    }
}


void
FricoVideoView::Draw(BRect updateRect)
{
    if (fUseOverlay) {
        SetHighColor(fOverlayKeyColor);
        FillRect(updateRect);
    } else {
        FillRect(updateRect);

        if (fCurrentFrame == NULL || !fCurrentFrame->IsValid())
            return;

        BRect bounds = Bounds();
        BRect bitmapBounds = fCurrentFrame->Bounds();

        float scale = std::min(bounds.Width() / bitmapBounds.Width(),
                               bounds.Height() / bitmapBounds.Height());

        float destWidth = bitmapBounds.Width() * scale;
        float destHeight = bitmapBounds.Height() * scale;

        BRect destRect(
            (bounds.Width() - destWidth) / 2.0f,
            (bounds.Height() - destHeight) / 2.0f,
            (bounds.Width() + destWidth) / 2.0f,
            (bounds.Height() + destHeight) / 2.0f
        );

        DrawBitmap(fCurrentFrame, bitmapBounds, destRect);
    }
}


/*static*/ void
FricoVideoView::_AudioCallback(void* cookie, void* buffer, size_t size,
    const media_raw_audio_format& format)
{
    FricoVideoView* self = static_cast<FricoVideoView*>(cookie);
    if (self->fAudioTrack == NULL) {
        memset(buffer, 0, size);
        return;
    }

    uint8* dest = static_cast<uint8*>(buffer);
    size_t bytesNeeded = size;

    size_t sampleSize = format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK;
    size_t frameSize = sampleSize * format.channel_count;
    if (frameSize == 0)
        frameSize = 4;

    while (bytesNeeded > 0) {
        // 1. Se ci sono dati residui nel buffer d'appoggio, copiamoli
        if (self->fAudioBufferSize > 0) {
            size_t toCopy = std::min(bytesNeeded, self->fAudioBufferSize);
            memcpy(dest, self->fAudioBuffer + self->fAudioBufferPos, toCopy);

            dest += toCopy;
            bytesNeeded -= toCopy;
            self->fAudioBufferPos += toCopy;
            self->fAudioBufferSize -= toCopy;

            if (bytesNeeded == 0)
                break;
        }

        // 2. Buffer vuoto: allochiamo se necessario e leggiamo un chunk
        if (self->fAudioBuffer == NULL) {
            self->fAudioBuffer = new uint8[65536];
        }

        int64 framesRead = 0;
        media_header header;

        status_t err = self->fAudioTrack->ReadFrames(self->fAudioBuffer, &framesRead, &header);
        size_t bytesRead = framesRead * frameSize;

        if (err != B_OK || bytesRead == 0) {
            memset(dest, 0, bytesNeeded);
            break;
        }

        self->fAudioBufferPos = 0;
        self->fAudioBufferSize = bytesRead;
    }
}
