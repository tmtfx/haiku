# HPKG Integrity System - Security Model

## Executive Summary

The HPKG Integrity System is designed to **detect and recover from accidental data corruption**, not to provide cryptographic security or tamper-proof guarantees. It trades simplicity and performance for ease of deployment and integration with Haiku's existing architecture.

**Threat Model Focus**: Accidental corruption (disk errors, incomplete writes, system crashes)  
**Out of Scope**: Malicious tampering, insider threats, advanced persistent threats

---

## Threat Model Analysis

### Threats We Address ✓

#### 1. Disk Corruption
**Scenario**: Bit rot, sector failure, or I/O error corrupts package file silently  
**Without System**: Package silently corrupted, causes system instability  
**With System**: 
- Corruption detected on next scan
- User notified immediately
- Corrupted package automatically recovered from repository
- ✅ Risk: Eliminated

#### 2. Incomplete Downloads
**Scenario**: Network error interrupts package download, file truncated  
**Without System**: Truncated package installed, causes crashes  
**With System**:
- Package daemon verifies checksum before activation (standard protection)
- If somehow activated with wrong checksum, system detects on next scan
- ✅ Risk: Largely mitigated by existing package manager

#### 3. System Crashes During Package Operations
**Scenario**: Power loss or kernel panic during package write  
**Without System**: File partially written, system unbootable  
**With System**:
- Extended attributes preserve checksum of original valid file
- Next boot, system detects corruption and recovers
- ✅ Risk: Mitigated for installation-source packages

#### 4. Silent Data Loss After Installation
**Scenario**: Months after installation, disk error silently corrupts package  
**Without System**: User unaware, package may crash randomly  
**With System**:
- Periodic scans detect corruption
- User alerted before system becomes unstable
- ✅ Risk: Early detection enables prevention

### Threats We Do NOT Address ✗

#### 1. Intentional Tampering by Privileged User
**Scenario**: System administrator or malware modifies both file and extended attribute  
**Our Protection**: None  
**Why**: Haiku runs with user privileges for practical usability. Cryptographic signatures would require key management and would be bypassed anyway with admin access.  
**Mitigation**: Use full-disk encryption (BitLocker/FileVault equivalent) if worried about offline tampering. For online protection, rely on file access controls.

#### 2. Compromised Package Repository
**Scenario**: Repository server compromised, malicious package injected  
**Our Protection**: None  
**Why**: Integrity checking only compares against repository checksum. If repository itself is compromised, checksums are wrong.  
**Mitigation**: Use GPG-signed packages with signature verification (external to this system). Implement supply-chain security measures (code review, reproducible builds, etc.)

#### 3. Network Attack During Download
**Scenario**: Man-in-the-middle intercepts package download, injects malicious version  
**Our Protection**: None  
**Why**: Checksums transmitted alongside package over same network.  
**Mitigation**: Use HTTPS with certificate pinning. Package manager already does this.

#### 4. Extended Attribute Forgery
**Scenario**: Attacker modifies extended attribute to hide corruption  
**Our Protection**: Mismatch still detected  
**Outcome**: If either file or attribute is modified, they won't match → corruption detected  
**Limitation**: We don't know which one is "correct," so we conservatively recover.

#### 5. Malicious or Unsafe Packages
**Scenario**: Valid package file that is intentionally malicious  
**Our Protection**: None  
**Why**: Integrity = correctness of data. Maliciousness is a separate concern.  
**Mitigation**: Use sandboxing, capability-based security, or code review for critical packages.

---

## Extended Attributes: Security Properties

### Property 1: Tamper Detection (Not Tamper Proof)

**Design**: Store checksum in extended attribute on same filesystem

**Strength**:
- Bit-level corruption of file immediately detected
- Bit-level corruption of attribute immediately detected
- Corruption of either one detected (mismatch)

**Weakness**:
- Attacker with write access can modify both file and attribute
- No way to distinguish between legitimate and corrupted version
- No cryptographic proof of correctness

**Practical Impact**:
- ✅ Protects against accidental corruption
- ✗ Does NOT protect against intentional tampering

### Property 2: Availability (Not Authorization)

**Design**: Extended attributes are readable by any user (no access control)

**Rationale**:
- Query API must work for non-root users checking their system status
- BFS extended attributes don't support fine-grained ACLs anyway
- Haiku's threat model assumes user process = single privilege level

**Consequence**: User can read (but not necessarily trust) checksums

### Property 3: Immutability (For Repository Packages Only)

**Design**: Checksums come from repository at download time, never recalculated

**Strength**:
- Package daemon verifies checksum during download
- If package is valid then, checksum stored at activation time
- Any later mismatch = corruption (not legitimate update)

**Limitation**:
- Applies ONLY to repository-sourced packages
- Manually installed packages: no checksum verification guarantee
- Network/USB packages: source may be untrustworthy

**Mitigation**: Manual recovery requires user confirmation; user selects file to use as recovery source.

---

## Attribute Namespace: `integrity.*` vs `user.*`

### Why Not `user.*`?

Early design used `user.hpkg.*` namespace (standard user extended attributes). Changed to `integrity.*` for these reasons:

1. **Semantic Clarity**: `user.` suggests per-user metadata, but this is system-level integrity data
2. **Future Flexibility**: Namespace `integrity.*` allows for other integrity features (e.g., `integrity.atime_modified` for tracking last modification time)
3. **Discoverability**: Clearer intent when listing attributes

### Security Implication: None

Both namespaces have identical access controls on BFS. The namespace name alone provides no security.

---

## Checksum Source: Critical Design Decision

### Rule: NEVER Recalculate Checksums After File Write

**Problem**: If we recalculate SHA256 after writing package to disk, we might "verify" an already-corrupted file.

**Example Attack Scenario**:
1. Attacker causes filesystem corruption
2. Package file becomes corrupted at bit level
3. System recalculates and writes corrupted checksum to extended attribute
4. Future scans find file and checksum match → Corruption hidden indefinitely

**Solution**: Checksum ALWAYS comes from repository download verification.

**Flow**:
```
Package Download:
  [1] Download file from repository
  [2] Verify SHA256 matches repository's advertised checksum ✓ VERIFIED HERE
  [3] Activate into /boot/system/packages/
  [4] Package daemon writes SHA256 to extended attribute
  
Later Verification:
  [1] Read checksum from extended attribute
  [2] Compute SHA256 of file
  [3] Compare: Match = verified, Mismatch = corrupted
  [4] No recalculation, no verification of the checksum source
```

**Implication**: System is only as trustworthy as the repository's integrity. If repository is compromised, game over.

---

## Recovery Strategy: Hybrid Model

### Recovery by Source Type

| Source | Corruption Detected | Recovery Action | User Interaction |
|--------|-------------------|-----------------|-------------------|
| **repository** | Automatic scan | Auto-download & activate | None (automatic) |
| **unknown** | Automatic scan | File dialog to select recovery file | Required (user selects) |
| **local** | Automatic scan | Attempt backup recovery | None if backup exists, dialog otherwise |
| **network** | Automatic scan | Manual (guide user to source) | Required (user re-downloads) |

### Safety Rule: Never Direct File Replacement

**Enforcement**: All recovery operations go through package daemon's normal activation mechanism.

**Why**:
- Ensures proper dependency tracking
- Ensures proper version tracking
- Ensures proper permission handling
- Enables rollback if recovery fails

**Never Do**:
```bash
# DON'T: Direct file copy
cp /tmp/backup.hpkg /boot/system/packages/broken.hpkg
```

**Always Do**:
```bash
# DO: Let package daemon handle it
pkgman install /tmp/backup.hpkg
# or
fsck_hpkgs repair --package broken.hpkg  # Uses daemon internally
```

---

## Dependency Safety

### Rule: Don't Deactivate Packages With Dependents

**Risk**: Removing a package that other packages depend on can render system unbootable.

**Example**: 
- Package A (libc) is corrupted
- Package B (bash) depends on Package A
- If we deactivate Package A without deactivating B first → crash

**Solution**: Recovery coordinator checks dependency graph before deactivation.

```cpp
// Pseudo-code
before_recovering(package_A) {
    dependents = get_packages_that_depend_on(package_A);
    if (dependents.count > 0) {
        log_error("Cannot recover %s: %s depends on it",
                  package_A.name, dependents[0].name);
        return B_ERROR;  // Don't attempt recovery
    }
    proceed_with_recovery();
}
```

---

## Audit Trail & Logging

### What Gets Logged

**All recovery attempts are logged to**: `/var/log/hpkg_recovery.log`

```
[2026-11-02 22:10:15] RepositoryRecovery: Starting recovery for curl-8.5.0-1-x86_64.hpkg
[2026-11-02 22:10:15] RepositoryRecovery: Extracted package name: curl
[2026-11-02 22:10:16] RepositoryRecovery: Downloaded fresh copy (2.4 MB)
[2026-11-02 22:10:17] RepositoryRecovery: Verified checksum: MATCH
[2026-11-02 22:10:17] RepositoryRecovery: Backed up original to: /var/backups/curl-8.5.0-1-x86_64.hpkg.corrupted
[2026-11-02 22:10:18] RepositoryRecovery: Replaced file
[2026-11-02 22:10:19] RepositoryRecovery: Re-activated via package daemon: SUCCESS
[2026-11-02 22:10:19] RepositoryRecovery: Recovery completed successfully
```

### Log Retention

- Log file: `/var/log/hpkg_recovery.log`
- Rotation: Manual or via standard logrotate
- Retention: Indefinite (users should rotate)

### Log Access

- Readable by: All users (informational)
- Writable by: Recovery daemon (package_daemon)
- Cannot be used for: Proving package authenticity (could be forged by privileged user)

---

## Notification Security

### Desktop Notifications

When integrity system makes automated decisions (auto-repair), it notifies user via:
- Desktop notification popup
- Log entry in `/var/log/hpkg_recovery.log`

**Notification Content**:
```
Package Repaired: curl-8.5.0-1-x86_64.hpkg
Status: Successfully recovered from repository
Action: Automatic repair (recovery source: repository)
Timestamp: 2026-11-02 22:10:19
```

### Notification Security Properties

- ✅ Provides transparency (user sees what system did)
- ✗ Can be forged by compromised system (system itself could send fake notifications)
- ✗ Cannot be used as proof of correctness

**Purpose**: Transparency to help user understand system behavior, not cryptographic proof.

---

## Threat Scenarios: Detailed Analysis

### Scenario 1: Accidental Disk Corruption

**Setup**: Running system for 2 years, kernel randomly writes wrong data to disk sector

**Without System**:
- Package file silently corrupted
- Application using corrupted library crashes randomly
- User frustrated, debugging takes weeks

**With System**:
- Next scheduled scan detects mismatch
- User notified immediately
- Package automatically recovered from repository
- System stable within minutes
- **Outcome**: ✅ Risk eliminated

**Security Impact**: **LOW RISK** (accidental corruption is common, this prevents it)

### Scenario 2: Compromised Local Admin Account

**Setup**: Attacker gains admin access (e.g., rootkit installed via security breach)

**Attack**: Attacker modifies curl binary to include backdoor

**Without System**:
- Backdoor silently installed
- System compromised, attacker has access

**With System**:
- Attacker can modify curl binary (has admin access)
- Attacker can also modify extended attribute (has admin access)
- Next scan sees matching checksum → "verified" ✗
- Backdoor installed undetected

**Countermeasure**: System can't protect against compromised admin. If admin is compromised, game over. Use:
- Secure boot with code signing
- Full-disk encryption
- SELinux or AppArmor for privilege separation
- IDS to detect unauthorized changes

**Outcome**: ✗ System doesn't help here (expected)

### Scenario 3: Network Attack During Repository Sync

**Setup**: Attacker on same LAN, can intercept HTTP traffic

**Attack**: MITM injects malicious curl package during download

**Without System**:
- Package manager verifies checksum against intercepted fake checksum
- Malicious package installed
- System compromised

**With System**:
- Same outcome if repository is also compromised
- If repository is legitimate but network compromised: package manager's HTTPS should catch it
- This system doesn't add protection (external to its scope)

**Outcome**: HTTPS/certificate pinning handles this (package manager's job)

### Scenario 4: Extended Attribute Corruption

**Setup**: Filesystem bug corrupts extended attribute for curl package

**Corruption**: SHA256 attribute becomes garbage (random bytes)

**Without System**:
- System doesn't know package is corrupted
- Continues to use it

**With System**:
- Verification reads garbage attribute
- Tries to parse as hex, fails → treats as unverified
- Next repair prompts user
- User recovers from repository
- **Outcome**: ✅ Detected and user guided to recovery

### Scenario 5: Copyable Backup Gets Old

**Setup**: User backs up system to USB drive, uses backup 2 years later

**Issue**: Backup's extended attributes are from backup time, don't match current date

**With System**:
- Backup restored, packages show as "verified" with old timestamps
- User runs scan, sees corruption (attribute timestamp is 2 years old)
- User is confused

**Note**: This is NOT corruption, just stale metadata. System should handle gracefully.

**Solution**: On full system restore, scan without corruption = expected. Timestamps outdated = expected. Add documentation.

---

## Compliance & Standards

### Does This System Comply With...?

#### Cryptographic Standards
**No**. This is not a cryptographic security system. It uses SHA256 (which is cryptographically secure) but the system architecture doesn't provide cryptographic integrity guarantees.

#### FIPS 140
**No**. System doesn't meet FIPS 140 requirements (no keymanagement, no formal validation, etc.)

#### Common Criteria
**No**. System is not evaluated against Common Criteria.

#### Haiku Security Model
**Yes**. Assumes Haiku's existing privilege model (user = trusted, admin = full control).

---

## Recommendations for Higher Security

If you need stronger guarantees, consider layering:

### Layer 1: This System (Accidental Corruption)
✅ Detects bit-level corruption  
✓ Auto-recovers repository packages  
✗ Doesn't protect against tampering

### Layer 2: Package Signing (Authenticity)
Add: GPG-signed package manifests  
Effect: Ensures packages come from trusted repository  
Protects against: Repository compromise, MITM attacks

### Layer 3: Code Review (Maliciousness)
Add: Peer review of package contents before installation  
Effect: Ensures packages don't contain malware  
Protects against: Malicious package authors

### Layer 4: Sandboxing (Damage Control)
Add: Run applications in restricted sandbox (AppArmor, pledge)  
Effect: Limits damage if package is malicious  
Protects against: Malicious packages escalating privileges

### Layer 5: Full-Disk Encryption (Physical Security)
Add: Encrypt filesystem with BitLocker or similar  
Effect: Protects against offline attacks  
Protects against: Attacker with physical access

---

## Security Testing

### Test 1: Detect Intentional File Corruption

```bash
# Create test package
cp /boot/system/packages/curl-8.5.0-1-x86_64.hpkg /tmp/test.hpkg

# Corrupt one byte
dd if=/dev/zero bs=1 count=1 seek=1000 of=/tmp/test.hpkg conv=notrunc

# Verify detects corruption
fsck_hpkgs query /tmp/test.hpkg
# Expected: ✗ CORRUPTED
```

### Test 2: Detect Attribute Tampering

```bash
# Modify extended attribute to random value
addattr -t string integrity.hpkg.sha256 \
  "0000000000000000000000000000000000000000000000000000000000000000" \
  /boot/system/packages/curl-8.5.0-1-x86_64.hpkg

# Verify detects mismatch
fsck_hpkgs query /boot/system/packages/curl-8.5.0-1-x86_64.hpkg
# Expected: ✗ CORRUPTED (attribute doesn't match actual checksum)
```

### Test 3: Attribute Doesn't Protect Against Tampering

```bash
# Corrupt file and update attribute to match
dd if=/dev/zero bs=1 count=1 seek=1000 of=/boot/system/packages/test.hpkg conv=notrunc
CHECKSUM=$(sha256sum /boot/system/packages/test.hpkg | cut -d' ' -f1)
addattr -t string integrity.hpkg.sha256 "$CHECKSUM" /boot/system/packages/test.hpkg

# Verify doesn't detect (expected: tampering by admin undetectable)
fsck_hpkgs scan /boot/system/packages/
# Expected: ✓ VERIFIED (system can't tell this was intentional tampering)
```

---

## Lessons Learned

### What We Got Right

1. **Simple & Deployable**: Minimal dependencies, works without kernel changes
2. **Immutable-Package Assumption**: Correctly matches Haiku's package design (packages never update in-place)
3. **Hybrid Recovery**: Balances automation (repository packages) with safety (manual packages)
4. **Audit Trail**: All actions logged, providing transparency

### What We Didn't Attempt

1. **Cryptographic Security**: System is not designed to protect against intentional tampering
2. **Supply Chain Security**: Assumes repository is trustworthy (external problem)
3. **Malware Detection**: Checksums verify correctness, not safety
4. **Privilege Escalation Prevention**: Relies on existing OS security model

### When This System Helps

✅ Disk corruption  
✅ Incomplete downloads  
✅ System crashes during updates  
✅ Silent data loss over time  
✅ Accidental deletion / loss  

### When This System Doesn't Help

✗ Intentional tampering (by admin or malware)  
✗ Compromised package repository  
✗ Malicious packages  
✗ Network attacks with HTTPS bypass  
✗ Cryptographic signature verification  

---

## Future Enhancements

### Possible Security Improvements

1. **GPG Package Signing**
   - Add optional signature verification
   - Block installation of unsigned packages
   - Detect repository compromise

2. **Cryptographic Notary Service**
   - Maintain separate signed notary of all package checksums
   - Detect if repository swaps checksums
   - Requires external infrastructure

3. **Blockchain Ledger** (Speculative)
   - Immutable log of all package checksums
   - Detect retroactive corruption cover-ups
   - Overkill for Haiku's threat model

4. **Trusted Boot Integration**
   - Verify packages at kernel load time
   - Detect tampering before user-space runs
   - Requires secure boot hardware support

---

## References

- **BFS Extended Attributes**: Haiku API documentation, `Node.h`
- **BCrypto SHA256**: Haiku Kit documentation, `BMessageDigest`
- **Package Daemon**: Source: `src/servers/package/`
- **Security Design**: This document (canonical source)

---

**Last Updated**: November 2026  
**Project**: Pirati del Frico HPKG Integrity System  
**Repository**: tmtfx/haiku (hpkg_chk branch)  
**Security Review**: By system design (not formally audited)
