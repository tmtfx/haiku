#include "ResultsListView.h"

#include <ListItem.h>
#include <String.h>
#include <stdio.h>

ResultsListView::ResultsListView(const char* name, list_view_type type)
	: BListView(name, type)
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
ResultsListView::_FormatDisplayString(
	const char* name,
	const char* status,
	const char* source)
{
	BString display;
	display.SetToFormat("%s %s - %s %s",
		_GetStatusIcon(status),
		name ? name : "unknown",
		status ? status : "unknown",
		_GetSourceIcon(source));
	
	return display;
}

void
ResultsListView::AddPackageResult(
	const char* name,
	const char* status,
	const char* checksum,
	const char* source)
{
	if (!name || !status)
		return;
	
	// Create display string with status icon
	BString display = _FormatDisplayString(name, status, source);
	
	// Create list item
	BStringItem* item = new BStringItem(display.String());
	
	// Color code by status
	if (strcmp(status, "ok") == 0) {
		item->SetTextColor(make_color(0, 150, 0, 255));  // Green
	} else if (strcmp(status, "corrupted") == 0) {
		item->SetTextColor(make_color(255, 0, 0, 255));  // Red
	} else if (strcmp(status, "unverified") == 0) {
		item->SetTextColor(make_color(255, 165, 0, 255));  // Orange
	}
	
	AddItem(item);
}

void
ResultsListView::MakeEmpty()
{
	int32 count = CountItems();
	for (int32 i = count - 1; i >= 0; i--) {
		BListItem* item = RemoveItem(i);
		delete item;
	}
}

bool
ResultsListView::GetSelectedPackage(BString& outName, BString& outStatus)
{
	int32 selected = CurrentSelection();
	if (selected < 0)
		return false;
	
	BStringItem* item = (BStringItem*)ItemAt(selected);
	if (!item)
		return false;
	
	// Parse the display string to extract name and status
	BString text(item->Text());
	
	// Format is: "✓ name - status [source]"
	// Extract name (between "✓ " and " - ")
	int32 dashPos = text.FindFirst(" - ");
	if (dashPos < 0)
		return false;
	
	outName = text;
	outName.Remove(0, 2);  // Remove status icon
	outName.Truncate(dashPos - 2);
	
	// Extract status (between " - " and space before "[")
	outStatus = text;
	outStatus.Remove(0, dashPos + 3);
	int32 bracketPos = outStatus.FindFirst(" [");
	if (bracketPos > 0)
		outStatus.Truncate(bracketPos);
	
	return true;
}
