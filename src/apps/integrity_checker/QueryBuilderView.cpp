#include "QueryBuilderView.h"
#include <ListItem.h>

QueryBuilderView::QueryBuilderView(const char* name, list_view_type type)
	: BListView(name, type)
{
}

QueryBuilderView::~QueryBuilderView()
{
}

void
QueryBuilderView::AttachedToWindow()
{
	BListView::AttachedToWindow();
	_PopulateQueries();
}

void
QueryBuilderView::_PopulateQueries()
{
	MakeEmpty();
	
	// Only list filters GetStatusFilter() can actually back with a real
	// query - a "Show packages from repository"-style item that
	// silently fell back to "show everything" would be worse than not
	// offering it.
	AddItem(new BStringItem("Show all packages"));
	AddItem(new BStringItem("Show OK packages (valid checksum)"));
	AddItem(new BStringItem("Show corrupted packages (checksum mismatch)"));
	AddItem(new BStringItem("Show unverified packages (missing checksum)"));

	Select(0);
}

const char*
QueryBuilderView::GetStatusFilter()
{
	int32 selected = CurrentSelection();
	if (selected < 0)
		return NULL;
	
	BStringItem* item = (BStringItem*)ItemAt(selected);
	if (!item)
		return NULL;
	
	BString text(item->Text());
	
	if (text.FindFirst("corrupted") >= 0)
		return "corrupted";
	if (text.FindFirst("unverified") >= 0)
		return "unverified";
	if (text.FindFirst("OK") >= 0)
		return "ok";
	
	return NULL;
}
