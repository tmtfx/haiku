# HPKG Integrity System - User Guide

## Overview

The HPKG Integrity System is a data integrity verification and automatic recovery system for Haiku packages. It ensures that installed `.hpkg` files remain uncorrupted by:

- Storing SHA256 checksums of packages in BFS extended attributes
- Verifying packages during periodic scans
- Automatically recovering corrupted packages from the repository
- Providing both CLI and GUI interfaces for manual verification and recovery

**Key Principle**: HPKG files are immutable. Any difference from the original is corruption, not an intentional change.

---

## Quick Start

### CLI: Scan All Packages

Check all installed packages for corruption:

```bash
fsck_hpkgs scan
```

Output example:
```
Scanning /boot/system/packages...

✓ curl-8.5.0-1-x86_64.hpkg           [VERIFIED]   (source: repository)
✓ openssl-3.2.0-1-x86_64.hpkg        [VERIFIED]   (source: repository)
⚠ git-2.43.0-1-x86_64.hpkg           [UNVERIFIED] (no checksum found)
✗ libc-1.0.1-1-x86_64.hpkg           [CORRUPTED]  (mismatch detected)

Scan complete: 4 packages, 2 verified, 1 unverified, 1 corrupted
```

### CLI: Query Corrupted Packages

Find all corrupted packages quickly:

```bash
fsck_hpkgs query --status corrupted
```

### GUI: Full Integrity Checker App

Launch the graphical interface:

```bash
IntegrityChecker
```

The app opens with three tabs:

1. **Scan Results** - View verification history and status
2. **Query Builder** - Run predefined or custom queries
3. **Settings** - Configure auto-repair and notifications

---

## CLI Reference

### Command: `scan`

Scans all packages in `/boot/system/packages/` and verifies their checksums.

```bash
fsck_hpkgs scan [OPTIONS]
```

**Options:**
- `--json` - Output results in JSON format for scripting
- `--verbose` - Show detailed verification information for each package
- `--recursive` - Include subdirectories (default: only top level)

**Example:**
```bash
# Verbose scan with JSON output
fsck_hpkgs scan --verbose --json | jq '.packages[] | select(.status == "corrupted")'
```

### Command: `query`

Query packages by status or source without performing a scan.

```bash
fsck_hpkgs query [OPTIONS]
```

**Options:**
- `--status {verified|corrupted|unverified}` - Filter by verification status
- `--source {repository|unknown|local|network}` - Filter by installation source
- `--package-name <pattern>` - Filter by package name (supports wildcards)

**Examples:**
```bash
# Find all unverified packages
fsck_hpkgs query --status unverified

# Find all packages from repository
fsck_hpkgs query --source repository

# Find all git-related packages
fsck_hpkgs query --package-name "git*"

# Combine filters
fsck_hpkgs query --status corrupted --source repository
```

### Command: `repair`

Repair corrupted packages by re-downloading from the repository or manual recovery.

```bash
fsck_hpkgs repair [OPTIONS]
```

**Options:**
- `--dry-run` - Show what would be repaired without making changes
- `--auto` - Automatically repair all corrupted repository packages (interactive for manual)
- `--package <name>` - Repair specific package only

**Examples:**
```bash
# Preview repairs
fsck_hpkgs repair --dry-run

# Auto-repair all corrupted repository packages
fsck_hpkgs repair --auto

# Repair specific package with prompts
fsck_hpkgs repair --package curl-8.5.0-1-x86_64.hpkg

# Auto-repair with detailed output
fsck_hpkgs repair --auto --verbose
```

### Command: `help`

Display command help and options.

```bash
fsck_hpkgs help [COMMAND]
```

---

## Extended Attributes (Technical Reference)

Each package has three extended attributes stored in the BFS filesystem:

### `integrity.hpkg.sha256`
- **Type**: 64-character hexadecimal string
- **Content**: SHA256 hash of the package file
- **Set by**: Package daemon at activation time
- **Source**: Repository verification checksum

**View with:**
```bash
catattr integrity.hpkg.sha256 /boot/system/packages/curl-8.5.0-1-x86_64.hpkg
```

**Example output:**
```
a7f3e9c2d1b5f4e8c3a2d9f1b5e7c4a1d2f9e3c5b8a7d1e4f2c9a3d6e1f4c7
```

### `integrity.hpkg.verified`
- **Type**: ISO 8601 timestamp with UTC suffix
- **Content**: Time when checksum was last verified
- **Format**: `YYYY-MM-DDTHH:MM:SS UTC`

**Example output:**
```
2026-11-02T22:10:15 UTC
```

### `integrity.hpkg.source`
- **Type**: Text string
- **Values**: `repository`, `unknown`, `local`, `network`
- **Meaning**: Where the package was installed from

**Values explained:**
- `repository` - From official Haiku package repository (auto-repairable)
- `unknown` - Source unknown or undetermined
- `local` - From local file or filesystem location
- `network` - From network share or temporary download

---

## GUI: IntegrityChecker Application

### Tab 1: Scan Results

Shows a list of all scanned packages with status indicators:

| Symbol | Meaning | Color |
|--------|---------|-------|
| ✓ | Verified | Green |
| ✗ | Corrupted | Red |
| ⚠ | Unverified | Orange |

**Actions:**
- Double-click a package to see details
- Right-click to show context menu (copy path, show in Tracker, etc.)
- [Scan All] button - Run full package scan in background
- [Refresh] button - Reload results from last scan

**Display columns:**
- Package Name
- Status (✓/✗/⚠)
- Verification Time
- Source
- File Path

### Tab 2: Query Builder

Pre-configured queries for common tasks:

**Quick Queries:**
- [All Corrupted] - Shows `status == "corrupted"`
- [All Unverified] - Shows `status == "unverified"`
- [Repository Only] - Shows `source == "repository"`
- [Unknown Source] - Shows `source == "unknown"`
- [Last 24 Hours] - Shows `verified > (now - 24h)`
- [Failed Auto-Repair] - Shows `auto_repair_failed == true`
- [All Packages] - Shows all (no filters)
- [Custom Query] - Build your own filters

**Custom Query Builder:**
1. Select field (Status, Source, Date Range, Package Name)
2. Select operator (equals, contains, before, after, etc.)
3. Enter value
4. Click [Add Filter]
5. Click [Execute Query]

### Tab 3: Settings

Configure application behavior:

- **Auto-Repair on Corruption Detection**: Enable/Disable
  - When enabled, corrupted repository packages are repaired automatically
  - Manual packages require user intervention

- **Notifications**:
  - [ ] Show desktop notifications for corruption
  - [ ] Show notifications for auto-repair attempts
  - [ ] Show notifications for recovery success/failure

- **Logging**:
  - [ ] Enable detailed logging to file
  - Log location: `/var/log/integrity_checker.log`

- **Scan Scheduling**:
  - Background scan interval: [1 hour ▼]
  - [ ] Run scans in background automatically

- **Recovery Options**:
  - Timeout for manual recovery dialog: [120 seconds ▼]
  - [ ] Auto-download from repository (repository packages only)

---

## Common Workflows

### Scenario 1: Weekly Package Integrity Check

```bash
# Schedule via cron or task scheduler
0 3 * * 0 fsck_hpkgs scan --json > /var/log/weekly-integrity-check.log
```

Or use the GUI: Enable "Background scan interval" in Settings, set to "1 week".

### Scenario 2: Fix a Single Corrupted Package

```bash
# Identify the package
fsck_hpkgs query --status corrupted

# Repair it
fsck_hpkgs repair --package libc-1.0.1-1-x86_64.hpkg
```

### Scenario 3: Bulk Repair After System Crash

```bash
# See what would be repaired
fsck_hpkgs repair --dry-run

# Auto-repair all repository packages
fsck_hpkgs repair --auto

# Manually recover any remaining packages via GUI
IntegrityChecker  # Switch to Scan Results tab, use context menu
```

### Scenario 4: Monitor Integrity During Development

```bash
# Continuous monitoring (re-scan every 5 minutes)
while true; do
  fsck_hpkgs scan --json | jq '.summary'
  sleep 300
done
```

---

## Troubleshooting

### Problem: "No checksum found" for newly installed packages

**Cause**: Package daemon hasn't written the checksum yet (rare).

**Solution**:
1. Verify package is intact: `ls -l /boot/system/packages/package-name.hpkg`
2. Wait 10 seconds for daemon to complete
3. Re-scan: `fsck_hpkgs scan`
4. If still unverified, try manual verification:
   ```bash
   fsck_hpkgs repair --package package-name.hpkg
   ```

### Problem: "Checksum mismatch" for repository packages

**Cause**: Package file corrupted (disk error, failed download, etc.).

**Solution**:
1. Verify corruption: `fsck_hpkgs query --status corrupted`
2. Auto-repair:
   ```bash
   fsck_hpkgs repair --auto
   ```
3. Verify repair succeeded:
   ```bash
   fsck_hpkgs scan
   ```

### Problem: "Source unknown" for manually installed packages

**Cause**: Package was installed manually (not from repository), source tracking unknown.

**Solution**:
1. Check file integrity manually:
   ```bash
   fsck_hpkgs query --source unknown
   ```
2. If corrupted, use manual recovery:
   ```bash
   fsck_hpkgs repair --package package-name.hpkg
   # Follow prompts to locate replacement file
   ```

### Problem: GUI app won't start

**Cause**: Potential missing dependencies or filesystem issues.

**Solution**:
1. Try CLI equivalent:
   ```bash
   fsck_hpkgs scan
   ```
2. Check logs:
   ```bash
   cat /var/log/integrity_checker.log
   ```
3. Restart package daemon:
   ```bash
   pkill package_daemon
   sleep 1
   # Daemon restarts automatically
   ```

### Problem: Scan takes too long on large package collections

**Cause**: Verifying 100+ packages with SHA256 takes time.

**Solution**:
1. Use query instead (instant, if attributes already indexed):
   ```bash
   fsck_hpkgs query --status corrupted
   ```
2. Run full scan in background:
   ```bash
   IntegrityChecker  # Enable "Background scan interval" in Settings
   ```
3. For scripting, use `--json` and parse results for conditional logic.

---

## Advanced: Manual Attribute Management

### View all extended attributes on a package

```bash
lsattr /boot/system/packages/curl-8.5.0-1-x86_64.hpkg
```

### View specific attribute

```bash
catattr integrity.hpkg.sha256 /boot/system/packages/curl-8.5.0-1-x86_64.hpkg
```

### Manually set an attribute (not recommended)

```bash
addattr -t string integrity.hpkg.sha256 \
  "a7f3e9c2d1b5f4e8c3a2d9f1b5e7c4a1d2f9e3c5b8a7d1e4f2c9a3d6e1f4c7" \
  /boot/system/packages/curl-8.5.0-1-x86_64.hpkg
```

⚠️ **Warning**: Only do this if you are absolutely certain of the checksum value. Incorrect values will cause the integrity system to report false positives.

---

## Getting Help

- **CLI help**: `fsck_hpkgs help`
- **CLI command help**: `fsck_hpkgs help scan`
- **Report issues**: Submit feedback in Haiku bug tracker with category "Pirati del Frico - Integrity System"
- **Developer questions**: See `DEVELOPER_REFERENCE.md`

---

**Last Updated**: November 2026  
**Project**: Pirati del Frico HPKG Integrity System  
**Repository**: tmtfx/haiku (hpkg_chk branch)
