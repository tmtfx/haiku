/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGER_LIST_ITEM_H
#define FINGER_LIST_ITEM_H


#include <StringItem.h>
#include <SupportDefs.h>


class FingerListItem : public BStringItem {
public:
								FingerListItem(int32 fingerId,
									const char* label, int64 createdAt);

			int32				FingerId() const { return fFingerId; }

private:
			int32				fFingerId;
};


#endif // FINGER_LIST_ITEM_H
