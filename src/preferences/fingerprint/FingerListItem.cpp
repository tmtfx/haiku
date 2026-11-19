/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerListItem.h"

#include <time.h>

#include <String.h>


FingerListItem::FingerListItem(int32 fingerId, const char* label,
	int64 createdAt)
	:
	BStringItem(""),
	fFingerId(fingerId)
{
	BString text(label);
	text << " (#" << fingerId << ")";

	time_t t = (time_t)createdAt;
	char dateBuf[64];
	struct tm tmBuf;
	if (localtime_r(&t, &tmBuf) != NULL
		&& strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", &tmBuf) > 0) {
		text << " - " << dateBuf;
	}

	SetText(text.String());
}
