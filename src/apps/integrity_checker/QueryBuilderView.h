#ifndef _QUERY_BUILDER_VIEW_H
#define _QUERY_BUILDER_VIEW_H

#include <ListControl.h>

class QueryBuilderView : public BListView {
public:
	QueryBuilderView(const char* name, list_view_type type = B_SINGLE_SELECTION_LIST);
	virtual ~QueryBuilderView();
	
	virtual void AttachedToWindow();
	
	// Get current query filter
	const char* GetStatusFilter();
	
private:
	void _PopulateQueries();
};

#endif // _QUERY_BUILDER_VIEW_H
