#ifndef _RECOVERY_STRATEGY_H
#define _RECOVERY_STRATEGY_H

#include <SupportDefs.h>
#include <String.h>
#include <List.h>
#include <time.h>

// Recovery source type constants
enum HPKGRecoverySource {
	RECOVERY_SOURCE_REPOSITORY = 0,
	RECOVERY_SOURCE_UNKNOWN = 1,
	RECOVERY_SOURCE_LOCAL_FILE = 2,
	RECOVERY_SOURCE_NETWORK = 3,
	RECOVERY_SOURCE_UNKNOWN_TYPE = -1
};

// Recovery status flags
enum HPKGRecoveryStatus {
	RECOVERY_STATUS_UNKNOWN = 0,
	RECOVERY_STATUS_SUCCESS = 1,
	RECOVERY_STATUS_IN_PROGRESS = 2,
	RECOVERY_STATUS_FAILED = 3,
	RECOVERY_STATUS_SKIPPED = 4,
	RECOVERY_STATUS_MANUAL_REQUIRED = 5
};

// Corruption details
struct PackageCorruptionInfo {
	BString packagePath;
	BString packageName;
	BString expectedChecksum;
	BString actualChecksum;
	time_t corruptionDetectedTime;
	HPKGRecoverySource sourceType;
	BString sourceInfo;		// Path, URL, or repo identifier
	
	PackageCorruptionInfo()
		: corruptionDetectedTime(0), sourceType(RECOVERY_SOURCE_UNKNOWN_TYPE) {}
};

// Recovery result
struct RecoveryResult {
	HPKGRecoveryStatus status;
	BString statusMessage;
	BString recoveredFilePath;
	time_t recoveryAttemptTime;
	time_t recoveryCompleteTime;
	bool wasAutomatic;
	int errorCode;
	BString errorDetail;
	
	RecoveryResult()
		: status(RECOVERY_STATUS_UNKNOWN), recoveryAttemptTime(0),
		  recoveryCompleteTime(0), wasAutomatic(false), errorCode(0) {}
};

// Recovery strategy interface
class RecoveryStrategy {
public:
	virtual ~RecoveryStrategy() {}
	
	// Check if this strategy can handle the given corruption
	virtual bool CanRecover(const PackageCorruptionInfo& corruption) = 0;
	
	// Get human-readable strategy name
	virtual const char* StrategyName() const = 0;
	
	// Get recovery description for UI display
	virtual BString DescribeRecovery(const PackageCorruptionInfo& corruption) = 0;
	
	// Perform the recovery
	// Returns B_OK if recovery completed, other error codes for failures
	// Result contains detailed status
	virtual status_t Recover(
		const PackageCorruptionInfo& corruption,
		RecoveryResult& outResult) = 0;
	
	// Check if this recovery requires user interaction
	virtual bool RequiresUserInteraction() const = 0;
};

// Recovery coordinator - manages multiple strategies
class RecoveryCoordinator {
public:
	RecoveryCoordinator();
	~RecoveryCoordinator();
	
	// Add a recovery strategy (coordinator takes ownership)
	status_t AddStrategy(RecoveryStrategy* strategy);
	
	// Find appropriate strategy for corruption
	RecoveryStrategy* FindStrategy(const PackageCorruptionInfo& corruption);
	
	// Execute recovery with best available strategy
	status_t AttemptRecovery(
		const PackageCorruptionInfo& corruption,
		RecoveryResult& outResult);
	
private:
	BList fStrategies;
};

// Logging interface for recovery operations
class RecoveryLogger {
public:
	virtual ~RecoveryLogger() {}
	
	virtual void LogRecoveryStart(
		const PackageCorruptionInfo& corruption,
		const char* strategyName) = 0;
	
	virtual void LogRecoveryComplete(
		const RecoveryResult& result) = 0;
	
	virtual void LogRecoveryError(
		const char* packageName,
		const char* error) = 0;
};

// File-based recovery logger implementation
class FileRecoveryLogger : public RecoveryLogger {
public:
	FileRecoveryLogger(const char* logPath = "/var/log/package_integrity_recovery.log");
	~FileRecoveryLogger();
	
	virtual void LogRecoveryStart(
		const PackageCorruptionInfo& corruption,
		const char* strategyName);
	
	virtual void LogRecoveryComplete(
		const RecoveryResult& result);
	
	virtual void LogRecoveryError(
		const char* packageName,
		const char* error);
	
private:
	BString fLogPath;
	
	status_t _EnsureLogFile();
	BString _FormatTimestamp(time_t t) const;
};

// Notification interface for user feedback
class RecoveryNotifier {
public:
	virtual ~RecoveryNotifier() {}
	
	// Show notification for auto-recovery
	virtual void NotifyAutoRecoveryStarted(
		const char* packageName) = 0;
	
	virtual void NotifyAutoRecoverySuccess(
		const char* packageName,
		const char* details) = 0;
	
	virtual void NotifyAutoRecoveryFailed(
		const char* packageName,
		const char* error) = 0;
	
	// Show notification for manual recovery
	virtual void NotifyManualRecoveryNeeded(
		const char* packageName,
		const char* reason) = 0;
};

// Silent notifier (no-op, for command-line use)
class SilentRecoveryNotifier : public RecoveryNotifier {
public:
	virtual void NotifyAutoRecoveryStarted(const char*) override {}
	virtual void NotifyAutoRecoverySuccess(const char*, const char*) override {}
	virtual void NotifyAutoRecoveryFailed(const char*, const char*) override {}
	virtual void NotifyManualRecoveryNeeded(const char*, const char*) override {}
};

// Desktop notification notifier (for GUI integration)
class DesktopRecoveryNotifier : public RecoveryNotifier {
public:
	virtual void NotifyAutoRecoveryStarted(const char* packageName);
	virtual void NotifyAutoRecoverySuccess(const char* packageName, const char* details);
	virtual void NotifyAutoRecoveryFailed(const char* packageName, const char* error);
	virtual void NotifyManualRecoveryNeeded(const char* packageName, const char* reason);

private:
	void _SendNotification(const char* title, const char* message);
};

// Parse/format the integrity.hpkg.source attribute value into/from the
// HPKGRecoverySource enum (implemented in RecoveryStrategy.cpp).
HPKGRecoverySource HPKGParseRecoverySource(const char* sourceStr);
const char* HPKGRecoverySourceToString(HPKGRecoverySource source);

// Shared real activation primitives used by every RecoveryStrategy.
//
// Earlier revisions of this moved the .hpkg file directly in/out of the
// packages directory (packagefs's node monitor does react to that, see
// Volume.cpp). That turned out to be the wrong call: that path is a
// *daemon-side* mechanism with no dependency checking at all - the
// daemon blindly applies whatever activate/deactivate set it is given,
// whether that came from a real pkgman transaction or a file appearing/
// disappearing on its own. pkgman's `install`/`uninstall` commands go
// through BPackageManager, which calls BSolver::VerifyInstallation()
// and handles any problems *before* touching the packages directory
// (see src/kits/package/manager/PackageManager.cpp, and
// src/bin/pkgman/command_install.cpp / command_uninstall.cpp) - that is
// the real, safe, dependency-checked path, and it is also the only
// thing in this tree that can actually fetch a package from its
// repository, so it replaces the download step too.
//
// Run `pkgman <command> -y <argument>` and wait for it. Returns B_OK
// only if pkgman exited 0; a dependency problem or a missing/unreachable
// repository makes it fail (and -y makes pkgman fail rather than
// prompt), which is exactly what should happen instead of silently
// going ahead.
status_t HPKGPkgmanUninstall(const char* packageName);
status_t HPKGPkgmanInstall(const char* packageNameOrPath);

#endif // _RECOVERY_STRATEGY_H
