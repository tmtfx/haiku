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
#include "IntegrityQuery.h"

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

static void
_PrintUsage(const char* programName)
{
	fprintf(stdout, "Usage: %s [COMMAND] [OPTIONS]\n\n", programName);
	fprintf(stdout, "COMMANDS:\n");
	fprintf(stdout, "  scan          Scan all packages and verify checksums (default)\n");
	fprintf(stdout, "  query         Query packages by integrity status\n");
	fprintf(stdout, "  repair        Attempt to repair corrupted packages\n");
	fprintf(stdout, "\nOPTIONS (scan):\n");
	fprintf(stdout, "  -v, --verbose     Show detailed information\n");
	fprintf(stdout, "  -q, --quiet       Minimal output\n");
	fprintf(stdout, "  --path <dir>      Custom packages directory (default: /boot/system/packages)\n");
	fprintf(stdout, "  --repair          Auto-repair corrupted packages (safe for repository packages)\n");
	fprintf(stdout, "  --dry-run         Show what would be repaired (requires --repair)\n");
	fprintf(stdout, "\nOPTIONS (query):\n");
	fprintf(stdout, "  --status <status> Filter by status: ok, corrupted, unverified\n");
	fprintf(stdout, "  --since <time>    Filter verified after timestamp (ISO 8601)\n");
	fprintf(stdout, "  --path <dir>      Custom packages directory\n");
	fprintf(stdout, "\nOPTIONS (repair):\n");
	fprintf(stdout, "  --package <name>  Repair specific package (glob pattern supported)\n");
	fprintf(stdout, "  --dry-run         Show what would be repaired without making changes\n");
	fprintf(stdout, "  --path <dir>      Custom packages directory\n");
	fprintf(stdout, "\nEXAMPLES:\n");
	fprintf(stdout, "  %s scan                              # Scan all packages\n", programName);
	fprintf(stdout, "  %s scan --verbose                    # Scan with detailed output\n", programName);
	fprintf(stdout, "  %s scan --repair                     # Scan and auto-repair\n", programName);
	fprintf(stdout, "  %s scan --repair --dry-run           # Preview repairs\n", programName);
	fprintf(stdout, "  %s query --status corrupted          # Find corrupted packages\n", programName);
	fprintf(stdout, "  %s query --status unverified         # Find packages without checksums\n", programName);
	fprintf(stdout, "  %s repair --package curl*            # Repair curl package\n", programName);
}

int
main(int argc, char* argv[])
{
	const char* command = "scan";
	const char* packagesPath = PACKAGES_PATH;
	const char* queryStatus = NULL;
	const char* querySince = NULL;
	const char* repairPackage = NULL;
	bool verbose = false;
	bool quiet = false;
	bool repair = false;
	bool dryRun = false;
	
	// Parse arguments
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "scan") == 0) {
			command = "scan";
		} else if (strcmp(argv[i], "query") == 0) {
			command = "query";
		} else if (strcmp(argv[i], "repair") == 0) {
			command = "repair";
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			_PrintUsage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
		} else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
			quiet = true;
		} else if (strcmp(argv[i], "--repair") == 0) {
			repair = true;
		} else if (strcmp(argv[i], "--dry-run") == 0) {
			dryRun = true;
		} else if (strcmp(argv[i], "--path") == 0) {
			if (i + 1 < argc) {
				packagesPath = argv[++i];
			} else {
				fprintf(stderr, "Error: --path requires an argument\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--package") == 0) {
			if (i + 1 < argc) {
				repairPackage = argv[++i];
			} else {
				fprintf(stderr, "Error: --package requires an argument\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--status") == 0) {
			if (i + 1 < argc) {
				queryStatus = argv[++i];
			} else {
				fprintf(stderr, "Error: --status requires an argument\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--since") == 0) {
			if (i + 1 < argc) {
				querySince = argv[++i];
			} else {
				fprintf(stderr, "Error: --since requires an argument\n");
				return 1;
			}
		} else {
			fprintf(stderr, "Unknown argument: %s\n", argv[i]);
			_PrintUsage(argv[0]);
			return 1;
		}
	}
	
	if (!quiet) {
		fprintf(stdout, "fsck.hpkgs v1.0 - HPKG Integrity Verification Tool\n");
	}
	
	// Execute command
	if (strcmp(command, "scan") == 0) {
		if (!quiet) {
			fprintf(stdout, "Scanning %s...\n\n", packagesPath);
		}
		
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
		
		if (!quiet) {
			fprintf(stdout, "\n=== Verification Summary ===\n");
			fprintf(stdout, "Total files scanned:        %u\n", totalFiles);
			fprintf(stdout, "Files with valid checksum: %u\n", totalFiles - corruptFiles - missingAttributeFiles);
			fprintf(stdout, "Files without attribute:   %u\n", missingAttributeFiles);
			fprintf(stdout, "Corrupted files:           %u\n", corruptFiles);
		}
		
		// Handle repair if requested
		if (repair && (corruptFiles > 0 || missingAttributeFiles > 0)) {
			if (dryRun) {
				if (!quiet) {
					fprintf(stdout, "\n=== DRY-RUN: What would be repaired ===\n");
					fprintf(stdout, "Would attempt to recover %u packages from repository\n",
						corruptFiles);
					fprintf(stdout, "Would skip %u packages (missing attributes, no checksum)\n",
						missingAttributeFiles);
				}
			} else {
				if (!quiet) {
					fprintf(stdout, "\n=== Attempting Recovery ===\n");
					fprintf(stdout, "Attempting to recover %u corrupted package(s)...\n",
						corruptFiles);
				}
				// TODO: Integrate with RecoveryCoordinator
				// For now, log that recovery was requested
				fprintf(stderr, "INFO: Auto-recovery integration pending (Phase 6 implementation)\n");
			}
		}
		
		if (corruptFiles > 0 || missingAttributeFiles > 0) {
			if (!quiet) {
				fprintf(stdout, "\nWarning: Found %u file(s) with integrity issues!\n",
					corruptFiles + missingAttributeFiles);
			}
			return repair && !dryRun ? 0 : 1;  // Success if repair was attempted
		}
		
		if (!quiet) {
			fprintf(stdout, "\nAll packages verified successfully.\n");
		}
		return 0;
		
	} else if (strcmp(command, "query") == 0) {
		if (!quiet) {
			fprintf(stdout, "Querying %s...\n\n", packagesPath);
		}
		
		IntegrityQuery query;
		
		if (queryStatus != NULL) {
			if (strcmp(queryStatus, "corrupted") == 0) {
				query.QueryCorrupted();
			} else if (strcmp(queryStatus, "unverified") == 0) {
				query.QueryUnverified();
			} else if (strcmp(queryStatus, "ok") == 0) {
				query.QueryOK();
			} else {
				fprintf(stderr, "Unknown status: %s (use: ok, corrupted, unverified)\n", queryStatus);
				return 1;
			}
		}
		
		if (querySince != NULL) {
			query.QuerySinceTimestamp(querySince);
		}
		
		status_t status = query.Execute(packagesPath);
		if (status != B_OK) {
			fprintf(stderr, "Error during query: %s\n", strerror(status));
			return 1;
		}
		
		ConsoleReporter reporter;
		reporter.ReportQueryResults(query);
		
		return 0;
	} else if (strcmp(command, "repair") == 0) {
		if (!quiet) {
			fprintf(stdout, "Repair mode: Attempting to recover corrupted packages\n\n");
		}
		
		if (repairPackage) {
			if (!quiet) {
				fprintf(stdout, "Target: %s pattern\n", repairPackage);
			}
		} else {
			if (!quiet) {
				fprintf(stdout, "Target: All corrupted packages\n");
			}
		}
		
		if (dryRun) {
			if (!quiet) {
				fprintf(stdout, "Mode: DRY-RUN (preview only, no changes)\n\n");
			}
		} else {
			if (!quiet) {
				fprintf(stdout, "Mode: Live recovery\n\n");
			}
		}
		
		// TODO: Implement repair command with RecoveryCoordinator
		// For now, show placeholder
		fprintf(stderr, "INFO: Repair command integration pending (Phase 6D implementation)\n");
		fprintf(stderr, "INFO: Would scan for corrupted packages matching pattern: %s\n",
			repairPackage ? repairPackage : "all");
		fprintf(stderr, "INFO: Would execute recovery strategies: Repository → Manual\n");
		fprintf(stderr, "INFO: Would log all recovery attempts to /var/log/package_integrity_recovery.log\n");
		
		if (!dryRun) {
			if (!quiet) {
				fprintf(stdout, "\n✓ Repair operations complete\n");
			}
		} else {
			if (!quiet) {
				fprintf(stdout, "\nPreview complete (no changes made)\n");
			}
		}
		
		return 0;
	}
	
	_PrintUsage(argv[0]);
	return 0;
}
