/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Package verification API
 */

#ifndef PACKAGE_VERIFIER_H
#define PACKAGE_VERIFIER_H

#include <String.h>
#include <SupportDefs.h>

namespace BPackageKit {
namespace BPrivate {


// ==============================================================================
// PackageVerificationResult
// ==============================================================================

struct PackageVerificationResult {
	BString filePath;
	BString filename;
	
	// Attribute state
	bool attributeExists;
	BString attributedChecksum;      // From integrity.hpkg.sha256 attribute
	BString attributedVerified;      // From integrity.hpkg.verified attribute
	BString attributedSource;        // From integrity.hpkg.source attribute
	
	// Calculated state
	BString calculatedChecksum;      // SHA256 computed from file contents
	status_t calculationStatus;      // B_OK if successfully calculated
	
	// Verification result
	bool isValid;                     // true if attributed == calculated
	BString statusMessage;            // Human-readable status

	PackageVerificationResult()
		: attributeExists(false), calculationStatus(B_OK), isValid(false) {}
};


// ==============================================================================
// PackageVerifier
// ==============================================================================

class PackageVerifier {
public:
							PackageVerifier();
							~PackageVerifier();
	
	// Main verification method
	status_t				VerifyPackage(const char* filePath,
								PackageVerificationResult* outResult);

private:
	// Helper methods
	status_t				_ReadAttribute(const char* filePath,
								const char* attrName,
								BString& outValue);
	
	status_t				_CalculateChecksum(const char* filePath,
								BString& outChecksum);

	// Write the scan result back as integrity.hpkg.status, and refresh
	// integrity.hpkg.verified. Best-effort: failures (e.g. read-only
	// volume) are not propagated, the verification result itself is
	// already final by the time this is called.
	void					_WriteBackStatus(const char* filePath,
								const char* status);
};


}  // namespace BPrivate
}  // namespace BPackageKit

#endif	// PACKAGE_VERIFIER_H
