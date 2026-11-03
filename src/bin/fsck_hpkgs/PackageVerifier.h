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
	BString attributedChecksum;      // From user.hpkg.sha256 attribute
	BString attributedVerified;      // From user.hpkg.verified attribute
	BString attributedSource;        // From user.hpkg.source attribute
	
	// Calculated state
	BString calculatedChecksum;      // SHA256 computed from file contents
	status_t calculationStatus;      // B_OK if successfully calculated
	
	// Verification result
	bool isValid;                     // true if attributed == calculated
	BString statusMessage;            // Human-readable status
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
};


}  // namespace BPrivate
}  // namespace BPackageKit

#endif	// PACKAGE_VERIFIER_H
