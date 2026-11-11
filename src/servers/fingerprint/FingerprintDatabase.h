/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef FINGERPRINT_DATABASE_H
#define FINGERPRINT_DATABASE_H


#include <String.h>
#include <SupportDefs.h>


class BMessage;


// Stores enrolled fingerprint samples (raw captured images, one file per
// finger under the user's private settings directory) and matches a
// freshly captured probe image against them.
//
// NOTE: matching here is a placeholder downsampled normalized-cross-
// correlation over a small search window, NOT real minutiae-based
// matching. It is good enough to validate the end-to-end daemon/IPC/
// keystore-escrow plumbing on real hardware, but should be replaced by
// the planned NBIS (mindtct + bozorth3) pipeline for anything beyond
// personal/experimental use - see Fase 1c in the project plan.
class FingerprintDatabase {
public:
								FingerprintDatabase();
								~FingerprintDatabase();

			status_t			Init();

			// Allocates a new finger id and persists `sampleCount` raw
			// images (each imageWidth * imageHeight bytes, geometry as
			// reported by whichever FingerprintDevice captured them -
			// stored alongside the samples so Match() can tell records
			// from a different device model/geometry apart) under that id
			// with the given label. Returns the new id via outFingerId.
			status_t			SaveEnrollment(const char* label,
									const uint8* const* samples,
									int32 sampleCount, int32 imageWidth,
									int32 imageHeight, int32* outFingerId);

			status_t			Delete(int32 fingerId);

			// Fills reply with parallel "fingerId"/"label"/"createdAt"
			// arrays for every enrolled finger.
			status_t			List(BMessage* reply);

			// Compares probeImage (imageWidth * imageHeight bytes) against
			// every stored sample of every enrolled finger whose stored
			// geometry matches. On a match above threshold returns B_OK
			// with outFingerId/outScore set; otherwise B_NAME_NOT_FOUND.
			status_t			Match(const uint8* probeImage,
									int32 imageWidth, int32 imageHeight,
									int32* outFingerId, int32* outScore);

private:
			BString				fTemplatesDir;

			status_t			_EntryPath(int32 fingerId, BString& path);
			int32				_NextFingerId();
};


#endif // FINGERPRINT_DATABASE_H
