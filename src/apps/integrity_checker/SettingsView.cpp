#include "SettingsView.h"

#include <CheckBox.h>
#include <TextControl.h>
#include <GroupLayoutBuilder.h>
#include <GroupLayout.h>
#include <StringView.h>
#include <LayoutItem.h>
#include <SpaceLayoutItem.h>

SettingsView::SettingsView(const char* name)
	: BView(name, B_WILL_DRAW),
	  fAutoRepairCheckbox(NULL),
	  fNotificationsCheckbox(NULL),
	  fLoggingCheckbox(NULL),
	  fLogPathControl(NULL)
{
	_CreateUI();
}

SettingsView::~SettingsView()
{
}

void
SettingsView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}

void
SettingsView::_CreateUI()
{
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 10);
	SetLayout(layout);
	layout->SetInsets(10, 10, 10, 10);
	
	// Auto-repair checkbox
	fAutoRepairCheckbox = new BCheckBox("auto_repair",
		"Automatically repair corrupted packages from repository",
		new BMessage(B_SIMPLE_DATA));
	fAutoRepairCheckbox->SetValue(B_CONTROL_ON);
	layout->AddView(fAutoRepairCheckbox);
	
	// Notifications checkbox
	fNotificationsCheckbox = new BCheckBox("notifications",
		"Show desktop notifications for recovery events",
		new BMessage(B_SIMPLE_DATA));
	fNotificationsCheckbox->SetValue(B_CONTROL_ON);
	layout->AddView(fNotificationsCheckbox);
	
	// Logging checkbox
	fLoggingCheckbox = new BCheckBox("logging",
		"Write detailed logs to file",
		new BMessage(B_SIMPLE_DATA));
	fLoggingCheckbox->SetValue(B_CONTROL_ON);
	layout->AddView(fLoggingCheckbox);
	
	// Log path
	fLogPathControl = new BTextControl("log_path", "Log file path:",
		"/var/log/package_integrity_recovery.log",
		new BMessage(B_SIMPLE_DATA));
	fLogPathControl->SetDivider(100);
	layout->AddView(fLogPathControl);
	
	// Add glue to push everything to top
	layout->AddItem(BSpaceLayoutItem::CreateVerticalStrut(0));
	layout->AddItem(BSpaceLayoutItem::CreateGlue());
}

bool
SettingsView::IsAutoRepairEnabled()
{
	return fAutoRepairCheckbox ? fAutoRepairCheckbox->Value() == B_CONTROL_ON : false;
}

bool
SettingsView::IsNotificationsEnabled()
{
	return fNotificationsCheckbox ? fNotificationsCheckbox->Value() == B_CONTROL_ON : false;
}

bool
SettingsView::IsLoggingEnabled()
{
	return fLoggingCheckbox ? fLoggingCheckbox->Value() == B_CONTROL_ON : false;
}

const char*
SettingsView::GetLogPath()
{
	return fLogPathControl ? fLogPathControl->Text() : NULL;
}

void
SettingsView::SetAutoRepair(bool enabled)
{
	if (fAutoRepairCheckbox)
		fAutoRepairCheckbox->SetValue(enabled ? B_CONTROL_ON : B_CONTROL_OFF);
}

void
SettingsView::SetNotifications(bool enabled)
{
	if (fNotificationsCheckbox)
		fNotificationsCheckbox->SetValue(enabled ? B_CONTROL_ON : B_CONTROL_OFF);
}

void
SettingsView::SetLogging(bool enabled)
{
	if (fLoggingCheckbox)
		fLoggingCheckbox->SetValue(enabled ? B_CONTROL_ON : B_CONTROL_OFF);
}

void
SettingsView::SetLogPath(const char* path)
{
	if (fLogPathControl && path)
		fLogPathControl->SetText(path);
}
