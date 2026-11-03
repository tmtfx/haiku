/*
 * Copyright 2026, Pirati del Frico. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Copilot CLI
 *		tmtfx
 *
 * fsck.hpkgs - HPKG Package Integrity Verification Utility
 *
 * Scans /boot/system/packages for all installed HPKG files,
 * verifies checksums against BFS extended attributes, and
 * reports corruption status.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Node.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include "PackageVerifier.h"
#include "ConsoleReporter.h"

using namespace BPackageKit::BPrivate;


// ==============================================================================
// Constants
// ==============================================================================

const char* PACKAGES_PATH = "/boot/system/packages";
const char* ATTR_CHECKSUM = "integrity.hpkg.sha256";
const char* ATTR_VERIFIED = "integrity.hpkg.verified";
const char* ATTR_SOURCE = "integrity.hpkg.source";


// ==============================================================================
// Helper Functions
// ==============================================================================

static bool
_IsHPKGFile(const char* filename)
{
	if (filename == NULL)
		return false;
	
	size_t len = strlen(filename);
	if (len < 5)
		return false;
	
	return strcmp(filename + len - 5, ".hpkg") == 0;
}


static status_t
_ScanPackagesDirectory(PackageVerifier& verifier, ConsoleReporter& reporter)
{
	DIR* dir = opendir(PACKAGES_PATH);
	if (dir == NULL) {
		fprintf(stderr, "Error: Cannot open %s: %m\n", PACKAGES_PATH);
		return errno;
	}

	struct dirent* entry;
	int filesProcessed = 0;
	
	while ((entry = readdir(dir)) != NULL) {
		// Skip . and ..
		if (entry->d_name[0] == '.')
			continue;
		
		// Process only .hpkg files
		if (!_IsHPKGFile(entry->d_name))
			continue;
		
		BPath fullPath(PACKAGES_PATH);
		fullPath.Append(entry->d_name);
		
		PackageVerificationResult result;
		status_t status = verifier.VerifyPackage(fullPath.Path(), &result);
		
		reporter.ReportPackage(result);
		filesProcessed++;
	}

	closedir(dir);
	
	if (filesProcessed == 0) {
		fprintf(stderr, "Warning: No HPKG files found in %s\n", PACKAGES_PATH);
	}
	
	return B_OK;
}


// ==============================================================================
// Main
// ==============================================================================

int
main(int argc, char* argv[])
{
	// TODO: Parse command-line arguments
	// -v, --verbose      : Show detailed information
	// -r, --repair       : Attempt auto-repair of corrupt packages
	// -q, --quiet        : Minimal output
	// --path <dir>       : Custom packages directory
	
	fprintf(stdout, "fsck.hpkgs v1.0 - HPKG Integrity Verification Tool\n");
	fprintf(stdout, "Scanning %s...\n\n", PACKAGES_PATH);
	
	PackageVerifier verifier;
	ConsoleReporter reporter;
	
	status_t status = _ScanPackagesDirectory(verifier, reporter);
	if (status != B_OK) {
		fprintf(stderr, "Error during scanning: %s\n", strerror(status));
		return 1;
	}
	
	// Print summary
	uint32 totalFiles = reporter.GetTotalFiles();
	uint32 corruptFiles = reporter.GetCorruptFiles();
	uint32 missingAttributeFiles = reporter.GetMissingAttributeFiles();
	
	fprintf(stdout, "\n=== Verification Summary ===\n");
	fprintf(stdout, "Total files scanned:        %u\n", totalFiles);
	fprintf(stdout, "Files with valid checksum: %u\n", totalFiles - corruptFiles - missingAttributeFiles);
	fprintf(stdout, "Files without attribute:   %u\n", missingAttributeFiles);
	fprintf(stdout, "Corrupted files:           %u\n", corruptFiles);
	
	if (corruptFiles > 0 || missingAttributeFiles > 0) {
		fprintf(stdout, "\nWarning: Found %u file(s) with integrity issues!\n",
			corruptFiles + missingAttributeFiles);
		return 1;
	}
	
	fprintf(stdout, "\nAll packages verified successfully.\n");
	return 0;
}
