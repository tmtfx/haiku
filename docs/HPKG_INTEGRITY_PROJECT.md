# HPKG Integrity Checking System - Project Plan
## "Pirati del Frico" Fork Enhancement

**Branch:** `hpkg_chk`  
**Status:** Planning Phase  
**Last Updated:** 2026-11-02

---

## 📋 Table of Contents

1. [Executive Summary](#executive-summary)
2. [Problem Statement](#problem-statement)
3. [Proposed Solution](#proposed-solution)
4. [System Architecture](#system-architecture)
5. [Implementation Phases](#implementation-phases)
6. [Data Flow & Integration Points](#data-flow--integration-points)
7. [Detailed Implementation Steps](#detailed-implementation-steps)
8. [Testing Strategy](#testing-strategy)
9. [Security & Reliability Considerations](#security--reliability-considerations)
10. [References & Dependencies](#references--dependencies)

---

## Executive Summary

This project implements **SHA256-based data integrity checking** for HPKG (Haiku Package) files using **BFS extended attributes**, providing **ZFS/btrfs-like scrubbing capabilities** with **automatic corruption detection and recovery**.

### Key Features
- ✅ **Immutable checksum storage** in BFS extended attributes (`user.hpkg.sha256`)
- ✅ **Zero file modification** - HPKG files remain identical byte-for-byte
- ✅ **Automatic verification** via `fsck.hpkgs` tool
- ✅ **Corruption detection** - any mismatch triggers alert
- ✅ **Automatic recovery** - re-download from repository
- ✅ **BFS indexable** - fast queries on integrity status
- ✅ **Non-intrusive** - repository checksums are captured, never recalculated

### Why This Matters for "Pirati del Frico"
- Haiku's BFS journaling covers **only metadata**, not data
- HPKG files are **immutable** - any change is corruption
- Adds **stability layer** without security assumptions (appropriate for Haiku's security model)
- Enables **package ecosystem confidence** in a personal fork

---

## Problem Statement

### Current Gaps in BFS/HPKG Architecture

1. **No Data Integrity Checking**
   - BFS journaling protects metadata, not file contents
   - Bitrot, disk errors, or data corruption go undetected
   - HPKG files are assumed immutable but unverified

2. **No Verification After Installation**
   - Once a package is activated, its integrity is never checked
   - Corruption only discovered at runtime (app crashes, failures)
   - No automated way to detect corrupted packages

3. **Recovery Path Undefined**
   - When corruption is suspected, no standard procedure exists
   - Manual re-installation is tedious
   - No way to verify the fix worked

### Use Cases Addressed
- **Bitrot Detection:** Automatic discovery of gradual data corruption
- **Disk Error Recovery:** Identify packages affected by hardware errors
- **Network Transfer Integrity:** Verify package wasn't corrupted during download
- **Storage Migration:** Validate packages after system migration/backup restore
- **Maintenance:** Scheduled integrity audits on `/boot/system/packages`

---

## Proposed Solution

### Core Concept: Attribute-Based Checksums

Store SHA256 checksums in **BFS extended attributes** of HPKG files:

```
File: /boot/system/packages/haiku-0.1-1-x86_64.hpkg
├── Data: [unchanged, byte-for-byte identical]
└── Extended Attributes:
    ├── user.hpkg.sha256    = "a1b2c3d4...ef0" (64-char hex)
    ├── user.hpkg.verified  = "2026-11-02 22:10:15 UTC"
    └── user.hpkg.source    = "repository"
```

### Why Extended Attributes?
1. **Non-invasive** - HPKG file binary remains untouched
2. **Indexable** - BFS can index attributes for fast queries
3. **Immutable expectation** - Attributes stay with file via `cp -a`
4. **Flexible** - Can store additional metadata (source, algorithm, timestamp)
5. **Standard** - BFS supports extended attributes natively

### Checksum Source: Critical Design Decision
- ✅ **CORRECT:** Capture checksum from repository (already verified)
- ❌ **WRONG:** Recalculate SHA256 after file written (risks certifying corrupted data)

The checksum is obtained from `DownloadFileRequest::fChecksum`, which is already verified by the package manager during download. This is the **single source of truth**.

---

## System Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────┐
│         HPKG Integrity System Architecture              │
└─────────────────────────────────────────────────────────┘

Layer 1: Package Installation
  ├── Repository Download (DownloadFileRequest)
  │   └─ SHA256 already verified
  ├── Package Activation (CommitTransactionHandler)
  │   └─ NEW: HPKGIntegrityWriter captures checksum
  └─ Attribute Write (BNode::WriteAttr)
      └─ user.hpkg.sha256 persisted to disk

Layer 2: Verification & Monitoring
  ├── fsck.hpkgs CLI tool
  │   ├─ Scans /boot/system/packages
  │   ├─ Reads user.hpkg.sha256
  │   └─ Computes current SHA256 & compares
  ├── IntegrityChecker utility
  │   └─ Reusable verification logic
  └─ IntegrityIndex (optional)
      └─ BFS attribute queries

Layer 3: Recovery & Reporting
  ├─ Corruption Detection → Alert user
  ├─ Re-download from repo (optional)
  └─ Automated repair framework
```

### Data Structures

**HPKGIntegrityConstants.h:**
```cpp
namespace BPackageKit {
namespace BPrivate {

// Attribute names
#define HPKG_ATTR_CHECKSUM "user.hpkg.sha256"
#define HPKG_ATTR_VERIFIED "user.hpkg.verified"
#define HPKG_ATTR_SOURCE   "user.hpkg.source"

// Checksum format
#define HPKG_SHA256_HEX_LENGTH 64
#define HPKG_SHA256_ALGORITHM  "SHA256"

}  // namespace BPrivate
}  // namespace BPackageKit
```

### Integration Points

**Checksum Write (Phase 2):**
- Location: `CommitTransactionHandler::Handle()` after activation succeeds
- Method: `HPKGIntegrityWriter::WriteChecksum(entry_ref, sha256_string)`
- Timing: Synchronous write, non-blocking if attribute write fails
- Source: From `Package::Info()` or repository metadata

**Checksum Verification (Phase 3):**
- Location: `fsck.hpkgs` CLI tool (`src/bin/fsck.hpkgs/`)
- Method: `IntegrityChecker::VerifyPackage(path)`
- Output: JSON or text report with per-package status

---

## Implementation Phases

### Phase 1: Attribute Schema Definition ⏳ IN PROGRESS

**Objective:** Define constants, data structures, and attribute schema

**Deliverable:** `src/kits/package/hpkg/HPKGIntegrityConstants.h`

**Tasks:**
- [ ] Define attribute names and buffer sizes
- [ ] Document hash algorithm constants
- [ ] Create helper functions for hex encoding/decoding
- [ ] Add Jamfile for kit compilation

**Estimated Duration:** 1-2 hours

**Key Decision:** Hex string format (64 chars) vs binary (32 bytes)
- Chosen: Hex string (human-readable, easier logging/debugging)

---

### Phase 2: Checksum Write (Package Daemon) ⏳ PENDING

**Objective:** Modify package daemon to write SHA256 to attributes during activation

**Deliverables:**
- `src/servers/package/HPKGIntegrityWriter.{h,cpp}`
- Modified: `src/servers/package/CommitTransactionHandler.cpp`

**Key Points:**
- Checksum source: Already verified from repository download
- Write location: After successful package activation
- Error handling: Non-blocking (log warning, continue)
- Thread safety: Use atomic write or mutex

**Tasks:**
- [ ] Create HPKGIntegrityWriter class
- [ ] Implement WriteChecksum(entry_ref, checksum_string)
- [ ] Hook into CommitTransactionHandler
- [ ] Add error handling and logging
- [ ] Test attribute persistence

**Estimated Duration:** 3-4 hours

---

### Phase 3: Checksum Verification Utility ⏳ PENDING

**Objective:** Create `fsck.hpkgs` tool to audit package integrity

**Deliverables:**
- `src/bin/fsck.hpkgs/fsck_hpkgs.cpp` (main)
- `src/bin/fsck.hpkgs/IntegrityChecker.{h,cpp}`
- Jamfile for tool build

**CLI Interface:**
```bash
fsck.hpkgs [options] [path]

Options:
  -h, --help              Show help
  -j, --json              Output JSON format
  -v, --verbose           Verbose output
  -s, --summary           Only summary report
  --repair                Auto-repair corrupted packages (future)
  --path <dir>            Scan directory (default: /boot/system/packages)
  --threads <n>           Parallel verification threads (default: auto)

Exit codes:
  0 = All packages OK
  1 = Some packages unverified (missing checksum)
  2 = Corruption detected
  3 = I/O or permission error
```

**Output Format (JSON):**
```json
{
  "timestamp": "2026-11-02T22:10:15Z",
  "summary": {
    "total": 150,
    "ok": 148,
    "unverified": 1,
    "corrupted": 1
  },
  "packages": [
    {
      "path": "/boot/system/packages/haiku-0.1-1-x86_64.hpkg",
      "status": "OK",
      "expected": "a1b2c3d4...ef0",
      "actual": "a1b2c3d4...ef0",
      "verified": "2026-11-02T22:05:30Z"
    },
    {
      "path": "/boot/system/packages/curl-8.0-1-x86_64.hpkg",
      "status": "UNVERIFIED",
      "expected": null,
      "actual": null,
      "reason": "No checksum attribute found"
    },
    {
      "path": "/boot/system/packages/bzip2-1.0-1-x86_64.hpkg",
      "status": "CORRUPTED",
      "expected": "f1e2d3c4...5a6",
      "actual": "00000000...000",
      "verified": "2026-10-01T10:00:00Z",
      "action": "Re-download recommended"
    }
  ]
}
```

**Tasks:**
- [ ] Create IntegrityChecker class with VerifyPackage()
- [ ] Implement SHA256 computation (use BPackageKit::GeneralFileChecksumAccessor)
- [ ] Implement attribute read/write logic
- [ ] Create fsck_hpkgs main with argument parsing
- [ ] Implement JSON and text output formatters
- [ ] Add parallel verification with thread pool
- [ ] Test with corrupted files

**Estimated Duration:** 4-5 hours

---

### Phase 4: BFS Attribute Indexing (Optional) ⏳ PENDING

**Objective:** Enable fast queries on integrity status via BFS indices

**Deliverables:**
- `src/bin/fsck.hpkgs/IntegrityIndex.{h,cpp}`

**Capabilities:**
```bash
# Find packages without checksums
query: "name:*.hpkg AND NOT user.hpkg.sha256:*"

# Find packages with failed verification
query: "name:*.hpkg AND user.hpkg.verified:CORRUPT"
```

**Tasks:**
- [ ] Create BFS attribute index schema
- [ ] Implement query builder
- [ ] Add results iterator
- [ ] Performance benchmarking

**Estimated Duration:** 2-3 hours (optional - defer if needed)

---

### Phase 5: Auto-Recovery Framework (Optional) ⏳ PENDING

**Objective:** Framework for automatic package re-download and repair

**Deliverables:**
- Recovery strategy interface in HPKGIntegrityWriter
- Stub implementation in fsck.hpkgs

**Tasks:**
- [ ] Define recovery strategy interface
- [ ] Implement repository connector for re-download
- [ ] Add `--repair` flag to fsck.hpkgs (logs intention only)
- [ ] Test re-download flow

**Estimated Duration:** 2-3 hours (defer until Phase 3 complete)

---

### Phase 6: Hybrid Mode Testing ⏳ PENDING

**Objective:** Verify checksum computation consistency in hybrid rendering mode

**Deliverables:**
- Test report: `docs/HPKG_INTEGRITY_HYBRID_TEST.md`

**Test Procedure:**
1. Build test HPKG in normal mode, compute SHA256
2. Build same HPKG in hybrid mode, compute SHA256
3. Compare → must match exactly
4. Repeat with various HPKG sizes

**Tasks:**
- [ ] Set up test harness with GeneralFileChecksumAccessor
- [ ] Run on both rendering modes
- [ ] Document findings
- [ ] Publish results

**Estimated Duration:** 2-3 hours (can be done in parallel)

---

### Phase 7: Documentation ⏳ PENDING

**Objective:** Complete documentation for users and developers

**Deliverables:**
- `docs/HPKG_INTEGRITY_USER_GUIDE.md` (user guide)
- `docs/HPKG_INTEGRITY_DEVELOPER.md` (developer reference)
- Inline code comments and Doxygen docs

**Coverage:**
- Feature overview and use cases
- Usage examples (fsck.hpkgs)
- Attribute schema and storage format
- Limitations and known issues
- Security model (what this is NOT)
- Troubleshooting

**Tasks:**
- [ ] Write user guide with examples
- [ ] Write developer reference with API docs
- [ ] Add code comments
- [ ] Review and edit

**Estimated Duration:** 2-3 hours

---

## Data Flow & Integration Points

### Installation & Activation Flow

```
1. User requests package installation
   ↓
2. Package Manager (via DownloadFileRequest):
   ├─ Downloads .hpkg from repository
   ├─ Verifies SHA256 from repository metadata
   └─ Marks download as verified (sets FetchUtils::fCompleted flag)
   ↓
3. CommitTransactionHandler::Handle() [INTEGRATION POINT]:
   ├─ Activates package (mounts via packagefs)
   ├─ Calls HPKGIntegrityWriter::WriteChecksum()  ← NEW
   │  ├─ Retrieves verified checksum from cache
   │  ├─ Encodes to hex string
   │  └─ Writes user.hpkg.sha256 attribute
   └─ Completes transaction
   ↓
4. Result:
   ├─ Package activated and mounted
   ├─ Checksum stored in BFS attribute
   └─ Ready for verification
```

### Verification Flow

```
1. User runs: fsck.hpkgs

2. fsck.hpkgs main:
   ├─ Scans /boot/system/packages
   └─ For each .hpkg file:
      ↓
   3. IntegrityChecker::VerifyPackage(path):
      ├─ Read user.hpkg.sha256 attribute
      │  ├─ If missing → status = "UNVERIFIED"
      │  └─ If present → continue
      │
      ├─ Compute current SHA256:
      │  ├─ Use GeneralFileChecksumAccessor
      │  ├─ 64KB chunks for efficiency
      │  └─ Build hex string
      │
      ├─ Compare expected vs actual
      │  ├─ Match → status = "OK", update verified timestamp
      │  └─ Mismatch → status = "CORRUPTED"
      │
      └─ Return VerificationResult
      ↓
   4. Output formatter:
      ├─ Collect all results
      ├─ Compute summary (OK, UNVERIFIED, CORRUPTED)
      └─ Format as JSON or text
      ↓
   5. Output & exit code:
      ├─ Print report
      └─ Exit (0=OK, 1=unverified, 2=corrupted)
```

### Checksum Source: Critical Path

```
Repository DB                DownloadFileRequest
├─ package-name              ├─ GET /packages/.../haiku.hpkg
├─ version                   ├─ GET /packages/.../haiku.hpkg.sha256
├─ sha256: "a1b2c3d4..."     ├─ Compare checksums
└─ url                       ├─ If match: ValidateChecksumJob succeeds
                             └─ fChecksum = "a1b2c3d4..."
                                    ↓
                             CommitTransactionHandler
                                    ↓
                             HPKGIntegrityWriter::WriteChecksum()
                                    ↓
                             BNode::WriteAttr(user.hpkg.sha256, "a1b2c3d4...")
                                    ↓
                             ✅ Checksum persisted
```

**Critical:** Checksum is **NEVER** recalculated after file written. It comes from the repository's verified metadata.

---

## Detailed Implementation Steps

### Step 1.1: Create HPKGIntegrityConstants.h

Location: `src/kits/package/hpkg/HPKGIntegrityConstants.h`

Purpose:
- Central definition of attribute names
- Buffer sizes and format specifications
- Helper functions for encoding/decoding

Key definitions:
```cpp
#define HPKG_ATTR_CHECKSUM "user.hpkg.sha256"
#define HPKG_ATTR_VERIFIED "user.hpkg.verified"

// SHA256: 256 bits = 32 bytes = 64 hex characters
#define HPKG_CHECKSUM_LENGTH_BYTES   32
#define HPKG_CHECKSUM_LENGTH_HEX     64
#define HPKG_CHECKSUM_BUFFER_SIZE    65  // +1 for null terminator

// Timestamp: ISO 8601 format
#define HPKG_TIMESTAMP_BUFFER_SIZE   32

// Algorithm identifier
#define HPKG_CHECKSUM_ALGORITHM "SHA256"
```

### Step 1.2: Update Jamfile

Location: `src/kits/package/hpkg/Jamfile`

Purpose: Ensure new header is compiled with package kit

Action: Add HPKGIntegrityConstants.h to the public headers list

### Step 2.1: Create HPKGIntegrityWriter Class

Location: `src/servers/package/HPKGIntegrityWriter.{h,cpp}`

Interface:
```cpp
class HPKGIntegrityWriter {
public:
    // Write checksum to file's extended attributes
    static status_t WriteChecksum(
        const entry_ref& packageEntry,
        const BString& checksum);

    // Read existing checksum
    static status_t ReadChecksum(
        const entry_ref& packageEntry,
        BString& outChecksum);

    // Verify checksum matches current file
    static status_t VerifyChecksum(
        const entry_ref& packageEntry,
        bool& outMatches);

private:
    // Helper: Convert binary digest to hex string
    static void DigestToHex(
        const uint8* digest,
        size_t digestLen,
        BString& outHex);
};
```

### Step 2.2: Integrate with CommitTransactionHandler

Location: `src/servers/package/CommitTransactionHandler.cpp`

Changes:
```cpp
// In CommitTransactionHandler::Handle() after activation succeeds:

// Before: return B_OK;
// After:

// Write integrity checksum to activated package
if (!fPackage->Checksum().IsEmpty()) {
    status_t checksumStatus = HPKGIntegrityWriter::WriteChecksum(
        fPackage->EntryRef(),
        fPackage->Checksum());
    
    if (checksumStatus != B_OK) {
        LogWarning("Failed to write integrity checksum for %s: %s",
            fPackage->FileName().String(),
            strerror(checksumStatus));
        // Non-blocking: continue despite checksum write failure
    }
}

return B_OK;
```

### Step 3.1: Create IntegrityChecker Utility

Location: `src/bin/fsck.hpkgs/IntegrityChecker.{h,cpp}`

Interface:
```cpp
struct VerificationResult {
    enum Status { OK, UNVERIFIED, CORRUPTED, ERROR };
    
    BString path;
    Status status;
    BString expectedChecksum;
    BString actualChecksum;
    BString lastVerified;
    BString errorMessage;
};

class IntegrityChecker {
public:
    status_t VerifyPackage(
        const BString& packagePath,
        VerificationResult& outResult);

    status_t VerifyDirectory(
        const BString& dirPath,
        BObjectList<VerificationResult>& outResults,
        int32 threadCount = 0);
};
```

### Step 3.2: Create fsck_hpkgs Main

Location: `src/bin/fsck.hpkgs/fsck_hpkgs.cpp`

Purpose: Command-line interface for integrity checking

Features:
- Argument parsing (getopt or custom)
- Directory scanning
- Parallel verification
- Output formatting (JSON/text)
- Exit code handling

### Step 3.3: Add Jamfile for fsck.hpkgs

Location: `src/bin/fsck.hpkgs/Jamfile`

Purpose: Build configuration for tool

Dependencies:
- libpackage
- libbe
- libstdc++

---

## Testing Strategy

### Unit Testing

**Test File:** `src/bin/fsck.hpkgs/IntegrityCheckerTest.cpp`

Tests:
1. WriteChecksum stores hex string correctly
2. ReadChecksum retrieves stored value
3. VerifyPackage detects matching checksums (OK)
4. VerifyPackage detects missing attributes (UNVERIFIED)
5. VerifyPackage detects mismatches (CORRUPTED)
6. Hex encoding/decoding round-trip
7. Error handling for non-existent files
8. Error handling for permission denied

### Integration Testing

**Procedure:**
1. Build test HPKG file
2. Simulate installation:
   - Write to temporary location
   - Call HPKGIntegrityWriter::WriteChecksum()
   - Verify attribute exists
3. Corrupt the file (change one byte)
4. Run fsck.hpkgs
5. Verify it reports CORRUPTED
6. Restore file
7. Run fsck.hpkgs again
8. Verify it reports OK

### Manual Testing

**Scenario 1: Fresh Installation**
```bash
# Install a package normally
pkgman install haiku

# Run verification
fsck.hpkgs

# Expected: All packages OK
```

**Scenario 2: Detect Corruption**
```bash
# Corrupt a package file
hexdump -C /boot/system/packages/curl*.hpkg | head -1
# Edit one byte
# Save

# Run verification
fsck.hpkgs --verbose

# Expected: Report CORRUPTED for curl package
```

**Scenario 3: Hybrid Mode Consistency**
```bash
# Build in normal mode
jam haiku
sha256sum haiku*.hpkg

# Build in hybrid mode
# (switch rendering mode)
jam haiku
sha256sum haiku*.hpkg

# Expected: Checksums identical
```

---

## Security & Reliability Considerations

### What This System IS:
✅ **Data Integrity Checking** - Detect unintended corruption  
✅ **Stability Enhancement** - Prevent silent data loss  
✅ **Operational Monitoring** - Regular audit capability  
✅ **Automatic Detection** - No manual inspection needed  

### What This System IS NOT:
❌ **NOT Security** - User can modify file + attribute together  
❌ **NOT Authentication** - No cryptographic signing  
❌ **NOT Authorization** - No access control enforcement  
❌ **NOT Anti-Tampering** - Will not stop intentional modification  

### Why This Is Appropriate for Haiku
- Haiku's security model is **user-centric, not kernel-enforced**
- Goal is **system stability**, not attack resistance
- Focus on **accidental corruption**, not adversarial threats
- Tradeoff: Simplicity vs perfection

### Performance Implications

**Checksum Write:**
- Location: Package activation (already synchronous operation)
- Added overhead: ~1-5ms per package (negligible)
- Impact: Minimal, non-blocking

**Verification (fsck.hpkgs):**
- Per file: O(file size) - must read entire file
- Parallelizable: Use thread pool for multiple packages
- Typical: 150 packages × 50MB avg = 7.5GB
- With 4 threads: ~30-60 seconds on modern drive
- Can be run in background/cron

**Attribute Queries:**
- BFS attribute indexing: O(1) lookup
- Query performance: Depends on BFS index implementation
- Expected: <100ms for large queries

### Reliability & Error Handling

**Write Failures:**
- Non-blocking: Log warning, continue
- Package remains activated even if attribute write fails
- Next verification will report UNVERIFIED
- User can retry or re-download

**Read Failures:**
- Missing attribute: Treat as UNVERIFIED
- Corrupted attribute: Log error, treat as UNVERIFIED
- No cascading failures

**Computation Errors:**
- SHA256 computation failure: Report ERROR, don't guess
- File access denied: Report ERROR with permission details
- Disk I/O errors: Propagate error, don't mask

---

## References & Dependencies

### Required Haiku APIs

**BPackageKit (libpackage):**
- `GeneralFileChecksumAccessor` - SHA256 computation
- `ValidateChecksumJob` - Verification framework
- `BPackageInfo` - Package metadata
- `BNode`, `BEntry` - File attribute access

**Be API (libbe):**
- `BNode::WriteAttr()` - Attribute persistence
- `BNode::ReadAttr()` - Attribute retrieval
- `BString` - String manipulation
- `BObjectList` - Collection container

**Core utilities:**
- SHA256 implementation (from Be API)
- Standard C++ (threads, filesystem)

### Build System
- Jamfile syntax and conventions
- Existing package daemon build setup
- Tool build configuration

### Related Source Files

**Current Checksums (already exist):**
- `src/kits/package/ChecksumAccessors.{h,cpp}`
- `src/kits/package/ValidateChecksumJob.{h,cpp}`
- `src/kits/package/DownloadFileRequest.cpp`

**Package Daemon:**
- `src/servers/package/CommitTransactionHandler.{h,cpp}`
- `src/servers/package/Package.{h,cpp}`
- `src/servers/package/PackageFile.{h,cpp}`

**Reference Implementations:**
- `src/bin/package/` - Package management tools
- `src/add-ons/kernel/file_systems/packagefs/` - HPKG mounting

---

## Timeline & Milestones

| Phase | Component | Duration | Dependencies | Status |
|-------|-----------|----------|--------------|--------|
| 1 | Schema Definition | 1-2h | None | **IN PROGRESS** |
| 2 | Daemon Integration | 3-4h | Phase 1 | Pending |
| 3 | fsck.hpkgs Tool | 4-5h | Phase 1-2 | Pending |
| 4 | BFS Indexing | 2-3h | Phase 3 | Optional |
| 5 | Auto-Recovery | 2-3h | Phase 3 | Optional |
| 6 | Hybrid Testing | 2-3h | Phase 2-3 | Parallel |
| 7 | Documentation | 2-3h | All phases | Final |

**Total Estimated Time:** 18-27 hours (core phases 1-3: 8-11 hours)

**Realistic Schedule:** 1-2 weeks of part-time development

---

## Next Steps

1. ✅ **Complete Phase 1:** Create `HPKGIntegrityConstants.h`
2. → **Phase 2:** Integrate with package daemon
3. → **Phase 3:** Build fsck.hpkgs tool
4. → **Testing:** Manual verification on test system
5. → **Documentation:** User and developer guides
6. → **Commit:** Merge hpkg_chk → main with release notes

---

## Questions & Discussion

For questions or clarifications:
1. Refer to data flow sections (page references)
2. Check implementation details for specific components
3. Review security considerations for threat model
4. See timeline for scheduling constraints

---

**Document Version:** 1.0  
**Last Modified:** 2026-11-02 22:10  
**Author:** Copilot CLI (plan) / tmtfx (refinement)  
**Status:** Ready for Phase 1 Implementation
