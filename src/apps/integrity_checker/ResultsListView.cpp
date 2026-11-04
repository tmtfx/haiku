#include "ResultsListView.h"

#include <ListItem.h>
#include <String.h>
#include <Window.h>
#include <stdio.h>

ResultsListView::ResultsListView(const char* name, list_view_type type)
	: BListView(name, type),
	  fPackageInfos(10)
{
	// Set up list view styling
}

ResultsListView::~ResultsListView()
{
	MakeEmpty();
}

void
ResultsListView::MessageReceived(BMessage* message)
{
	BListView::MessageReceived(message);
}

void
ResultsListView::SelectionChanged()
{
	// Notify parent window of selection change
	int32 selected = CurrentSelection();
	if (selected >= 0) {
		BMessage msg(B_SIMPLE_DATA);
		msg.AddInt32("index", selected);
		Window()->PostMessage(&msg);
	}
}

const char*
ResultsListView::_GetStatusIcon(const char* status)
{
	if (!status)
		return "? ";

	if (strcmp(status, "ok") == 0)
		return "✓ ";
	if (strcmp(status, "corrupted") == 0)
		return "✗ ";
	if (strcmp(status, "unverified") == 0)
		return "⚠ ";

	return "? ";
}

const char*
ResultsListView::_GetSourceIcon(const char* source)
{
	if (!source)
		return "";

	if (strcmp(source, "repository") == 0)
		return "[repo]";
	if (strcmp(source, "local") == 0)
		return "[local]";
	if (strcmp(source, "network") == 0)
		return "[net]";
	if (strcmp(source, "unknown") == 0)
		return "[?]";

	return "";
}

BString
ResultsListView::_FormatDisplayString(const PackageInfo& info)
{
	BString display;
	display.SetToFormat("%s %s - %s %s",
		_GetStatusIcon(info.status.String()),
		info.name.IsEmpty() ? "unknown" : info.name.String(),
		info.status.IsEmpty() ? "unknown" : info.status.String(),
		_GetSourceIcon(info.source.String()));

	return display;
}

void
ResultsListView::AddPackageResult(const PackageInfo& info)
{
	if (info.name.IsEmpty() || info.status.IsEmpty())
		return;

	// Create display string with status icon (status is already conveyed
	// by the icon; BStringItem has no per-item text color API)
	BString display = _FormatDisplayString(info);
	BStringItem* item = new BStringItem(display.String());

	AddItem(item);
	fPackageInfos.AddItem(new PackageInfo(info));
}

void
ResultsListView::MakeEmpty()
{
	int32 count = CountItems();
	for (int32 i = count - 1; i >= 0; i--) {
		BListItem* item = RemoveItem(i);
		delete item;
	}

	fPackageInfos.MakeEmpty();
}

bool
ResultsListView::GetSelectedPackageInfo(PackageInfo& outInfo) const
{
	int32 selected = CurrentSelection();
	if (selected < 0 || selected >= fPackageInfos.CountItems())
		return false;

	const PackageInfo* info = fPackageInfos.ItemAt(selected);
	if (!info)
		return false;

	outInfo = *info;
	return true;
}
