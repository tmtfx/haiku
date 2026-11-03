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
#include <ctime>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <ctype.h>

#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <ObjectList.h>
#include <Path.h>
#include <Node.h>
#include <TypeConstants.h>
#include <fs_attr.h>

#include "PackageVerifier.h"
#include "ConsoleReporter.h"
#include "IntegrityQuery.h"
#include "../../servers/package/RecoveryStrategy.h"
#include "../../servers/package/RepositoryRecovery.h"
#include "../../servers/package/ManualRecovery.h"

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
_ScanPackagesDirectory(PackageVerifier& verifier, ConsoleReporter& reporter,
	BObjectList<PackageVerificationResult, true>* outFailed = NULL)
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

		if (outFailed != NULL && !result.isValid)
			outFailed->AddItem(new PackageVerificationResult(result));
	}

	closedir(dir);

	if (filesProcessed == 0) {
		fprintf(stderr, "Warning: No HPKG files found in %s\n", PACKAGES_PATH);
	}

	return B_OK;
}


// Build a PackageCorruptionInfo (RecoveryStrategy's input type) from a
// failed PackageVerificationResult (fsck_hpkgs's own scan output type).
static PackageCorruptionInfo
_BuildCorruptionInfo(const PackageVerificationResult& result)
{
	PackageCorruptionInfo info;
	info.packagePath = result.filePath;
	info.packageName = result.filename;
	info.expectedChecksum = result.attributedChecksum;
	info.actualChecksum = result.calculatedChecksum;
	info.corruptionDetectedTime = time(NULL);
	info.sourceType = result.attributedSource.IsEmpty()
		? RECOVERY_SOURCE_UNKNOWN
		: HPKGParseRecoverySource(result.attributedSource.String());
	info.sourceInfo = result.attributedSource;
	return info;
}


// Attempt to recover one package. If replacementFile is given, it is
// handed directly to ManualRecovery (this is the only strategy that can
// actually succeed without a real network client - see
// RepositoryRecovery::_DownloadFromRepository). Otherwise, the normal
// RecoveryCoordinator dispatch is used (repository strategy first, falls
// through to manual, which - with no file provided - correctly reports
// that user intervention is needed rather than pretending to fix it).
static void
_AttemptRepair(const PackageVerificationResult& result,
	const char* replacementFile, bool dryRun,
	RecoveryLogger* logger, RecoveryNotifier* notifier)
{
	PackageCorruptionInfo info = _BuildCorruptionInfo(result);

	if (dryRun) {
		fprintf(stdout, "  Would attempt recovery for %s (source: %s)\n",
			info.packageName.String(),
			HPKGRecoverySourceToString(info.sourceType));
		return;
	}

	RecoveryResult recoveryResult;

	if (replacementFile != NULL) {
		ManualRecovery manual(logger, notifier);
		manual.RecoverWithFile(info, replacementFile, recoveryResult);
	} else {
		RecoveryCoordinator coordinator;
		coordinator.AddStrategy(new RepositoryRecovery(logger, notifier));
		coordinator.AddStrategy(new ManualRecovery(logger, notifier));
		coordinator.AttemptRecovery(info, recoveryResult);
	}

	fprintf(stdout, "  %s: %s\n", info.packageName.String(),
		recoveryResult.statusMessage.String());
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
	fprintf(stdout, "  --file <path>     Known-good replacement .hpkg to recover from\n");
	fprintf(stdout, "                    (required - there is no automatic network download;\n");
	fprintf(stdout, "                    --package must match exactly one file when used)\n");
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
	const char* repairFile = NULL;
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
		} else if (strcmp(argv[i], "--file") == 0) {
			if (i + 1 < argc) {
				repairFile = argv[++i];
			} else {
				fprintf(stderr, "Error: --file requires an argument\n");
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
		BObjectList<PackageVerificationResult, true> failedPackages;

		status_t status = _ScanPackagesDirectory(verifier, reporter,
			&failedPackages);
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
				}
				for (int32 i = 0; i < failedPackages.CountItems(); i++) {
					_AttemptRepair(*failedPackages.ItemAt(i), NULL, true,
						NULL, NULL);
				}
			} else {
				if (!quiet) {
					fprintf(stdout, "\n=== Attempting Recovery ===\n");
					fprintf(stdout, "Attempting to recover %u corrupted/unverified package(s)...\n",
						corruptFiles + missingAttributeFiles);
				}

				FileRecoveryLogger logger;
				SilentRecoveryNotifier notifier;
				for (int32 i = 0; i < failedPackages.CountItems(); i++) {
					_AttemptRepair(*failedPackages.ItemAt(i), NULL, false,
						&logger, &notifier);
				}
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
		if (!repairPackage) {
			fprintf(stderr, "Error: repair requires --package <name or glob>\n");
			return 1;
		}

		if (!quiet) {
			fprintf(stdout, "Repair mode: Target %s%s\n\n", repairPackage,
				dryRun ? " (DRY-RUN, no changes)" : "");
		}

		DIR* dir = opendir(packagesPath);
		if (dir == NULL) {
			fprintf(stderr, "Error: Cannot open %s: %m\n", packagesPath);
			return 1;
		}

		PackageVerifier verifier;
		BObjectList<PackageVerificationResult, true> matches;

		struct dirent* dirEntry;
		while ((dirEntry = readdir(dir)) != NULL) {
			if (dirEntry->d_name[0] == '.' || !_IsHPKGFile(dirEntry->d_name))
				continue;

			if (fnmatch(repairPackage, dirEntry->d_name, 0) != 0)
				continue;

			BPath fullPath(packagesPath);
			fullPath.Append(dirEntry->d_name);

			PackageVerificationResult* result = new PackageVerificationResult();
			verifier.VerifyPackage(fullPath.Path(), result);
			matches.AddItem(result);
		}
		closedir(dir);

		if (matches.CountItems() == 0) {
			fprintf(stderr, "No package matching \"%s\" found in %s\n",
				repairPackage, packagesPath);
			return 1;
		}

		if (repairFile != NULL && matches.CountItems() > 1) {
			fprintf(stderr, "Error: --package matched %" B_PRId32 " files; "
				"--file only makes sense for exactly one\n",
				matches.CountItems());
			return 1;
		}

		FileRecoveryLogger logger;
		SilentRecoveryNotifier notifier;

		for (int32 i = 0; i < matches.CountItems(); i++) {
			const PackageVerificationResult& result = *matches.ItemAt(i);

			if (result.isValid && repairFile == NULL) {
				if (!quiet) {
					fprintf(stdout, "  %s: already OK, nothing to repair\n",
						result.filename.String());
				}
				continue;
			}

			_AttemptRepair(result, repairFile, dryRun,
				dryRun ? NULL : &logger, dryRun ? NULL : &notifier);
		}

		if (!quiet) {
			fprintf(stdout, dryRun ? "\nPreview complete (no changes made)\n"
				: "\nRepair attempt(s) complete - see above for per-package "
					"results.\n");
		}

		return 0;
	}
	
	_PrintUsage(argv[0]);
	return 0;
}
