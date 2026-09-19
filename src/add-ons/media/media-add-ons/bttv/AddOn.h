#ifndef _BTTV_MEDIA_ADDON_H
#define _BTTV_MEDIA_ADDON_H

#include <media/MediaAddOn.h>


extern "C" _EXPORT BMediaAddOn* make_media_addon(image_id image);


class BttvMediaAddOn : public BMediaAddOn {
public:
								BttvMediaAddOn(image_id image);
	virtual						~BttvMediaAddOn();

	virtual	status_t			InitCheck(const char** outFailureText);
	virtual	int32				CountFlavors();
	virtual	status_t			GetFlavorAt(int32 index,
									const flavor_info** outInfo);
	virtual	BMediaNode*			InstantiateNodeFor(const flavor_info* info,
									BMessage* config, status_t* outError);

	virtual	status_t			GetConfigurationFor(BMediaNode* node,
									BMessage* message)
									{ return BMediaAddOn::GetConfigurationFor(node, message); }

	virtual	bool				WantsAutoStart() { return false; }

private:
	status_t					fInitStatus;
	flavor_info					fFlavorInfo;
	media_format				fMediaFormat;
};

#endif
