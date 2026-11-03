#ifndef _RESULTS_LIST_VIEW_H
#define _RESULTS_LIST_VIEW_H

#include <ListView.h>
#include <String.h>
#include <Message.h>

class ResultsListView : public BListView {
public:
	ResultsListView(const char* name, list_view_type type = B_SINGLE_SELECTION_LIST);
	virtual ~ResultsListView();
	
	virtual void MessageReceived(BMessage* message);
	virtual void SelectionChanged();
	
	// Add package result to list
	void AddPackageResult(
		const char* name,
		const char* status,
		const char* checksum = NULL,
		const char* source = NULL);
	
	void MakeEmpty();
	
	// Get selected package info
	bool GetSelectedPackage(BString& outName, BString& outStatus);
	
private:
	struct PackageInfo {
		BString name;
		BString status;
		BString checksum;
		BString source;
	};
	
	// Status icons (text-based for now)
	const char* _GetStatusIcon(const char* status);
	const char* _GetSourceIcon(const char* source);
	BString _FormatDisplayString(const char* name, const char* status, const char* source);
};

#endif // _RESULTS_LIST_VIEW_H
