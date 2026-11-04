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


void
ConsoleReporter::ReportQueryResults(const IntegrityQuery& query)
{
	int32 count = query.CountResults();
	
	if (count == 0) {
		fprintf(stdout, "No packages matching query.\n");
		return;
	}
	
	fprintf(stdout, "Query Results (%d packages)\n", count);
	fprintf(stdout, "────────────────────────────────────────────────────────\n");
	
	for (int32 i = 0; i < count; i++) {
		PackageQueryResult result;
		if (query.GetResult(i, result) != B_OK)
			continue;
		
		const char* statusIcon = "?";
		if (result.status == "ok") {
			statusIcon = "✓";
		} else if (result.status == "corrupted") {
			statusIcon = "✗";
		} else if (result.status == "unverified") {
			statusIcon = "⚠";
		}
		
		fprintf(stdout, "%s %-40s [%s]\n", 
			statusIcon,
			result.filename.String(),
			result.status.String());
		
		if (!result.verified.IsEmpty()) {
			fprintf(stdout, "  verified: %s\n", result.verified.String());
		}
		if (!result.checksum.IsEmpty()) {
			fprintf(stdout, "  checksum: %s\n", result.checksum.String());
		}
	}
	
	fprintf(stdout, "────────────────────────────────────────────────────────\n");
}
