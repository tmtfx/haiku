#ifndef _RESULTS_LIST_VIEW_H
#define _RESULTS_LIST_VIEW_H

#include <ListView.h>
#include <ObjectList.h>
#include <String.h>
#include <Message.h>

class ResultsListView : public BListView {
public:
	struct PackageInfo {
		BString name;
		BString packagePath;
		BString status;			// "ok" / "corrupted" / "unverified"
		BString checksum;			// expected (attributed) checksum
		BString actualChecksum;	// freshly calculated checksum
		BString source;
	};

	ResultsListView(const char* name, list_view_type type = B_SINGLE_SELECTION_LIST);
	virtual ~ResultsListView();

	virtual void MessageReceived(BMessage* message);
	virtual void SelectionChanged();

	// Add a scanned package to the list
	void AddPackageResult(const PackageInfo& info);

	void MakeEmpty();

	// Get the full info for the currently selected row, if any
	bool GetSelectedPackageInfo(PackageInfo& outInfo) const;

private:
	// Status icons (text-based for now)
	const char* _GetStatusIcon(const char* status);
	const char* _GetSourceIcon(const char* source);
	BString _FormatDisplayString(const PackageInfo& info);

	BObjectList<PackageInfo, true> fPackageInfos;
};

#endif // _RESULTS_LIST_VIEW_H
