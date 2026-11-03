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

// Shared real recovery primitives used by every RecoveryStrategy.
//
// Earlier revisions of this either moved the .hpkg file directly in/out
// of the packages directory, or called `pkgman uninstall`+`install` to
// force a reinstall. Both turned out to be the wrong call for a pure
// content-corruption repair (same package, same version, just bad bytes
// on disk):
//   - Moving the file in/out of the watched packages directory bypasses
//     dependency checking entirely - the daemon-side node-monitor path
//     (CommitTransactionHandler.cpp) applies whatever activate/
//     deactivate set it is given with no BSolver involved at all; the
//     dependency check only exists client-side, in whatever submits a
//     transaction.
//   - `pkgman uninstall` *does* go through BSolver::VerifyInstallation()
//     (PackageManager.cpp), but for an uninstall that also means
//     computing the "inverse base package closure" and proposing to
//     remove every *healthy* package that depends on the one being
//     repaired - a dependency check that actively wants to do too much,
//     not too little. `pkgman install <name>` also doesn't help on its
//     own: the solver sees the same name/version as already installed
//     and does nothing (BNothingToDoException, see command_install.cpp).
//
// The actual fix needed is simpler than either of those: fetch a
// verified-correct copy of the exact same version, and overwrite the
// existing file's *content* in place - same directory entry, same
// inode, no create/remove/rename at all. That means packagefs's node
// monitor never fires, so there is no activation change and therefore
// no dependency check to get wrong either way; the corrected content
// only takes effect on the next reboot/remount, exactly like this
// fork's own core "haiku" packages already behave today (changes to
// those are explicitly deferred to next boot - see
// CommitTransactionHandler.cpp's _IsSystemPackage() handling).
//
// `pkgman download` (src/bin/pkgman/command_download.cpp) is the piece
// that makes the first half possible without writing a network client
// here: it resolves <packageName> through the same solver/repository
// configuration as every other pkgman command and fetches+verifies the
// file via BPackageManager::DownloadPackage(), but - unlike install/
// uninstall - never touches installed state, so no dependency logic is
// ever invoked.

// Run `pkgman download <packageName> -o <outputPath>` and parse the
// "sha256: <hex>" line it prints on success into outChecksum. The
// caller must still compare outChecksum against whatever checksum it
// was expecting: the repository may have moved on to a different
// version of the same name since this package was installed, and that
// is not something pkgman download has any way to know about.
status_t HPKGPkgmanDownload(const char* packageName,
	const char* outputPath, BString& outChecksum);

// Replace packagePath's content with sourcePath's, byte for byte,
// without creating/removing/renaming the directory entry.
status_t HPKGOverwritePackageContent(const char* packagePath,
	const char* sourcePath);

#endif // _RECOVERY_STRATEGY_H
