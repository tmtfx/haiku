#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <Button.h>
#include <MediaAddOn.h>
#include <MediaDefs.h>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <Window.h>

#include "ControlsWindow.h"
#include "DirectCapture.h"
#include "DriverInterface.h"
#include "VideoConsumer.h"
#include "VideoView.h"

enum {
	kMsgToggleCapture    = 'tgcp',
	kMsgSourceChanged    = 'srcm',
	kMsgInputChanged     = 'inpm',
	kMsgStandardChanged  = 'stdm',
	kMsgResolutionChanged= 'resm',
	kMsgOpenControls     = 'opct',
	kMsgPulseUpdate      = 'puls'
};

enum SourceMode {
	SOURCE_MEDIA_DEFAULT = 0,
	SOURCE_MEDIA_BTTV    = 1,
	SOURCE_DIRECT_DRIVER = 2
};


class MainWindow : public BWindow {
public:
								MainWindow();
	virtual						~MainWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildLayout();
			void				_UpdateResolutions();
			void				_ApplyControls(const bttv_video_controls& controls);

			status_t			_StartCapture();
			void				_StopCapture();

			status_t			_StartMediaKit();
			void				_StopMediaKit();
			status_t			_FindBttvNode(media_node* outNode);

			status_t			_StartDirect();
			void				_StopDirect();

			void				_UpdateStatus();
			void				_SetStatus(const char* text);

private:
			VideoView*			fVideoView;
			BStringView*		fStatusView;
			BButton*			fStartStopButton;
			BButton*			fControlsButton;

			BMenuField*			fSourceField;
			BPopUpMenu*			fSourceMenu;
			BMenuField*			fInputField;
			BPopUpMenu*			fInputMenu;
			BMenuField*			fStandardField;
			BPopUpMenu*			fStandardMenu;
			BMenuField*			fResolutionField;
			BPopUpMenu*			fResolutionMenu;

			SourceMode			fSourceMode;
			uint32				fInput;
			uint32				fStandard;
			uint32				fWidth;
			uint32				fHeight;
			bool				fHalfResolution;
			bool				fCapturing;

			bttv_video_controls	fControls;
			ControlsWindow*		fControlsWindow;

			DirectCapture		fDirectCapture;

			BMediaRoster*		fMediaRoster;
			media_node			fProducerNode;
			media_node			fTimeSourceNode;
			VideoConsumer*		fConsumer;
			media_output		fProducerOutput;
			media_input			fConsumerInput;

			BMessageRunner*		fPulseRunner;
};

#endif // MAIN_WINDOW_H
