#ifndef _SETTINGS_VIEW_H
#define _SETTINGS_VIEW_H

#include <View.h>
#include <CheckBox.h>
#include <TextControl.h>

class SettingsView : public BView {
public:
	SettingsView(const char* name);
	virtual ~SettingsView();
	
	virtual void AttachedToWindow();
	
	// Get settings values
	bool IsAutoRepairEnabled();
	bool IsNotificationsEnabled();
	bool IsLoggingEnabled();
	const char* GetLogPath();
	
	// Set settings values
	void SetAutoRepair(bool enabled);
	void SetNotifications(bool enabled);
	void SetLogging(bool enabled);
	void SetLogPath(const char* path);
	
private:
	BCheckBox* fAutoRepairCheckbox;
	BCheckBox* fNotificationsCheckbox;
	BCheckBox* fLoggingCheckbox;
	BTextControl* fLogPathControl;
	
	void _CreateUI();
};

#endif // _SETTINGS_VIEW_H
