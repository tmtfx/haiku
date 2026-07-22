/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"


static engine_token engineToken = { 1, B_2D_ACCELERATION, NULL };


uint32
AccelerantEngineCount(void)
{
	return 1;
}


status_t
AcquireEngine(uint32 capabilities, uint32 max_wait,
						sync_token* st, engine_token** et)
{
	(void)capabilities;
	(void)max_wait;

	if (gInfo.sharedInfo->engineLock.Acquire() != B_OK)
		return B_ERROR;

	if (st)
		SyncToToken(st);

	*et = &engineToken;
	return B_OK;
}


status_t
ReleaseEngine(engine_token* et, sync_token* st)
{
	if (st)
		GetSyncToken(et, st);

	gInfo.sharedInfo->engineLock.Release();
	return B_OK;
}


void
WaitEngineIdle(void)
{
}


status_t
GetSyncToken(engine_token* et, sync_token* st)
{
	st->engine_id = et->engine_id;
	st->counter = 0;
	return B_OK;
}


status_t
SyncToToken(sync_token* st)
{
	(void)st;

	WaitEngineIdle();
	return B_OK;
}
