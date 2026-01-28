# WSL Codebase Analysis Report: Replace-Mode and SxS Readiness

## 1. Executive Summary (15 bullets)

1. **Build Process**: CMake-based build generates `wsl.msi` or MSIX package; deployment via `tools/deploy/deploy-to-host.ps1` or MSI install
2. **Replace Mode**: Default install **replaces** canonical WSL via same package identity (`UpgradeCode: 6D5B792B-1EDC-4DE9-8EAD-201B820F8E82`)
3. **Service Name**: Windows service `WSLService` (single instance, Session 0, runs as SYSTEM)
4. **COM Interface**: `ILxssUserSession` CLSID `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}`, AppID `{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}`
5. **Registry Root**: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss` for machine config, HKCU equivalent for user config
6. **Network Identity**: Hardcoded NAT Network GUIDs - `{b95d0c5e-57d4-412b-b571-18a81a16e005}` (legacy) and `{790e58b4-7939-4434-9358-89ae7ddbe87e}` (with Hyper-V Firewall)
7. **VM Owner**: HCS compute system uses `Owner = "WSL"` identifier
8. **Filesystem Paths**: UNC `\\wsl.localhost` and `\\wsl$` paths are registered via closed-source `p9rdr.sys` driver
9. **Install Directory**: `C:\Program Files\WSL` with tools in `tools/` subdirectory (kernel, initrd.img)
10. **HvSocket Ports**: Fixed ports 50000-50005 for init, plan9, virtiofs, crash dump communication
11. **Explorer Integration**: Shell folder CLSID `{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}` shows "Linux" in File Explorer
12. **Closed-Source Dependencies**: `p9rdr.sys` (Plan 9 redirector), `P9np.dll` (network provider) - cannot be renamed
13. **WSL1 Dependency**: `lxss` service and `Lxcore.sys` driver are inbox/closed-source
14. **Kernel Selection**: Custom kernel via `.wslconfig` `[wsl2] kernel=` setting; shared across all distros
15. **SxS Feasibility**: Achievable by forking service name, COM identities, registry paths, HNS network IDs, and install directory

---

## 2. Boot Path Map (Sequence Diagram)

```
┌─────────────┐     ┌───────────────────┐     ┌────────────┐     ┌──────────┐     ┌──────┐
│   wsl.exe   │     │  wslservice.exe   │     │  mini_init │     │   init   │     │ bash │
└──────┬──────┘     └─────────┬─────────┘     └──────┬─────┘     └────┬─────┘     └──┬───┘
       │                      │                      │                 │              │
       │  CoCreateInstance()  │                      │                 │              │
       │ (ILxssUserSession)   │                      │                 │              │
       │─────────────────────>│                      │                 │              │
       │                      │                      │                 │              │
       │  CreateInstance()    │                      │                 │              │
       │─────────────────────>│                      │                 │              │
       │                      │                      │                 │              │
       │                      │  [VM not running?]   │                 │              │
       │                      │  HcsCreateComputeSystem()              │              │
       │                      │  (kernel + initrd.img)                 │              │
       │                      │─────────────────────>│                 │              │
       │                      │                      │                 │              │
       │                      │ LxMiniInitMessageEarlyConfig           │              │
       │                      │─────────────────────>│                 │              │
       │                      │                      │                 │              │
       │                      │                      │ fork()/exec(gns)│              │
       │                      │                      │────────>        │              │
       │                      │                      │                 │              │
       │                      │ LxMiniInitMessageLaunchInit            │              │
       │                      │─────────────────────>│                 │              │
       │                      │                      │                 │              │
       │                      │                      │ fork()/exec(init)              │
       │                      │                      │────────────────>│              │
       │                      │                      │                 │              │
       │                      │     hvsocket connect (port 50000)      │              │
       │                      │<───────────────────────────────────────│              │
       │                      │                      │                 │              │
       │  CreateLxProcess()   │                      │                 │              │
       │─────────────────────>│                      │                 │              │
       │                      │                      │                 │              │
       │                      │ LxInitMessageCreateSession             │              │
       │                      │────────────────────────────────────────>│              │
       │                      │                      │                 │              │
       │                      │                      │          fork()/exec(bash)     │
       │                      │                      │                 │─────────────>│
       │                      │                      │                 │              │
       │  S_OK + hvsockets    │                      │                 │              │
       │<─────────────────────│                      │                 │              │
       │                      │                      │                 │              │
       │                      │     STDIN/STDOUT/STDERR relay          │              │
       │<══════════════════════════════════════════════════════════════════════════>│
       │                      │                      │                 │              │
```

**Code Flow:**
1. `wsl.exe` → `src/windows/wsl/main.cpp` calls `CoCreateInstance()` on `ILxssUserSession`
2. `wslservice.exe` → `src/windows/service/exe/LxssUserSession.cpp:CreateInstance()`
3. VM creation → `src/windows/service/exe/WslCoreVm.cpp:GenerateConfigJson()` → `HcsCreateComputeSystem()`
4. Mini init boot → `src/linux/init/main.cpp` (initramfs)
5. Init launch → `src/linux/init/init.cpp` (per-distro)
6. Process relay → `src/windows/common/relay.cpp`

---

## 3. Singleton Inventory Table

| # | Singleton Name | Type | Where Defined | How Used | Parameterization Strategy |
|---|----------------|------|---------------|----------|---------------------------|
| **1** | `WSLService` | Service Name | `msipackage/package.wix.in:218` | Windows service identity | Compile-time: change ServiceInstall Name |
| **2** | `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` | COM CLSID | `msipackage/package.wix.in:152` | LxssUserSession class | Compile-time: new GUID in package.wix.in + IDL |
| **3** | `{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}` | COM AppID | `msipackage/package.wix.in:143` | Service hosting identity | Compile-time: new GUID |
| **4** | `{38541BDC-F54F-4CEB-85D0-37F0F3D2617E}` | COM Interface | `msipackage/package.wix.in:127` | ILxssUserSession interface | Compile-time: new GUID in IDL |
| **5** | `{4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}` | ProxyStub CLSID | `msipackage/package.wix.in:130` | COM marshaling | Compile-time: new GUID |
| **6** | `SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss` | Registry Path | `src/windows/common/registry.hpp:17` | Machine-wide config storage | Compile-time constant |
| **7** | `SYSTEM\CurrentControlSet\Services\LxssManager` | Registry Path | `src/windows/common/registry.hpp:17` | Legacy LXSS settings | Compile-time constant |
| **8** | `{b95d0c5e-57d4-412b-b571-18a81a16e005}` | HNS Network ID | `test/windows/NetworkTests.cpp:1654` | NAT Network (legacy) | Compile-time constant in networking code |
| **9** | `{790e58b4-7939-4434-9358-89ae7ddbe87e}` | HNS Network ID | `test/windows/NetworkTests.cpp:1658` | NAT Network (with firewall) | Compile-time constant |
| **10** | `WSL` | VM Owner | `src/windows/common/wslutil.h:47` | HCS compute system ownership | Compile-time: `c_vmOwner` |
| **11** | `\\wsl.localhost` | UNC Path | `msipackage/package.wix.in:81,96` | Plan9 filesystem access | CANNOT change (p9rdr.sys hardcoded) |
| **12** | `\\wsl$` | UNC Path | `msipackage/package.wix.in:101` | Legacy Plan9 path | CANNOT change (p9rdr.sys hardcoded) |
| **13** | `{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}` | Shell Folder CLSID | `msipackage/package.wix.in:53` | Explorer "Linux" folder | Compile-time: new GUID |
| **14** | `C:\Program Files\WSL` | Install Path | `msipackage/package.wix.in:7` | Binary installation | CMake/WiX: INSTALLDIR |
| **15** | `6D5B792B-1EDC-4DE9-8EAD-201B820F8E82` | MSI UpgradeCode | `msipackage/package.wix.in:2` | Package upgrade identity | Compile-time: new UpgradeCode for fork |
| **16** | `50000-50005` | HvSocket Ports | `src/shared/inc/lxinitshared.h:115-120` | VM-host communication | Compile-time constants |
| **17** | `MicrosoftCorporationII.WindowsSubsystemForLinux_8wekyb3d8bbwe` | Package Family | `src/windows/common/wslutil.h:43` | MSIX identity | Packaging: AppxManifest.in |
| **18** | `wsl.exe` | Binary Name | `src/windows/inc/wsl.h:17` | CLI entry point | Compile-time + install rename |
| **19** | `WslInstaller` | Service Name | `src/windows/wslinstaller/exe/ServiceMain.cpp:22` | Installer service | Compile-time constant |
| **20** | `lxss` | Inbox Service | `src/windows/common/helpers.cpp:488` | WSL1 (cannot change) | CANNOT change (Windows component) |

---

## 4. Patch List (Replace-Mode Prototype)

### Patch 1: Custom Kernel Support Validation
**Files:** `src/windows/common/WslCoreConfig.cpp`, `.wslconfig`
**Rationale:** Validate custom kernel loading works. The `.wslconfig` file already supports `[wsl2] kernel=<path>`. Test that forked WSL loads the kernel correctly and that all distros share it as expected.

### Patch 2: Fork Identification Header
**Files:** Create `src/shared/inc/fork_identity.h`
```cpp
#pragma once
// Fork identity - change these for SxS mode
#define WSLX_FORK_NAME L"WSLX"
#define WSLX_SERVICE_NAME L"WSLXService"
#define WSLX_VM_OWNER L"WSLX"
#define WSLX_REGISTRY_ROOT L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WslX"
```
**Rationale:** Centralize all identity constants for easy forking. In replace-mode, these remain as canonical values; in SxS mode, all values change.

### Patch 3: Custom Init Contract Support
**Files:** `src/linux/init/main.cpp`, `src/linux/init/init.cpp`
**Rationale:** Add hook points for custom init behavior. Can inject additional daemons or modify rootfs layout detection. The init sequence in `src/linux/init/main.cpp:4298 lines` already has clear phases that can be extended.

### Patch 4: Telemetry Disable for Testing
**Files:** `src/windows/common/WslCoreConfig.cpp:89`
**Rationale:** For development, set `EnableTelemetry = false` to avoid sending data to Microsoft during testing.

### Discovered Invariants (Cannot Break)
- **p9rdr.sys dependency**: `\\wsl.localhost` UNC paths are handled by closed-source `p9rdr.sys` driver. A fork cannot provide its own filesystem namespace without replacing this driver.
- **WSL1 boundaries**: `Lxcore.sys` and `lxss` service are Windows inbox components. Fork must use WSL2 only.
- **HCS API**: VM creation uses `HcsCreateComputeSystem` which is a Windows API - no alternative exists.

---

## 5. SxS Blueprint + Collision Matrix

### 5.1 Identity Fork Map

| Category | Canonical Value | Fork Value | Type | Risk | Test |
|----------|-----------------|------------|------|------|------|
| **Service** | `WSLService` | `WSLXService` | Windows Service | Low | `sc query WSLXService` |
| **COM CLSID** | `{a9b7a1b9-...}` | `{NEW-GUID-1}` | COM Class | Medium | CoCreateInstance succeeds |
| **COM AppID** | `{370121D2-...}` | `{NEW-GUID-2}` | COM App | Medium | Service hosts correctly |
| **COM Interface** | `{38541BDC-...}` | `{NEW-GUID-3}` | Interface | High | Proxy/stub marshaling |
| **Registry Root** | `Lxss` | `WslX` | Registry | Low | Key enumeration |
| **NAT Network ID** | `{b95d0c5e-...}` | `{NEW-GUID-4}` | HNS Network | Medium | `hnsdiag list networks` |
| **VM Owner** | `WSL` | `WSLX` | HCS Identity | Low | `hcsdiag list` |
| **Shell CLSID** | `{B2B4A4D1-...}` | `{NEW-GUID-5}` | Explorer | Low | File Explorer shows both |
| **Install Path** | `C:\Program Files\WSL` | `C:\Program Files\WSLX` | Filesystem | Low | Files exist |
| **MSI UpgradeCode** | `{6D5B792B-...}` | `{NEW-GUID-6}` | Package | Critical | `wmic product` shows both |
| **Binary Name** | `wsl.exe` | `wslx.exe` | CLI | Low | Command works |
| **Package Family** | `Microsoft...WSL_8we...` | `Custom...WSLX_8we...` | MSIX | Medium | Package installed |
| **HvSocket Ports** | 50000-50005 | 51000-51005 | Network | Medium | Init connects |

### 5.2 Collision Matrix

| Scenario | Canonical Running | Fork Running | Conflict Point | Resolution |
|----------|-------------------|--------------|----------------|------------|
| **Both services start** | Yes | Yes | None if renamed | Services coexist |
| **Both create VM** | Yes | Yes | VM Owner name | Use different `c_vmOwner` |
| **Both create NAT network** | Yes | Yes | HNS Network ID collision | Different Network GUIDs |
| **Both port-forward same port** | Yes | Yes | Port conflict | User responsibility |
| **Both install/upgrade** | Yes | No | MSI UpgradeCode | Different UpgradeCode = independent |
| **Both access \\wsl.localhost** | Yes | Yes | **HARD CONFLICT** | Cannot resolve - p9rdr.sys limitation |
| **Both register Explorer folder** | Yes | Yes | Different CLSIDs | Both visible, user chooses |
| **Both distributions registered** | Yes | Yes | Registry namespace | Separate `Lxss` vs `WslX` roots |

### 5.3 Minimal SxS MVP Renames

**Critical Path (minimum for VM boot):**
1. `WSLService` → `WSLXService` (service name)
2. `{a9b7a1b9-...}` → new GUID (COM CLSID)
3. `{370121D2-...}` → new GUID (COM AppID)
4. `{b95d0c5e-...}` / `{790e58b4-...}` → new GUIDs (HNS Network)
5. `WSL` → `WSLX` (VM Owner)
6. `Lxss` → `WslX` (registry root)
7. `C:\Program Files\WSL` → `C:\Program Files\WSLX` (install path)
8. `6D5B792B-...` → new GUID (MSI UpgradeCode)

**Known Limitation:**
The fork **cannot** provide `\\wslx.localhost` paths. The `p9rdr.sys` driver is closed-source and hardcodes `\\wsl.localhost` and `\\wsl$`. Options:
- Accept no Windows-accessible filesystem (use `\\wsl.localhost` from canonical WSL)
- Implement own 9P redirector driver (significant effort)
- Use network shares instead (SMB)

---

## 6. Appendix: Code Pointers

### Service Registration
- Service definition: `msipackage/package.wix.in:218`
- Service entry: `src/windows/service/exe/ServiceMain.cpp:58-94`
- COM factory: `src/windows/service/exe/LxssUserSessionFactory.cpp`

### VM Management
- VM creation: `src/windows/service/exe/WslCoreVm.cpp:1392-1550` (GenerateConfigJson)
- Config parsing: `src/windows/common/WslCoreConfig.cpp:25-350`
- Network IDs: Inferred from `test/windows/NetworkTests.cpp:1650-1660`

### Registry Operations
- Path constant: `src/windows/common/registry.hpp:17` (LXSS_SERVICE_REGISTRY_PATH)
- Key operations: `src/windows/common/registry.cpp:266-280`

### IPC/Communication
- HvSocket ports: `src/shared/inc/lxinitshared.h:115-148`
- Socket channel: `src/shared/inc/SocketChannel.h`
- Service comm: `src/windows/common/svccomm.cpp`

### Linux Components
- Mini init: `src/linux/init/main.cpp` (4298 lines)
- Full init: `src/linux/init/init.cpp` (99KB)
- Plan9 server: `src/linux/plan9/*.cpp` (37 files)

### Networking
- NAT networking: `src/windows/common/NatNetworking.cpp`
- Network config: `src/windows/common/WslCoreConfig.h:85-125` (NetworkingMode enum)
- HNS schema: `src/shared/inc/hns_schema.h`

### Build/Deploy
- CMake root: `CMakeLists.txt`
- MSI package: `msipackage/package.wix.in`
- Deploy script: `tools/deploy/deploy-to-host.ps1`
- Dev docs: `doc/docs/dev-loop.md`

---

## Build Steps Used

```bash
# Prerequisites: Visual Studio 2022 with SDK 26100, CMake >= 3.25

# Generate solution
cmake .

# Build (Debug by default)
cmake --build .

# Deploy to host (replaces canonical WSL)
powershell tools/deploy/deploy-to-host.ps1

# Or install MSI directly
msiexec /i bin\x64\debug\wsl.msi
```

**Deviation from dev-loop.md:** None required for basic build. For ARM64: `cmake . -A arm64`

---

## Installed Artifacts Summary

| Path | Contents |
|------|----------|
| `C:\Program Files\WSL\wsl.exe` | Main CLI |
| `C:\Program Files\WSL\wslservice.exe` | Session 0 service |
| `C:\Program Files\WSL\wslhost.exe` | Interop host |
| `C:\Program Files\WSL\wslrelay.exe` | Network relay |
| `C:\Program Files\WSL\wslg.exe` | GUI support |
| `C:\Program Files\WSL\tools\kernel` | Linux kernel |
| `C:\Program Files\WSL\tools\initrd.img` | Initial ramdisk |
| `C:\Program Files\WSL\tools\init` | Linux init binary |
| `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss` | Machine config |
| `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss` | User distro registration |
