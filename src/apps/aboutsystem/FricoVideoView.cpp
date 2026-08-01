#include "FricoVideoView.h"

#include <Window.h>
#include <Entry.h>
#include <MediaDefs.h>
#include <string.h>

#include <algorithm>

FricoVideoView::FricoVideoView(const char* name)
    :
    BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
    fVideoMediaFile(NULL),
    fVideoTrack(NULL),
    fAudioMediaFile(NULL),
    fAudioTrack(NULL),
    fCurrentFrame(NULL),
    fSoundPlayer(NULL),
    fRunner(NULL),
    fFrameDelay(40000),
    fUseOverlay(false)
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
FricoVideoView::PlayVideo(const char* path)
{
    StopVideo();
    
    entry_ref ref;
    if (get_ref_for_path(path, &ref) != B_OK)
        return;

    // --- 1. APRIAMO IL FILE PER IL VIDEO ---
    fVideoMediaFile = new BMediaFile(&ref);
    if (fVideoMediaFile->InitCheck() == B_OK) {
        int32 numTracks = fVideoMediaFile->CountTracks();
        for (int32 i = 0; i < numTracks; i++) {
            BMediaTrack* track = fVideoMediaFile->TrackAt(i);
            if (track == NULL)
                continue;

            media_format format;
            if (track->EncodedFormat(&format) == B_OK
                && (format.type == B_MEDIA_RAW_VIDEO || format.type == B_MEDIA_ENCODED_VIDEO)) {
                fVideoTrack = track;
                break;
            }
            fVideoMediaFile->ReleaseTrack(track);
        }
    }

    if (fVideoTrack == NULL) {
        StopVideo();
        return;
    }
    
    // --- 2. APRIAMO UN'ISTANZA SEPARATA DEL FILE PER L'AUDIO ---
    fAudioMediaFile = new BMediaFile(&ref);
    if (fAudioMediaFile->InitCheck() == B_OK) {
        int32 numTracks = fAudioMediaFile->CountTracks();
        for (int32 i = 0; i < numTracks; i++) {
            BMediaTrack* track = fAudioMediaFile->TrackAt(i);
            if (track == NULL)
                continue;

            media_format format;
            if (track->EncodedFormat(&format) == B_OK
                && (format.type == B_MEDIA_RAW_AUDIO || format.type == B_MEDIA_ENCODED_AUDIO)) {
                fAudioTrack = track;
                break;
            }
            fAudioMediaFile->ReleaseTrack(track);
        }
    }

/*    int32 numTracks = fVideoMediaFile->CountTracks();
    for (int32 i = 0; i < numTracks; i++) {
        BMediaTrack* track = fVideoMediaFile->TrackAt(i);
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
                fVideoMediaFile->ReleaseTrack(track);
            }
        } else {
            fVideoMediaFile->ReleaseTrack(track);
        }
    }

    if (fVideoTrack == NULL) {
        StopVideo();
        return;
    }*/

    // 1. Chiediamo al MediaKit il formato RAW YUV422 (YUY2) nativo per l'Overlay HW
    media_format decodedFormat = {};
    decodedFormat.type = B_MEDIA_RAW_VIDEO;
    decodedFormat.u.raw_video = media_raw_video_format::wildcard;
    decodedFormat.u.raw_video.display.format = B_YCbCr422;//B_YUV422;

    // Se YUV422 non è accettato dal decoder, proviamo il fallback su RGB32
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

    // 2. Tentiamos prima la creazione di una Bitmap Overlay Hardware
    if (space == B_YUV422 || space == B_YCbCr422) {
        fCurrentFrame = new BBitmap(frameRect, B_BITMAP_WILL_OVERLAY, space);
        if (fCurrentFrame->InitCheck() == B_OK) {
            fUseOverlay = true;
        } else {
            // Se il driver/scheda non supporta l'overlay per questa risoluzione, pulisci
            delete fCurrentFrame;
            fCurrentFrame = NULL;
            fUseOverlay = false;
        }
    }

    // Fallback software se l'overlay non è disponibile
    if (!fUseOverlay) {
        // Se eravamo in YUV422 ma l'overlay è fallito, re-impostiamo RGB32 per DrawBitmap
        if (space != B_RGB32) {
            decodedFormat.u.raw_video.display.format = B_RGB32;
            fVideoTrack->DecodedFormat(&decodedFormat);
        }
        fCurrentFrame = new BBitmap(frameRect, B_RGB32);
    }

    // 3. Se l'overlay è attivo, configuriamo il colore di Chroma Key ed impostiamo la superficie
    if (fUseOverlay) {
        _UpdateOverlay();
    }

    // Avvia la riproduzione audio se esiste una traccia audio
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

    // Scrive i dati decodificati direttamente nella VRAM/Overlay Buffer di fCurrentFrame
    status_t err = fVideoTrack->ReadFrames(fCurrentFrame->Bits(), &frameCount, &header);

    if (err != B_OK || frameCount < 1) {
        // Loop continuo del video
        int64 frame = 0;
        fVideoTrack->SeekToFrame(&frame);
        return;
    }

    if (!fUseOverlay) {
        Invalidate();
    }
    // NOTA: Se fUseOverlay == true, non serve chiamare Invalidate()! 
    // Il driver/GPU legge fCurrentFrame->Bits() direttamente ad ogni V-Sync.
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
    
    if (fUseOverlay) {
        ClearViewOverlay();
        fUseOverlay = false;
    }

    if (fVideoMediaFile != NULL) {
        if (fVideoTrack != NULL)
            fVideoMediaFile->ReleaseTrack(fVideoTrack);
        if (fAudioTrack != NULL)
            fVideoMediaFile->ReleaseTrack(fAudioTrack);
        delete fVideoMediaFile;
        fVideoMediaFile = NULL;
        fVideoTrack = NULL;
        fAudioTrack = NULL;
    }

    delete fCurrentFrame;
    fCurrentFrame = NULL;
}

void
FricoVideoView::_UpdateOverlay()
{
    if (!fUseOverlay || fCurrentFrame == NULL)
        return;

    BRect bounds = Bounds();
    BRect bitmapBounds = fCurrentFrame->Bounds();

    // Calcolo aspect ratio
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

    // Aggiorna la superficie di overlay hardware del driver grafico
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
        // Se usiamo l'overlay, app_server riempie l'area con la color key (es. Magenta) 
        // ed il chip video esegue la sovrapposizione hardware YUV direttamente sul DAC!
        SetHighColor(fOverlayKeyColor);
        FillRect(updateRect);
    } else {
        // Fallback Software con DrawBitmap (RGB32)
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
    const media_raw_audio_format& /*format*/)
{
	memset(buffer, 0, size);
    FricoVideoView* self = static_cast<FricoVideoView*>(cookie);
    if (self == NULL || self->fAudioTrack == NULL)
        return;

    int64 frameCount = 0;
    media_header header;
    status_t err = self->fAudioTrack->ReadFrames(buffer, &frameCount, &header);

    if (err != B_OK || frameCount < 1) {
        // Fine traccia: riavvolgi per il loop
        int64 frame = 0;
        self->fAudioTrack->SeekToFrame(&frame);
    }
}
