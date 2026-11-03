/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Console reporting for verification results
 */

#ifndef CONSOLE_REPORTER_H
#define CONSOLE_REPORTER_H

#include <SupportDefs.h>
#include "PackageVerifier.h"

namespace BPackageKit {
namespace BPrivate {


// ==============================================================================
// ConsoleReporter
// ==============================================================================

class ConsoleReporter {
public:
							ConsoleReporter();
							~ConsoleReporter();
	
	// Report a single package verification result
	void					ReportPackage(const PackageVerificationResult& result);
	
	// Statistics accessors
	uint32					GetTotalFiles() const;
	uint32					GetCorruptFiles() const;
	uint32					GetMissingAttributeFiles() const;

private:
	uint32					fTotalFiles;
	uint32					fCorruptFiles;
	uint32					fMissingAttributeFiles;
};


}  // namespace BPrivate
}  // namespace BPackageKit

#endif	// CONSOLE_REPORTER_H
