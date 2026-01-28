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
| Both create VM | Owner=WSL | Owner=WSLX | **OK** | Different owners |
| Both port-forward :8080 | Binds port | Conflict | **CONFLICT** | User responsibility |
| Both access `\\wsl.localhost` | Works | Works (sees own distros via fork registry) | **PARTIAL** | Explorer shows both shell folders |
| Both upgrade/install | MSI UpgradeCode | Fork UpgradeCode | **OK** | Independent packages |
| Both register distros | `Lxss` registry | `WslX` registry | **OK** | Separate registry |

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

## 7. SxS MVP Checklist

| # | Task | Status |
|---|------|--------|
| 1 | Generate new GUIDs for all COM identities | [ ] |
| 2 | Create `fork_identity.h` with all constants | [ ] |
| 3 | Update `ServiceMain.cpp` service name | [ ] |
| 4 | Update `wslservice.idl` COM identities | [ ] |
| 5 | Update `WslCoreConfig.cpp` HNS network IDs | [ ] |
| 6 | Update `wslutil.h` VM owner | [ ] |
| 7 | Update `lxinitshared.h` HvSocket ports | [ ] |
| 8 | Update `package.wix.in` all identities | [ ] |
| 9 | Update MSIX manifests | [ ] |
| 10 | Build and test MSI installation | [ ] |
| 11 | Run acceptance tests 1-7 | [ ] |
| 12 | Document any issues found | [ ] |

---

## 8. Estimated Effort

| Phase | Effort |
|-------|--------|
| GUID Generation | 1 hour |
| Source Code Changes | 4-8 hours |
| MSI/MSIX Changes | 4-8 hours |
| Build Testing | 2-4 hours |
| Acceptance Testing | 4-8 hours |
| **Total** | **15-30 hours** |

---

## 9. Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| COM marshaling fails with new IID | High | Low | Test proxy/stub rebuild thoroughly |
| HNS network collision | Medium | Low | Unique GUIDs |
| Registry conflict | Medium | Low | Separate registry tree |
| Binary name conflicts | Low | Medium | Rename all binaries |
| MSIX signing | Medium | Medium | Create dev certificate |
