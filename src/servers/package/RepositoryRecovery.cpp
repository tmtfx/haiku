#include "RepositoryRecovery.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

RepositoryRecovery::RepositoryRecovery(
	RecoveryLogger* logger,
	RecoveryNotifier* notifier)
	: fLogger(logger), fNotifier(notifier)
{
}

RepositoryRecovery::~RepositoryRecovery()
{
}

bool
RepositoryRecovery::CanRecover(const PackageCorruptionInfo& corruption)
{
	// Only handle repository-sourced packages
	return corruption.sourceType == RECOVERY_SOURCE_REPOSITORY;
}

BString
RepositoryRecovery::DescribeRecovery(const PackageCorruptionInfo& corruption)
{
	BString description;
	description.SetToFormat(
		"Auto-recover from repository: pkgman uninstall + reinstall %s",
		corruption.packageName.String());
	return description;
}

status_t
RepositoryRecovery::_ExtractPackageInfo(
	const char* packagePath,
	BString& outName,
	BString& outVersion)
{
	if (!packagePath)
		return B_BAD_VALUE;

	// Extract filename from path
	const char* filename = strrchr(packagePath, '/');
	if (filename)
		filename++;
	else
		filename = packagePath;

	// Remove .hpkg extension
	BString cleanName(filename);
	if (cleanName.EndsWith(".hpkg"))
		cleanName.Truncate(cleanName.Length() - 5);

	// Try to split on last dash to separate name from version
	// Format: name-version.hpkg
	int32 lastDash = cleanName.FindLast('-');
	if (lastDash > 0) {
		outName = cleanName;
		outName.Truncate(lastDash);
		outVersion = cleanName;
		outVersion.Remove(0, lastDash + 1);
	} else {
		outName = cleanName;
		outVersion = "unknown";
	}

	return B_OK;
}

status_t
RepositoryRecovery::Recover(
	const PackageCorruptionInfo& corruption,
	RecoveryResult& outResult)
{
	outResult.status = RECOVERY_STATUS_IN_PROGRESS;
	outResult.recoveryAttemptTime = time(NULL);
	outResult.wasAutomatic = true;

	if (fNotifier)
		fNotifier->NotifyAutoRecoveryStarted(corruption.packageName.String());
	if (fLogger)
		fLogger->LogRecoveryStart(corruption, StrategyName());

	BString packageName, packageVersion;
	status_t status = _ExtractPackageInfo(
		corruption.packagePath.String(), packageName, packageVersion);

	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "Failed to extract package info";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		return status;
	}

	// pkgman's own installed-package check would otherwise treat the
	// corrupted package as "already installed, nothing to do" (same
	// name/version is still recorded even though the file content is
	// bad - see command_install.cpp's BNothingToDoException handling),
	// so it has to be uninstalled first. Both uninstall and install go
	// through BSolver::VerifyInstallation() before touching anything
	// (PackageManager.cpp) - this is the real, dependency-checked path,
	// unlike moving the .hpkg file directly.
	status = HPKGPkgmanUninstall(packageName.String());
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage =
			"pkgman uninstall failed - likely blocked by a dependency "
			"check (something still needs this package)";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Could not safely remove the corrupted package");
		}
		return status;
	}

	status = HPKGPkgmanInstall(packageName.String());
	if (status != B_OK) {
		outResult.status = RECOVERY_STATUS_FAILED;
		outResult.statusMessage = "pkgman install failed (repository "
			"unreachable, or the package is no longer available there)";
		outResult.errorCode = status;
		if (fLogger)
			fLogger->LogRecoveryComplete(outResult);
		if (fNotifier) {
			fNotifier->NotifyAutoRecoveryFailed(
				corruption.packageName.String(),
				"Re-download via pkgman failed - the package was left "
				"uninstalled, manual recovery is required");
		}
		return status;
	}

	outResult.status = RECOVERY_STATUS_SUCCESS;
	outResult.statusMessage =
		"Package uninstalled and reinstalled from the repository via "
		"pkgman";
	outResult.recoveredFilePath = corruption.packagePath;
	outResult.recoveryCompleteTime = time(NULL);

	if (fLogger)
		fLogger->LogRecoveryComplete(outResult);

	if (fNotifier) {
		BString detail;
		detail.SetToFormat("Version: %s, Source: repository",
			packageVersion.String());
		fNotifier->NotifyAutoRecoverySuccess(
			corruption.packageName.String(), detail.String());
	}

	return B_OK;
}
