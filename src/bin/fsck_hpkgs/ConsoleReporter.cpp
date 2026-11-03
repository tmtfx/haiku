/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * Console reporting implementation
 */

#include "ConsoleReporter.h"

#include <stdio.h>

using namespace BPackageKit::BPrivate;


// ==============================================================================
// ConsoleReporter
// ==============================================================================

ConsoleReporter::ConsoleReporter()
	: fTotalFiles(0),
	  fCorruptFiles(0),
	  fMissingAttributeFiles(0)
{
}


ConsoleReporter::~ConsoleReporter()
{
}


void
ConsoleReporter::ReportPackage(const PackageVerificationResult& result)
{
	fTotalFiles++;
	
	if (result.attributeExists && result.isValid) {
		// OK - verified successfully
		fprintf(stdout, "✓ %s\n", result.filename.String());
		return;
	}
	
	if (!result.attributeExists) {
		// Warning - missing attribute
		fprintf(stdout, "⚠ %s (no checksum attribute)\n", result.filename.String());
		fMissingAttributeFiles++;
		return;
	}
	
	if (!result.isValid) {
		// Error - corrupted
		fprintf(stderr, "✗ %s (CORRUPTED - checksum mismatch)\n", result.filename.String());
		fprintf(stderr, "  Expected: %s\n", result.attributedChecksum.String());
		fprintf(stderr, "  Got:      %s\n", result.calculatedChecksum.String());
		fCorruptFiles++;
		return;
	}
}


uint32
ConsoleReporter::GetTotalFiles() const
{
	return fTotalFiles;
}


uint32
ConsoleReporter::GetCorruptFiles() const
{
	return fCorruptFiles;
}


uint32
ConsoleReporter::GetMissingAttributeFiles() const
{
	return fMissingAttributeFiles;
}
