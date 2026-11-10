/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_SERVER_DEFS_H
#define FINGERPRINT_SERVER_DEFS_H


#define FINGERPRINT_SERVER_SIGNATURE "application/x-vnd.Haiku-fingerprint_server"

// All requests are synchronous request/reply BMessages (client should use
// BMessenger::SendMessage(request, &reply) or equivalent). ENROLL_START and
// VERIFY_START are notably BLOCKING on the client side until the whole
// operation completes (which involves live user interaction with the
// sensor, so it can take up to ~30s) - there is no separate progress-event
// channel. An earlier design pushed intermediate progress/done messages to
// the client via a second, separate BMessenger::SendMessage() call per
// update; on real hardware this reliably broke after the first such call
// per client team (every later "fire and forget" push to the same team hung
// until its own timeout, regardless of calling thread or SendMessage
// overload used), so that approach was dropped. A future client wanting a
// live progress bar should poll a status query instead of expecting pushes.
enum {
	B_FP_ENROLL_START		= 'FPe1',	// "label": string
										// -> reply: "error": int32 (B_OK or
										//    error), "fingerId": int32
	B_FP_ENROLL_CANCEL		= 'FPe2',	// (none) - best-effort; takes effect
										// only between captures, not
										// mid-transfer

	B_FP_LIST				= 'FPl1',	// (none)
										// -> reply: "fingerId": int32[],
										//    "label": string[],
										//    "createdAt": int64[]

	B_FP_DELETE				= 'FPd1',	// "fingerId": int32
										// -> reply: "error": int32

	B_FP_VERIFY_START		= 'FPv1',	// (none)
										// -> reply: "error": int32 (B_OK on
										//    match, B_NAME_NOT_FOUND on no
										//    match, B_NO_INIT if no device),
										//    "fingerId": int32, "score": int32
	B_FP_VERIFY_CANCEL		= 'FPv2',	// (none) - same caveat as ENROLL_CANCEL

	B_FP_SET_ESCROW			= 'FPs1',	// "fingerId": int32, "key": raw[32]
										// -> reply: "error": int32
	B_FP_GET_ESCROW			= 'FPs2',	// "fingerId": int32 (must follow a
										// fresh successful VERIFY_START match
										// for the same fingerId)
										// -> reply: "error": int32,
										//    "key": raw[32]
};


#endif // FINGERPRINT_SERVER_DEFS_H
