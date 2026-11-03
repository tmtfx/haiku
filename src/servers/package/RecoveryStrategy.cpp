#include "RecoveryStrategy.h"

#include <File.h>
#include <DateTime.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <Directory.h>
#include <Path.h>

// ========== RecoveryCoordinator ==========

RecoveryCoordinator::RecoveryCoordinator()
{
}

RecoveryCoordinator::~RecoveryCoordinator()
{
	for (int i = 0; i < fStrategies.CountItems(); i++) {
		delete (RecoveryStrategy*)fStrategies.ItemAt(i);
	}
	fStrategies.MakeEmpty();
}

status_t
RecoveryCoordinator::AddStrategy(RecoveryStrategy* strategy)
{
	if (!strategy)
		return B_BAD_VALUE;
	
	return fStrategies.AddItem(strategy) ? B_OK : B_NO_MEMORY;
}

RecoveryStrategy*
RecoveryCoordinator::FindStrategy(const PackageCorruptionInfo& corruption)
{
	for (int i = 0; i < fStrategies.CountItems(); i++) {
		RecoveryStrategy* strategy = (RecoveryStrategy*)fStrategies.ItemAt(i);
		if (strategy && strategy->CanRecover(corruption))
			return strategy;
	}
	
	return NULL;
}

status_t
RecoveryCoordinator::AttemptRecovery(
	const PackageCorruptionInfo& corruption,
	RecoveryResult& outResult)
{
	RecoveryStrategy* strategy = FindStrategy(corruption);
	if (!strategy) {
		outResult.status = RECOVERY_STATUS_SKIPPED;
		outResult.statusMessage = "No recovery strategy available";
		return B_NOT_SUPPORTED;
	}
	
	return strategy->Recover(corruption, outResult);
}

// ========== FileRecoveryLogger ==========

FileRecoveryLogger::FileRecoveryLogger(const char* logPath)
	: fLogPath(logPath)
{
}

FileRecoveryLogger::~FileRecoveryLogger()
{
}

status_t
FileRecoveryLogger::_EnsureLogFile()
{
	BPath logDir(fLogPath.String());
	logDir.GetParent(&logDir);
	
	// Try to create parent directories if they don't exist
	mkdir(logDir.Path(), 0755);
	
	// Try to create/open log file
	BFile file(fLogPath.String(), B_WRITE_ONLY | B_OPEN_AT_END | B_CREATE_FILE);
	return file.InitCheck();
}

BString
FileRecoveryLogger::_FormatTimestamp(time_t t) const
{
	struct tm* timeinfo = localtime(&t);
	char buffer[64];
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
	return BString(buffer);
}

void
FileRecoveryLogger::LogRecoveryStart(
	const PackageCorruptionInfo& corruption,
	const char* strategyName)
{
	if (_EnsureLogFile() != B_OK)
		return;
	
	BFile file(fLogPath.String(), B_WRITE_ONLY | B_OPEN_AT_END);
	if (file.InitCheck() != B_OK)
		return;
	
	BString timestamp = _FormatTimestamp(time(NULL));
	BString sourceStr;
	
	switch (corruption.sourceType) {
		case RECOVERY_SOURCE_REPOSITORY:
			sourceStr = "repository";
			break;
		case RECOVERY_SOURCE_LOCAL_FILE:
			sourceStr = "local";
			break;
		case RECOVERY_SOURCE_NETWORK:
			sourceStr = "network";
			break;
		case RECOVERY_SOURCE_UNKNOWN:
		default:
			sourceStr = "unknown";
			break;
	}
	
	char buffer[512];
	snprintf(buffer, sizeof(buffer),
		"[%s] RECOVERY_START: package=%s source=%s strategy=%s checksum_expected=%s\n",
		timestamp.String(),
		corruption.packageName.String(),
		sourceStr.String(),
		strategyName ? strategyName : "unknown",
		corruption.expectedChecksum.String());
	
	file.Write(buffer, strlen(buffer));
}

void
FileRecoveryLogger::LogRecoveryComplete(
	const RecoveryResult& result)
{
	if (_EnsureLogFile() != B_OK)
		return;
	
	BFile file(fLogPath.String(), B_WRITE_ONLY | B_OPEN_AT_END);
	if (file.InitCheck() != B_OK)
		return;
	
	BString timestamp = _FormatTimestamp(time(NULL));
	BString statusStr;
	
	switch (result.status) {
		case RECOVERY_STATUS_SUCCESS:
			statusStr = "SUCCESS";
			break;
		case RECOVERY_STATUS_FAILED:
			statusStr = "FAILED";
			break;
		case RECOVERY_STATUS_SKIPPED:
			statusStr = "SKIPPED";
			break;
		case RECOVERY_STATUS_MANUAL_REQUIRED:
			statusStr = "MANUAL_REQUIRED";
			break;
		case RECOVERY_STATUS_IN_PROGRESS:
			statusStr = "IN_PROGRESS";
			break;
		default:
			statusStr = "UNKNOWN";
			break;
	}
	
	char buffer[1024];
	snprintf(buffer, sizeof(buffer),
		"[%s] RECOVERY_COMPLETE: status=%s message=%s automatic=%s duration=%ld seconds errorCode=%d\n",
		timestamp.String(),
		statusStr.String(),
		result.statusMessage.String(),
		result.wasAutomatic ? "true" : "false",
		result.recoveryCompleteTime - result.recoveryAttemptTime,
		result.errorCode);
	
	file.Write(buffer, strlen(buffer));
	
	if (result.errorDetail.Length() > 0) {
		snprintf(buffer, sizeof(buffer),
			"[%s]   Error detail: %s\n",
			timestamp.String(),
			result.errorDetail.String());
		file.Write(buffer, strlen(buffer));
	}
}

void
FileRecoveryLogger::LogRecoveryError(
	const char* packageName,
	const char* error)
{
	if (_EnsureLogFile() != B_OK)
		return;
	
	BFile file(fLogPath.String(), B_WRITE_ONLY | B_OPEN_AT_END);
	if (file.InitCheck() != B_OK)
		return;
	
	BString timestamp = _FormatTimestamp(time(NULL));
	char buffer[512];
	snprintf(buffer, sizeof(buffer),
		"[%s] RECOVERY_ERROR: package=%s error=%s\n",
		timestamp.String(),
		packageName ? packageName : "unknown",
		error ? error : "unknown error");
	
	file.Write(buffer, strlen(buffer));
}

// ========== DesktopRecoveryNotifier ==========

void
DesktopRecoveryNotifier::NotifyAutoRecoveryStarted(const char* packageName)
{
	BString title("Package Recovery Started");
	BString message;
	message.SetToFormat("Recovering %s from repository...", packageName);
	_SendNotification(title.String(), message.String());
}

void
DesktopRecoveryNotifier::NotifyAutoRecoverySuccess(
	const char* packageName,
	const char* details)
{
	BString title("Package Recovery Successful");
	BString message;
	message.SetToFormat("%s has been recovered and re-activated. %s",
		packageName, details ? details : "");
	_SendNotification(title.String(), message.String());
}

void
DesktopRecoveryNotifier::NotifyAutoRecoveryFailed(
	const char* packageName,
	const char* error)
{
	BString title("Package Recovery Failed");
	BString message;
	message.SetToFormat("Could not recover %s: %s",
		packageName, error ? error : "unknown error");
	_SendNotification(title.String(), message.String());
}

void
DesktopRecoveryNotifier::NotifyManualRecoveryNeeded(
	const char* packageName,
	const char* reason)
{
	BString title("Manual Package Recovery Needed");
	BString message;
	message.SetToFormat("%s is corrupted. Please locate the original file. Reason: %s",
		packageName, reason ? reason : "unknown source");
	_SendNotification(title.String(), message.String());
}

void
DesktopRecoveryNotifier::_SendNotification(
	const char* title,
	const char* message)
{
	// TODO: Integrate with Haiku notification system
	// For now, just write to stderr for testing
	fprintf(stderr, "[NOTIFICATION] %s\n%s\n", title, message);
}

// Helper function to parse recovery source type from string
HPKGRecoverySource
HPKGParseRecoverySource(const char* sourceStr)
{
	if (!sourceStr)
		return RECOVERY_SOURCE_UNKNOWN_TYPE;
	
	if (strcmp(sourceStr, "repository") == 0)
		return RECOVERY_SOURCE_REPOSITORY;
	if (strcmp(sourceStr, "local") == 0 || strncmp(sourceStr, "local:", 6) == 0)
		return RECOVERY_SOURCE_LOCAL_FILE;
	if (strncmp(sourceStr, "http://", 7) == 0 || strncmp(sourceStr, "https://", 8) == 0)
		return RECOVERY_SOURCE_NETWORK;
	if (strcmp(sourceStr, "unknown") == 0)
		return RECOVERY_SOURCE_UNKNOWN;
	
	return RECOVERY_SOURCE_UNKNOWN_TYPE;
}

// Helper function to get string representation of recovery source
const char*
HPKGRecoverySourceToString(HPKGRecoverySource source)
{
	switch (source) {
		case RECOVERY_SOURCE_REPOSITORY:
			return "repository";
		case RECOVERY_SOURCE_UNKNOWN:
			return "unknown";
		case RECOVERY_SOURCE_LOCAL_FILE:
			return "local";
		case RECOVERY_SOURCE_NETWORK:
			return "network";
		default:
			return "unknown_type";
	}
}
