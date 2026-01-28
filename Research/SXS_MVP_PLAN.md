# WSL Side-by-Side (SxS) MVP Plan

**Date:** 2026-01-28
**Goal:** Enable fork to run concurrently with canonical WSL without conflicts

---

## 1. Identity Fork Map (Concrete Values)

| # | Category | Canonical Value | Fork Value | Type |
|---|----------|-----------------|------------|------|
| 1 | Service Name | `WSLService` | `WSLXService` | Windows Service |
| 2 | COM CLSID | `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | COM Class |
| 3 | COM AppID | `{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | COM App |
| 4 | COM Interface IID | `{38541BDC-F54F-4CEB-85D0-37F0F3D2617E}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | COM Interface |
| 5 | COM ProxyStub CLSID | `{4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | COM ProxyStub |
| 6 | Registry Root | `Lxss` | `WslX` | Registry |
| 7 | HNS Network GUID (legacy) | `{b95d0c5e-57d4-412b-b571-18a81a16e005}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | HNS Network |
| 8 | HNS Network GUID (firewall) | `{790e58b4-7939-4434-9358-89ae7ddbe87e}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | HNS Network |
| 9 | Network Names | `"WSL"`, `"WSL (Hyper-V firewall)"` | `"WSLX"`, `"WSLX (Hyper-V firewall)"` | HNS Network |
| 10 | VM Owner | `"WSL"` | `"WSLX"` | HCS |
| 11 | MSI UpgradeCode | `{6D5B792B-1EDC-4DE9-8EAD-201B820F8E82}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | MSI |
| 12 | MSI Product Name | `"Windows Subsystem for Linux"` | `"WSLX"` | MSI |
| 13 | Install Directory | `C:\Program Files\WSL` | `C:\Program Files\WSLX` | Filesystem |
| 14 | Shell Folder CLSID | `{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}` | `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (generate new) | Explorer |
| 15 | Shell Display Name | `"Linux"` | `"Linux (WSLX)"` | Explorer |
| 16 | MSIX Identity | `MicrosoftCorporationII.WindowsSubsystemForLinux` | `(Custom Publisher).WSLX` | MSIX |
| 17 | Binary Names | `wsl.exe`, `wslservice.exe`, etc. | `wslx.exe`, `wslxservice.exe`, etc. | Binaries |
| 18 | HvSocket Ports | 50000-50005 | 51000-51005 | HvSocket |

---

## 2. Files to Modify

### 2.1 Central Identity Header (Create New)

**File:** `src/shared/inc/fork_identity.h`

```cpp
#pragma once

// ============================================================================
// WSLX Fork Identity Constants
// ============================================================================
// Change these values to create an independent SxS installation.
// All values must be changed together for a consistent fork.

// Service Identity
#define WSLX_SERVICE_NAME L"WSLXService"
#define WSLX_SERVICE_DISPLAY_NAME L"WSLX Service"

// COM Identity (generate new GUIDs for your fork)
// CLSID for LxssUserSession
#define WSLX_CLSID_LXSS_USER_SESSION {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}

// AppID for service hosting
#define WSLX_APPID {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}

// ILxssUserSession interface IID
#define WSLX_IID_ILXSS_USER_SESSION {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}

// ProxyStub CLSID
#define WSLX_CLSID_PROXY_STUB {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}

// Registry
#define WSLX_REGISTRY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\WslX"

// HNS Network Identity
#define WSLX_NAT_NETWORK_ID {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}
#define WSLX_NAT_NETWORK_FIREWALL_ID {0x????????, 0x????, 0x????, {0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??, 0x??}}
#define WSLX_NAT_NETWORK_NAME L"WSLX"
#define WSLX_NAT_NETWORK_FIREWALL_NAME L"WSLX (Hyper-V firewall)"

// HCS Identity
#define WSLX_VM_OWNER L"WSLX"

// HvSocket Ports (offset by 1000 from canonical)
#define WSLX_INIT_PORT (51000)
#define WSLX_PLAN9_PORT (51001)
#define WSLX_PLAN9_DRVFS_PORT (51002)
#define WSLX_PLAN9_DRVFS_ADMIN_PORT (51003)
#define WSLX_VIRTIOFS_PORT (51004)
#define WSLX_CRASH_DUMP_PORT (51005)
```

### 2.2 Source Files to Modify

| File | Changes Required |
|------|------------------|
| `src/windows/service/exe/ServiceMain.cpp:50` | Replace `L"WslService"` with `WSLX_SERVICE_NAME` |
| `src/windows/service/inc/wslservice.idl:157-162` | Replace CLSIDs with fork values |
| `src/windows/service/inc/wslservice.idl:165` | Replace interface IID |
| `src/windows/service/inc/wslservice.idl:145` | Replace `LXSS_REGISTRY_PATH` |
| `src/windows/common/WslCoreConfig.cpp:498-509` | Replace network GUIDs and names |
| `src/windows/common/wslutil.h:47` | Replace `c_vmOwner` with `WSLX_VM_OWNER` |
| `src/shared/inc/lxinitshared.h:115-120` | Replace port numbers |

### 2.3 Build System Files to Modify

| File | Changes Required |
|------|------------------|
| `msipackage/package.wix.in:2` | New UpgradeCode |
| `msipackage/package.wix.in:2` | New Package Name |
| `msipackage/package.wix.in:7` | Change `INSTALLDIR` to `WSLX` |
| `msipackage/package.wix.in:127-155` | Update all COM registry keys |
| `msipackage/package.wix.in:218` | Change ServiceInstall Name |
| `msipackage/package.wix.in:53-102` | Update Explorer integration CLSIDs |
| `msixinstaller/AppxManifest.in:15` | New Identity Name/Publisher |
| `msixgluepackage/AppxManifest.in:11` | New Identity Name/Publisher |
| `CMakeLists.txt` | Add define for fork mode |

---

## 3. Implementation Steps

### Phase 1: Create Fork Identity Header

1. Create `src/shared/inc/fork_identity.h` with all constants
2. Generate new GUIDs using `uuidgen` or PowerShell `[guid]::NewGuid()`
3. Add `#include "fork_identity.h"` to affected files

### Phase 2: Update Source Code

1. **Service Name:**
   - Edit `src/windows/service/exe/ServiceMain.cpp:50`
   - Replace hardcoded string with header constant

2. **COM Identities:**
   - Edit `src/windows/service/inc/wslservice.idl`
   - Update all GUID definitions
   - Rebuild to regenerate proxy/stub

3. **Registry Path:**
   - Edit `src/windows/service/inc/wslservice.idl:145`
   - Search codebase for other `Lxss` references

4. **HNS Network:**
   - Edit `src/windows/common/WslCoreConfig.cpp:495-511`
   - Replace both GUIDs and names

5. **VM Owner:**
   - Edit `src/windows/common/wslutil.h:47`

6. **HvSocket Ports:**
   - Edit `src/shared/inc/lxinitshared.h:115-120`

### Phase 3: Update MSI Package

1. Generate new GUIDs for all WiX identities
2. Edit `msipackage/package.wix.in`:
   - UpgradeCode (line 2)
   - Package Name (line 2)
   - INSTALLDIR (line 7)
   - All registry keys (lines 44, 53-155, 218)
3. Rename output binary names in WiX

### Phase 4: Update MSIX Packages

1. Edit `msixinstaller/AppxManifest.in`:
   - Identity Name (line 15)
   - Create new Publisher certificate
   - Update execution aliases if renaming binaries

2. Edit `msixgluepackage/AppxManifest.in`:
   - Same Identity changes

### Phase 5: Binary Renaming (Optional but Recommended)

1. Rename executables: `wsl.exe` → `wslx.exe`, etc.
2. Update all internal references
3. Update WiX file references

---

## 4. Acceptance Tests

### Test 1: Independent Installation
```powershell
# Precondition: Canonical WSL installed
wsl --version  # Should work

# Install fork MSI
msiexec /i bin\x64\debug\wslx.msi

# Verify both are installed
sc query WSLService   # Canonical - should be RUNNING
sc query WSLXService  # Fork - should be RUNNING

# PASS: Both services coexist
```

### Test 2: Independent VM Boot
```powershell
# Boot canonical WSL
wsl -d Ubuntu -e /bin/true

# Verify canonical VM
hcsdiag list | Select-String "WSL"  # Owner=WSL

# Boot fork (assumes wslx.exe or service renamed)
wslx -d Ubuntu -e /bin/true  # Or via COM directly

# Verify fork VM
hcsdiag list | Select-String "WSLX"  # Owner=WSLX

# PASS: Two VMs with different owners
```

### Test 3: Independent HNS Networks
```powershell
# Before any WSL
hnsdiag list networks > before.txt

# Start canonical WSL
wsl -d Ubuntu -e /bin/true
hnsdiag list networks | Select-String "WSL"  # Name="WSL" or "WSL (Hyper-V firewall)"

# Start fork
wslx -d Ubuntu -e /bin/true
hnsdiag list networks | Select-String "WSLX"  # Name="WSLX" or "WSLX (Hyper-V firewall)"

# PASS: Two distinct HNS networks
```

### Test 4: Independent Registry
```powershell
# Canonical distros
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss"

# Fork distros
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\WslX"

# PASS: Separate registry trees
```

### Test 5: Independent COM Activation
```powershell
# Verify both CLSIDs registered
reg query "HKCR\CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}"  # Canonical
reg query "HKCR\CLSID\{<FORK_CLSID>}"  # Fork

# Verify different AppIDs
reg query "HKCR\AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}" /v LocalService  # WSLService
reg query "HKCR\AppID\{<FORK_APPID>}" /v LocalService  # WSLXService

# PASS: Independent COM registration
```

### Test 6: Concurrent Execution
```powershell
# Start bash in canonical
Start-Process wsl -ArgumentList "-d Ubuntu -e bash -c 'sleep 60'"

# Start bash in fork
Start-Process wslx -ArgumentList "-d Ubuntu -e bash -c 'sleep 60'"

# Verify both processes running
Get-Process | Where-Object { $_.Name -match "vmmem" }
# Should see: vmmem, vmmem (or vmmem WSL, vmmem WSLX if suffix supported)

# PASS: Both running concurrently
```

### Test 7: No Cross-Interference
```powershell
# Shutdown canonical
wsl --shutdown

# Verify fork still running
wslx --list --running  # Should show running distro

# Shutdown fork
wslx --shutdown

# Verify canonical still works
wsl -d Ubuntu -e /bin/true  # Should work

# PASS: Independent lifecycle
```

---

## 5. Collision Matrix

| Scenario | Canonical WSL | Fork (WSLX) | Result | Mitigation |
|----------|---------------|-------------|--------|------------|
| Both services running | WSLService | WSLXService | **OK** | Different names |
| Both create NAT network | `{b95d0c5e-...}` | `{fork-guid}` | **OK** | Different GUIDs |
| Both create VM | Owner=WSL | Owner=WSLX | **OK (UNCONFIRMED)** | Different owners; needs runtime proof |
| Both port-forward :8080 | Binds port | Conflict | **CONFLICT** | User responsibility |
| Both access `\\wsl.localhost` | Works | **Cannot access** | **LIMITATION** | Fork uses SMB/VirtioFS (see Section 6) |
| Both upgrade/install | MSI UpgradeCode | Fork UpgradeCode | **OK** | Independent packages |
| Both register distros | `Lxss` registry | `WslX` registry | **OK** | Separate registry |

**Note on `\\wsl.localhost`:** P9rdr.sys/P9np.dll are closed-source and hardcode the canonical CLSID. Fork distros registered under `WslX` registry will NOT appear in `\\wsl.localhost`. This is UNCONFIRMED pending runtime verification (see `UNC_RUNTIME_PROOF.md`).

---

## 6. Known Limitations

### Cannot Change (Closed-Source Dependencies)
1. `\\wsl.localhost` UNC namespace - tied to P9rdr.sys
2. `\\wsl$` UNC namespace - tied to P9rdr.sys
3. WSL1 functionality - requires Lxcore.sys

### Workarounds
1. Fork can expose files via SMB share from guest
2. Fork can use VirtioFS for host-guest file sharing
3. Fork shell folder can be named differently (e.g., "Linux (WSLX)")

---

## 7. MSI-Only MVP v0 (No MSIX)

### 7.1 Rationale

For MVP v0, we explicitly exclude MSIX packaging to:
- Avoid certificate signing complexity
- Avoid package family name collisions
- Avoid execution alias conflicts (`bash.exe`, `wsl.exe`, `wslconfig.exe`)
- Reduce identity surfaces to manage

### 7.2 Glue MSIX Analysis

**What the glue MSIX provides** (`msixgluepackage/AppxManifest.in`):

| Feature | WiX Line | Impact if Skipped |
|---------|----------|-------------------|
| Execution alias: `bash.exe` | AppxManifest.in:47 | Users invoke `wslx.exe` directly |
| Execution alias: `wsl.exe` | AppxManifest.in:48 | No conflict with canonical |
| Execution alias: `wslconfig.exe` | AppxManifest.in:49 | Users invoke `wslxconfig.exe` directly |
| COM AppExtension entry point | AppxManifest.in:52-63 | MSI registers COM separately |

**Conclusion:** Glue MSIX provides convenience aliases only. MSI handles all critical COM registration.

### 7.3 How to Disable Glue MSIX Installation

**Option A: Use SKIPMSIX property (recommended)**

```powershell
# Install fork MSI without glue MSIX
msiexec /i wslx.msi SKIPMSIX=1
```

**Evidence:** WiX conditions check `SKIPMSIX` property:
```xml
<!-- package.wix.in:521-523 -->
<Custom Action="InstallMsix.SetProperty" ... Condition='... and (not SKIPMSIX = 1)' />
<Custom Action="InstallMsix" ... Condition='... and (not SKIPMSIX = 1)' />
<Custom Action="InstallMsixAsUser" ... Condition='... and (not SKIPMSIX = 1)' />
```

**Option B: Remove from WiX template (permanent)**

Edit `msipackage/package.wix.in`:

```xml
<!-- REMOVE or comment out these lines -->
<!-- Line 383: Remove embedded MSIX binary -->
<!-- <Binary Id="msixpackage" SourceFile="${PACKAGE_INPUT_DIR}/gluepackage.msix"/> -->

<!-- Lines 425-439: Remove or condition custom actions -->
<!-- Comment out InstallMsix, InstallMsixAsUser custom actions -->

<!-- Lines 521-523: Remove scheduling -->
<!-- Comment out InstallMsix sequence entries -->
```

### 7.4 WiX Custom Actions for MSIX

**CRITICAL WARNING:** The "remove/deprovision" actions target the canonical `MicrosoftCorporationII.WindowsSubsystemForLinux` MSIX. Running these in SxS mode **will break canonical WSL**.

| Custom Action | Line | Purpose | MVP v0 Action |
|---------------|------|---------|---------------|
| `InstallMsix` | 425-431 | Install glue MSIX as SYSTEM | **Skip** (SKIPMSIX=1) |
| `InstallMsixAsUser` | 433-439 | Register for current user | **Skip** (SKIPMSIX=1) |
| `InstallMsix.SetProperty` | 481 | Set database path | **Skip** (SKIPMSIX=1) |
| `DeprovisionMsix` | 401-407 | Remove canonical MSIX | **DISABLE** (breaks canonical!) |
| `RemoveMsixAsSystem` | 409-415 | Remove canonical MSIX | **DISABLE** (breaks canonical!) |
| `RemoveMsixAsUser` | 417-423 | Unregister canonical MSIX | **DISABLE** (breaks canonical!) |
| `CleanMsixState` | 449-455 | Clean shared state | **DISABLE** (may affect canonical) |

### 7.5 Required WiX Modifications for SxS Safety

**Edit `msipackage/package.wix.in`:**

Add a new property for SxS mode:
```xml
<!-- Add near line 542 -->
<Property Id="WSLX_SXS_MODE" Value="1" Secure="yes" />
```

Condition ALL MSIX actions to skip in SxS mode:
```xml
<!-- Line 500: Add SxS condition -->
<Custom Action="DeprovisionMsix" After="BindImage"
  Condition='((not INSTALLED) or (REMOVE~="ALL")) and (not UPGRADINGPRODUCTCODE) and (not SKIPMSIX = 1) and (not WSLX_SXS_MODE = 1)'/>

<!-- Line 503-504: Add SxS condition -->
<Custom Action="RemoveMsixAsUser" After="DeprovisionMsix"
  Condition='((not INSTALLED) or (REMOVE~="ALL")) and (not UPGRADINGPRODUCTCODE) and (not SKIPMSIX = 1) and (not WSLX_SXS_MODE = 1)'/>
<Custom Action="RemoveMsixAsSystem" After="RemoveMsixAsUser"
  Condition='((not INSTALLED) or (REMOVE~="ALL")) and (not UPGRADINGPRODUCTCODE) and (not SKIPMSIX = 1) and (not WSLX_SXS_MODE = 1)'/>

<!-- Line 507: Add SxS condition -->
<Custom Action="CleanMsixState" After="RemoveMsixAsSystem"
  Condition='((not INSTALLED) or (not REMOVE~="ALL")) and (not UPGRADINGPRODUCTCODE) and (not WSLX_SXS_MODE = 1)'/>
```

**Alternative: Hardcode SxS mode** (simpler for fork)

In fork's `package.wix.in`, simply remove or comment out lines 500-507 entirely:
```xml
<!-- REMOVED FOR SXS FORK - DO NOT TOUCH CANONICAL MSIX
<Custom Action="DeprovisionMsix" ... />
<Custom Action="RemoveMsixAsUser" ... />
<Custom Action="RemoveMsixAsSystem" ... />
<Custom Action="CleanMsixState" ... />
-->
```

### 7.6 SxS-Safe Install Command

```powershell
# MVP v0: Skip all MSIX operations
msiexec /i wslx.msi SKIPMSIX=1 WSLX_SXS_MODE=1 /qn /l*v wslx_install.log
```

### 7.5 Verification Procedure

**Before installation:**
```powershell
# Capture current AppX packages
$before = Get-AppxPackage *WindowsSubsystemForLinux* | Select-Object Name, Version, PackageFullName
$before | Export-Csv -Path "appx_before.csv" -NoTypeInformation
Write-Host "Packages before:" -ForegroundColor Cyan
$before | Format-Table
```

**Install fork MSI:**
```powershell
msiexec /i "C:\path\to\wslx.msi" SKIPMSIX=1 /qn /l*v wslx_install.log
```

**After installation:**
```powershell
# Capture AppX packages after
$after = Get-AppxPackage *WindowsSubsystemForLinux* | Select-Object Name, Version, PackageFullName
$after | Export-Csv -Path "appx_after.csv" -NoTypeInformation
Write-Host "Packages after:" -ForegroundColor Cyan
$after | Format-Table

# Diff check
$newPackages = Compare-Object $before $after -Property PackageFullName -PassThru |
    Where-Object { $_.SideIndicator -eq '=>' }

if ($newPackages) {
    Write-Host "FAIL: New AppX packages installed:" -ForegroundColor Red
    $newPackages | Format-Table
} else {
    Write-Host "PASS: No new AppX packages installed" -ForegroundColor Green
}
```

**Expected output:**
```
Packages before:
Name                                           Version        PackageFullName
----                                           -------        ---------------
MicrosoftCorporationII.WindowsSubsystemForLinux 2.x.x.x       MicrosoftCorporationII...

Packages after:
Name                                           Version        PackageFullName
----                                           -------        ---------------
MicrosoftCorporationII.WindowsSubsystemForLinux 2.x.x.x       MicrosoftCorporationII...

PASS: No new AppX packages installed
```

---

## 8. SxS MVP v0 Checklist (MSI-Only)

### Phase 1: Code Changes

| # | Task | Status | Evidence |
|---|------|--------|----------|
| 1 | Generate new GUIDs (8 total) | [ ] | GUIDs in `fork_identity.h` |
| 2 | Create `fork_identity.h` | [ ] | File exists |
| 3 | Update `ServiceMain.cpp` service name | [ ] | Code diff |
| 4 | Update `wslservice.idl` COM identities | [ ] | Code diff |
| 5 | Update `WslCoreConfig.cpp` HNS network IDs | [ ] | Code diff |
| 6 | Update `wslutil.h` VM owner | [ ] | Code diff |
| 7 | Update `wslutil.cpp` named objects | [ ] | Mutex/pipe names |
| 8 | Update `lxinitshared.h` HvSocket ports | [ ] | Code diff |

### Phase 2: Build System Changes

| # | Task | Status | Evidence |
|---|------|--------|----------|
| 9 | Update `package.wix.in` identities | [ ] | WiX diff |
| 10 | **Disable MSIX cleanup actions** (Section 7.5) | [ ] | WiX diff shows lines 500-507 conditioned/removed |
| 11 | **Build forked client** (`wslx.exe`) | [ ] | Binary exists |
| 12 | ~~Update MSIX manifests~~ | N/A | **Skipped for MVP v0** |

### Phase 3: Build & Install

| # | Task | Status | Evidence |
|---|------|--------|----------|
| 13 | Build MSI: `cmake --build . --target msi` | [ ] | MSI file exists |
| 14 | Install: `msiexec /i wslx.msi SKIPMSIX=1 WSLX_SXS_MODE=1` | [ ] | Install log |

### Phase 4: Verification Gates

| # | Gate | Status | Evidence |
|---|------|--------|----------|
| 15 | **Gate A: Canonical still works** | [ ] | `wsl --status`, `wsl -d Ubuntu -e /bin/true` |
| 16 | **Gate B: Two services, two CLSIDs** | [ ] | `sc query`, `reg query HKCR\CLSID\{...}` |
| 17 | **Gate C: Concurrent VMs** | [ ] | `hcsdiag list`, `hnsdiag list networks` |
| 18 | Verify no new AppX packages | [ ] | Before/after CSV |
| 19 | Run acceptance tests 1-7 | [ ] | Test outputs |
| 20 | Document issues | [ ] | Issue list |

### Critical: Forked Client Requirement

The fork **must** include `wslx.exe` (or equivalent) because:
- COM IID change makes canonical `wsl.exe` incompatible with fork service
- MSIX execution aliases are skipped in MVP v0
- Users need a way to invoke the fork

**Options:**
1. **Rename binary in CMakeLists.txt** (recommended)
2. Create wrapper script that activates fork COM object
3. Document direct COM invocation (PowerShell/C++)

---

## 9. Estimated Effort (MVP v0 MSI-Only)

| Phase | Effort |
|-------|--------|
| GUID Generation | 1 hour |
| Source Code Changes | 4-6 hours |
| MSI Changes (no MSIX) | 2-4 hours |
| Build Testing | 2-4 hours |
| Acceptance Testing | 4-8 hours |
| **Total** | **13-23 hours** |

---

## 10. Risk Assessment (MVP v0)

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| COM marshaling fails with new IID | High | Low | Test proxy/stub rebuild thoroughly |
| HNS network collision | Medium | Low | Unique GUIDs |
| Registry conflict | Medium | Low | Separate registry tree |
| Binary name conflicts | Low | Medium | Rename all binaries |
| ~~MSIX signing~~ | N/A | N/A | **Not applicable for MVP v0** |
| HCS concurrent VM conflict | Medium | Low | Runtime verification needed |

---

## 11. Post-MVP v0: MSIX Integration (Future)

When ready to add MSIX support:

1. Create dev signing certificate
2. Update `msixgluepackage/AppxManifest.in`:
   - Identity Name: `YourPublisher.WSLX`
   - Update execution aliases to `wslx.exe`, `wslxconfig.exe`
3. Update CLSID reference in AppExtension (line 59)
4. Remove `SKIPMSIX=1` from install command
5. Sign package with dev certificate
6. Test alias registration doesn't conflict with canonical
