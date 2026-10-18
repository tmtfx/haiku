#include "MainWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ParameterWeb.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <stdio.h>
#include <string.h>


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 860, 680), "Bt848 / Bt878 Video Viewer",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fVideoView(NULL),
	fStatusView(NULL),
	fStartStopButton(NULL),
	fControlsButton(NULL),
	fSourceField(NULL),
	fSourceMenu(NULL),
	fInputField(NULL),
	fInputMenu(NULL),
	fStandardField(NULL),
	fStandardMenu(NULL),
	fResolutionField(NULL),
	fResolutionMenu(NULL),
	fSourceMode(SOURCE_MEDIA_DEFAULT),
	fInput(BTV_INPUT_COMPOSITE),
	fStandard(BTV_STD_PAL),
	fWidth(720),
	fHeight(576),
	fHalfResolution(false),
	fCapturing(false),
	fControlsWindow(NULL),
	fMediaRoster(NULL),
	fProducerNode(media_node::null),
	fTimeSourceNode(media_node::null),
	fConsumer(NULL),
	fPulseRunner(NULL)
{
	fControls.brightness = 0;
	fControls.contrast = 216;
	fControls.saturation = 128;
	fControls.hue = 0;

	fProducerOutput = media_output();
	fConsumerInput = media_input();

	_BuildLayout();

	fControlsWindow = new ControlsWindow(BMessenger(this), fControls);

	// 500 ms periodic timer for status bar updates
	BMessage pulseMsg(kMsgPulseUpdate);
	fPulseRunner = new BMessageRunner(BMessenger(this), &pulseMsg, 500000);
}


MainWindow::~MainWindow()
{
	delete fPulseRunner;

	if (fControlsWindow != NULL) {
		fControlsWindow->Lock();
		fControlsWindow->Quit();
		fControlsWindow = NULL;
	}
}


bool
MainWindow::QuitRequested()
{
	_StopCapture();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::_BuildLayout()
{
	// Source menu
	fSourceMenu = new BPopUpMenu("SourceMenu");
	BMessage* srcDefMsg = new BMessage(kMsgSourceChanged);
	srcDefMsg->AddInt32("source", SOURCE_MEDIA_DEFAULT);
	BMenuItem* itemDef = new BMenuItem("Media Kit: Default Input", srcDefMsg);
	itemDef->SetMarked(true);
	fSourceMenu->AddItem(itemDef);

	BMessage* srcBttvMsg = new BMessage(kMsgSourceChanged);
	srcBttvMsg->AddInt32("source", SOURCE_MEDIA_BTTV);
	fSourceMenu->AddItem(new BMenuItem("Media Kit: Bt848/Bt878 Node", srcBttvMsg));

	BMessage* srcDirMsg = new BMessage(kMsgSourceChanged);
	srcDirMsg->AddInt32("source", SOURCE_DIRECT_DRIVER);
	fSourceMenu->AddItem(new BMenuItem("Direct: /dev/video/bttv/0", srcDirMsg));

	fSourceField = new BMenuField("sourceField", "Source:", fSourceMenu);

	// Input menu
	fInputMenu = new BPopUpMenu("InputMenu");
	BMessage* inCompMsg = new BMessage(kMsgInputChanged);
	inCompMsg->AddInt32("input", BTV_INPUT_COMPOSITE);
	BMenuItem* itemComp = new BMenuItem("Composite", inCompMsg);
	itemComp->SetMarked(true);
	fInputMenu->AddItem(itemComp);

	BMessage* inSvidMsg = new BMessage(kMsgInputChanged);
	inSvidMsg->AddInt32("input", BTV_INPUT_SVIDEO);
	fInputMenu->AddItem(new BMenuItem("S-Video", inSvidMsg));

	BMessage* inTunMsg = new BMessage(kMsgInputChanged);
	inTunMsg->AddInt32("input", BTV_INPUT_TUNER);
	fInputMenu->AddItem(new BMenuItem("Tuner", inTunMsg));

	fInputField = new BMenuField("inputField", "Input:", fInputMenu);

	// Standard menu
	fStandardMenu = new BPopUpMenu("StandardMenu");
	BMessage* stdPalMsg = new BMessage(kMsgStandardChanged);
	stdPalMsg->AddInt32("standard", BTV_STD_PAL);
	BMenuItem* itemPal = new BMenuItem("PAL (50Hz)", stdPalMsg);
	itemPal->SetMarked(true);
	fStandardMenu->AddItem(itemPal);

	BMessage* stdNtscMsg = new BMessage(kMsgStandardChanged);
	stdNtscMsg->AddInt32("standard", BTV_STD_NTSC);
	fStandardMenu->AddItem(new BMenuItem("NTSC (60Hz)", stdNtscMsg));

	fStandardField = new BMenuField("standardField", "Standard:", fStandardMenu);

	// Resolution menu
	fResolutionMenu = new BPopUpMenu("ResolutionMenu");
	BMessage* resFullMsg = new BMessage(kMsgResolutionChanged);
	resFullMsg->AddBool("half", false);
	BMenuItem* itemFull = new BMenuItem("Full (720x576)", resFullMsg);
	itemFull->SetMarked(true);
	fResolutionMenu->AddItem(itemFull);

	BMessage* resHalfMsg = new BMessage(kMsgResolutionChanged);
	resHalfMsg->AddBool("half", true);
	fResolutionMenu->AddItem(new BMenuItem("Half (360x288)", resHalfMsg));

	fResolutionField = new BMenuField("resField", "Res:", fResolutionMenu);

	// Buttons
	fStartStopButton = new BButton("startStop", "Start Capture",
		new BMessage(kMsgToggleCapture));
	fControlsButton = new BButton("controls", "Picture...",
		new BMessage(kMsgOpenControls));

	// Center view
	fVideoView = new VideoView("videoView");

	// Status view
	fStatusView = new BStringView("statusView", "Ready. Select source and click Start.");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fSourceField)
			.Add(fInputField)
			.Add(fStandardField)
			.Add(fResolutionField)
			.AddGlue()
			.Add(fControlsButton)
			.Add(fStartStopButton)
		.End()
		.AddStrut(B_USE_DEFAULT_SPACING)
		.Add(fVideoView, 1.0f)
		.AddStrut(B_USE_DEFAULT_SPACING)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddStrut(4.0f)
		.Add(fStatusView)
	.End();
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleCapture:
		{
			if (fCapturing)
				_StopCapture();
			else
				_StartCapture();
			break;
		}

		case kMsgSourceChanged:
		{
			int32 source;
			if (message->FindInt32("source", &source) == B_OK) {
				fSourceMode = (SourceMode)source;
				if (fCapturing) {
					_StopCapture();
					_StartCapture();
				}
			}
			break;
		}

		case kMsgInputChanged:
		{
			int32 input;
			if (message->FindInt32("input", &input) == B_OK) {
				fInput = (uint32)input;
				if (fCapturing) {
					if (fSourceMode == SOURCE_DIRECT_DRIVER)
						fDirectCapture.SetInput(fInput);
				}
			}
			break;
		}

		case kMsgStandardChanged:
		{
			int32 standard;
			if (message->FindInt32("standard", &standard) == B_OK) {
				fStandard = (uint32)standard;
				_UpdateResolutions();
				if (fCapturing) {
					_StopCapture();
					_StartCapture();
				}
			}
			break;
		}

		case kMsgResolutionChanged:
		{
			bool half;
			if (message->FindBool("half", &half) == B_OK) {
				fHalfResolution = half;
				_UpdateResolutions();
				if (fCapturing) {
					_StopCapture();
					_StartCapture();
				}
			}
			break;
		}

		case kMsgOpenControls:
		{
			if (fControlsWindow != NULL) {
				if (fControlsWindow->IsHidden())
					fControlsWindow->Show();
				else
					fControlsWindow->Activate();
			}
			break;
		}

		case kMsgControlsChanged:
		{
			bttv_video_controls controls;
			if (message->FindInt32("brightness", &controls.brightness) == B_OK
				&& message->FindInt32("contrast", &controls.contrast) == B_OK
				&& message->FindInt32("saturation", &controls.saturation) == B_OK
				&& message->FindInt32("hue", &controls.hue) == B_OK) {
				fControls = controls;
				_ApplyControls(controls);
			}
			break;
		}

		case kMsgPulseUpdate:
		{
			_UpdateStatus();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MainWindow::_UpdateResolutions()
{
	if (fStandard == BTV_STD_PAL) {
		fWidth = fHalfResolution ? 360 : 720;
		fHeight = fHalfResolution ? 288 : 576;
		BMenuItem* item0 = fResolutionMenu->ItemAt(0);
		if (item0 != NULL)
			item0->SetLabel("Full (720x576)");
		BMenuItem* item1 = fResolutionMenu->ItemAt(1);
		if (item1 != NULL)
			item1->SetLabel("Half (360x288)");
	} else {
		fWidth = fHalfResolution ? 320 : 720;
		fHeight = fHalfResolution ? 240 : 480;
		BMenuItem* item0 = fResolutionMenu->ItemAt(0);
		if (item0 != NULL)
			item0->SetLabel("Full (720x480)");
		BMenuItem* item1 = fResolutionMenu->ItemAt(1);
		if (item1 != NULL)
			item1->SetLabel("Half (320x240)");
	}
}


void
MainWindow::_ApplyControls(const bttv_video_controls& controls)
{
	if (fSourceMode == SOURCE_DIRECT_DRIVER && fDirectCapture.IsRunning()) {
		fDirectCapture.SetControls(controls);
	} else if (fMediaRoster != NULL && fProducerNode.node > 0) {
		BParameterWeb* web = NULL;
		if (fMediaRoster->GetParameterWebFor(fProducerNode, &web) == B_OK && web != NULL) {
			for (int32 i = 0; i < web->CountParameters(); i++) {
				BParameter* param = web->ParameterAt(i);
				if (strcmp(param->Name(), "Brightness") == 0) {
					float val = controls.brightness;
					param->SetValue(&val, sizeof(val), system_time());
				} else if (strcmp(param->Name(), "Contrast") == 0) {
					float val = controls.contrast;
					param->SetValue(&val, sizeof(val), system_time());
				} else if (strcmp(param->Name(), "Saturation") == 0) {
					float val = controls.saturation;
					param->SetValue(&val, sizeof(val), system_time());
				} else if (strcmp(param->Name(), "Hue") == 0) {
					float val = controls.hue;
					param->SetValue(&val, sizeof(val), system_time());
				}
			}
			delete web;
		}
	}
}


status_t
MainWindow::_StartCapture()
{
	_StopCapture();

	status_t status = B_OK;
	if (fSourceMode == SOURCE_DIRECT_DRIVER) {
		status = _StartDirect();
	} else {
		status = _StartMediaKit();
	}

	if (status == B_OK) {
		fCapturing = true;
		fStartStopButton->SetLabel("Stop Capture");
	} else {
		fCapturing = false;
		fStartStopButton->SetLabel("Start Capture");
	}

	return status;
}


void
MainWindow::_StopCapture()
{
	if (fSourceMode == SOURCE_DIRECT_DRIVER) {
		_StopDirect();
	} else {
		_StopMediaKit();
	}

	fCapturing = false;
	fStartStopButton->SetLabel("Start Capture");
	_SetStatus("Capture stopped.");
}


status_t
MainWindow::_StartDirect()
{
	_SetStatus("Direct: Opening /dev/video/bttv/0...");
	status_t status = fDirectCapture.Start("/dev/video/bttv/0", fInput,
		fStandard, fWidth, fHeight, fVideoView);
	if (status != B_OK) {
		char err[128];
		snprintf(err, sizeof(err), "Direct Error: failed to start (%s)",
			strerror(status));
		_SetStatus(err);
		return status;
	}

	fDirectCapture.SetControls(fControls);
	_SetStatus("Direct: Streaming from /dev/video/bttv/0");
	return B_OK;
}


void
MainWindow::_StopDirect()
{
	fDirectCapture.Stop();
	if (fVideoView != NULL)
		fVideoView->Clear();
}


status_t
MainWindow::_StartMediaKit()
{
	status_t status = B_OK;
	fMediaRoster = BMediaRoster::Roster(&status);
	if (status != B_OK || fMediaRoster == NULL) {
		_SetStatus("Media Kit Error: Media Roster unavailable");
		return status != B_OK ? status : B_ERROR;
	}

	if (fSourceMode == SOURCE_MEDIA_DEFAULT) {
		_SetStatus("Media Kit: Acquiring Default Video Input...");
		status = fMediaRoster->GetVideoInput(&fProducerNode);
		if (status != B_OK) {
			_SetStatus("Media Kit: Default Video Input not set or inactive");
			return status;
		}
	} else {
		_SetStatus("Media Kit: Searching for Bt848/Bt878 Node...");
		status = _FindBttvNode(&fProducerNode);
		if (status != B_OK) {
			_SetStatus("Media Kit: Bt848/Bt878 node not found in media server");
			return status;
		}
	}

	status = fMediaRoster->GetTimeSource(&fTimeSourceNode);
	if (status != B_OK) {
		_SetStatus("Media Kit Error: Cannot acquire time source");
		return status;
	}

	fConsumer = new VideoConsumer("BttvViewerConsumer", fVideoView);
	status = fMediaRoster->RegisterNode(fConsumer);
	if (status != B_OK) {
		fConsumer->Release();
		fConsumer = NULL;
		_SetStatus("Media Kit Error: Failed to register video consumer");
		return status;
	}

	int32 count = 0;
	status = fMediaRoster->GetFreeOutputsFor(fProducerNode, &fProducerOutput, 1,
		&count, B_MEDIA_RAW_VIDEO);
	if (status != B_OK || count < 1) {
		_StopMediaKit();
		_SetStatus("Media Kit Error: No free video output available on producer");
		return status != B_OK ? status : B_RESOURCE_UNAVAILABLE;
	}

	count = 0;
	status = fMediaRoster->GetFreeInputsFor(fConsumer->Node(), &fConsumerInput,
		1, &count, B_MEDIA_RAW_VIDEO);
	if (status != B_OK || count < 1) {
		_StopMediaKit();
		_SetStatus("Media Kit Error: No free video input on consumer");
		return status != B_OK ? status : B_RESOURCE_UNAVAILABLE;
	}

	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;
	format.u.raw_video.display.format = B_YCbCr422;
	format.u.raw_video.display.line_width = fWidth;
	format.u.raw_video.display.line_count = fHeight;
	format.u.raw_video.field_rate = (fStandard == BTV_STD_PAL) ? 25.0f : 29.97f;

	status = fMediaRoster->Connect(fProducerOutput.source,
		fConsumerInput.destination, &format, &fProducerOutput, &fConsumerInput);
	if (status != B_OK) {
		_StopMediaKit();
		char err[128];
		snprintf(err, sizeof(err), "Media Kit Error: Connect failed (%s)",
			strerror(status));
		_SetStatus(err);
		return status;
	}

	fMediaRoster->SetTimeSourceFor(fProducerNode.node, fTimeSourceNode.node);
	fMediaRoster->SetTimeSourceFor(fConsumer->Node().node, fTimeSourceNode.node);

	bigtime_t startTime = system_time() + 30000;
	fMediaRoster->StartNode(fProducerNode, startTime);
	fMediaRoster->StartNode(fConsumer->Node(), startTime);

	_SetStatus("Media Kit: Streaming active");
	return B_OK;
}


void
MainWindow::_StopMediaKit()
{
	if (fMediaRoster == NULL)
		return;

	if (fConsumer != NULL) {
		fMediaRoster->StopNode(fConsumer->Node(), 0, true);
		if (fProducerNode.node > 0)
			fMediaRoster->StopNode(fProducerNode, 0, true);

		if (fProducerOutput.source != media_source::null
			&& fConsumerInput.destination != media_destination::null) {
			fMediaRoster->Disconnect(fProducerOutput.node.node,
				fProducerOutput.source, fConsumerInput.node.node,
				fConsumerInput.destination);
		}

		fMediaRoster->UnregisterNode(fConsumer);
		fConsumer->Release();
		fConsumer = NULL;
	}

	if (fProducerNode.node > 0) {
		fMediaRoster->ReleaseNode(fProducerNode);
		fProducerNode = media_node::null;
	}

	if (fVideoView != NULL)
		fVideoView->Clear();
}


status_t
MainWindow::_FindBttvNode(media_node* outNode)
{
	if (outNode == NULL || fMediaRoster == NULL)
		return B_BAD_VALUE;

	live_node_info liveNodes[16];
	int32 count = 16;
	status_t status = fMediaRoster->GetLiveNodes(liveNodes, &count, NULL, NULL,
		NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	if (status == B_OK) {
		for (int32 i = 0; i < count; i++) {
			if (strstr(liveNodes[i].name, "Bt8") != NULL
				|| strstr(liveNodes[i].name, "bttv") != NULL
				|| strstr(liveNodes[i].name, "Composite") != NULL) {
				*outNode = liveNodes[i].node;
				return B_OK;
			}
		}
	}

	dormant_node_info dormantNodes[16];
	count = 16;
	status = fMediaRoster->GetDormantNodes(dormantNodes, &count, NULL, NULL,
		NULL, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	if (status == B_OK) {
		for (int32 i = 0; i < count; i++) {
			if (strstr(dormantNodes[i].name, "Bt8") != NULL
				|| strstr(dormantNodes[i].name, "bttv") != NULL
				|| strstr(dormantNodes[i].name, "Composite") != NULL) {
				return fMediaRoster->InstantiateDormantNode(dormantNodes[i],
					outNode);
			}
		}
	}

	return B_NAME_NOT_FOUND;
}


void
MainWindow::_UpdateStatus()
{
	if (!fCapturing || fVideoView == NULL)
		return;

	uint64 frames = 0;
	float fps = 0.0f;
	uint32 width = 0;
	uint32 height = 0;
	fVideoView->GetStats(&frames, &fps, &width, &height);

	char buf[256];
	const char* modeStr = (fSourceMode == SOURCE_DIRECT_DRIVER)
		? "Direct /dev/video/bttv/0"
		: ((fSourceMode == SOURCE_MEDIA_DEFAULT) ? "MediaKit (Default)" : "MediaKit (Bt848/878)");

	snprintf(buf, sizeof(buf),
		"%s | %" B_PRIu32 "x%" B_PRIu32 " YUY2 | %.1f FPS | Frames: %" B_PRIu64,
		modeStr, width > 0 ? width : fWidth, height > 0 ? height : fHeight,
		fps, frames);
	_SetStatus(buf);
}


void
MainWindow::_SetStatus(const char* text)
{
	if (fStatusView != NULL && text != NULL)
		fStatusView->SetText(text);
}
