# WSL Codebase Evidence Pack

**Date:** 2026-01-28
**Repo:** WSLX (WSL open-source fork)
**Purpose:** Audit-grade evidence for singleton surfaces, replace-mode, and SxS planning

---

## 1. Distribution Channels & Identity Surfaces

### A. Repo-Built MSI / Deploy-to-Host Path

**Build Steps (from `doc/docs/dev-loop.md`):**
```bash
cmake .                    # Generate wsl.sln
cmake --build .            # Build all targets
msiexec /i bin\x64\debug\wsl.msi   # Install MSI
# OR
powershell tools/deploy/deploy-to-host.ps1  # Deploy script
```

**MSI Identity Values (from `msipackage/package.wix.in:2`):**
```xml
<Package
    Name="Windows Subsystem for Linux"
    Version="${PACKAGE_VERSION}"
    Manufacturer="Microsoft Corporation"
    UpgradeCode="6D5B792B-1EDC-4DE9-8EAD-201B820F8E82"
    Scope="perMachine" />
```

- **UpgradeCode:** `{6D5B792B-1EDC-4DE9-8EAD-201B820F8E82}` - PROVEN (package.wix.in:2)
- **Install Directory:** `C:\Program Files\WSL` - PROVEN (package.wix.in:7)

**MSI Behavior:**
- The MSI and the repo deploy script **REPLACE** canonical WSL due to same UpgradeCode
- MSI installs a "glue" MSIX package via custom action `InstallMsix` (package.wix.in:522-523)
- The glue package provides app execution aliases (`wsl.exe`, `bash.exe`, `wslconfig.exe`)

### B. MSIX Package Identities

**Main MSIX Package (`msixinstaller/AppxManifest.in:15`):**
```xml
<Identity
    Name="MicrosoftCorporationII.WindowsSubsystemForLinux"
    Publisher="CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US"
    Version="${PACKAGE_VERSION}"
    ProcessorArchitecture="${TARGET_PLATFORM}"/>
```

**Glue MSIX Package (`msixgluepackage/AppxManifest.in:11`):**
- Same Identity Name: `MicrosoftCorporationII.WindowsSubsystemForLinux`
- Purpose: Provides execution aliases when MSI is primary installer

**App Execution Aliases (both MSIX manifests):**
```xml
<desktop:ExecutionAlias Alias="bash.exe"/>
<desktop:ExecutionAlias Alias="wsl.exe"/>
<desktop:ExecutionAlias Alias="wslconfig.exe"/>
```

**App Extension CLSID (both manifests):**
```xml
<Clsid>{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}</Clsid>
```

### Identity Surface Table

| Channel | Install Location | Identity Key | Update Mechanism | Collision Risk |
|---------|------------------|--------------|------------------|----------------|
| MSI | `C:\Program Files\WSL` | UpgradeCode `{6D5B792B-...}` | Windows Installer | **HIGH** - Same UpgradeCode replaces |
| MSIX (Store) | WindowsApps | PackageFamilyName `MicrosoftCorporationII.WindowsSubsystemForLinux_8wekyb3d8bbwe` | Store/AppInstaller | Medium |
| MSIX Glue | WindowsApps | Same as Store MSIX | Installed by MSI | Low (tied to MSI) |

---

## 2. Singleton Inventory (Proven vs Unconfirmed)

### Tier 1: PROVEN Singletons (Must Change for SxS)

#### 2.1 Windows Service Name

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Service Name** | `WSLService` / `WslService` | |
| **Code Definition** | `return const_cast<LPWSTR>(L"WslService");` | `src/windows/service/exe/ServiceMain.cpp:50` |
| **MSI Registration** | `<ServiceInstall Name="WSLService" .../>` | `msipackage/package.wix.in:218` |
| **Status** | **PROVEN** | |

#### 2.2 COM Class Identity (LxssUserSession)

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **CLSID** | `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` | |
| **IDL Definition** | `const GUID CLSID_LxssUserSession = {0xa9b7a1b9, 0x0671, 0x405c, {0x95, 0xf1, 0xe0, 0x61, 0x2c, 0xb4, 0xce, 0x7e}};` | `src/windows/service/inc/wslservice.idl:157` |
| **MSI Registration** | `<RegistryKey Root="HKCR" Key="CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}">` | `msipackage/package.wix.in:152` |
| **Status** | **PROVEN** | |

#### 2.3 COM AppID

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **AppID** | `{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}` | |
| **MSI Registration** | `<RegistryKey Root="HKCR" Key="AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}">` | `msipackage/package.wix.in:143` |
| **LocalService** | Points to `WSLService` | `msipackage/package.wix.in:148` |
| **Status** | **PROVEN** | |

#### 2.4 COM Interface (ILxssUserSession)

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Interface IID** | `{38541BDC-F54F-4CEB-85D0-37F0F3D2617E}` | |
| **IDL Definition** | `[uuid(38541BDC-F54F-4CEB-85D0-37F0F3D2617E), ...]` | `src/windows/service/inc/wslservice.idl:165` |
| **MSI Registration** | `<RegistryKey Root="HKCR" Key="Interface\{38541BDC-F54F-4CEB-85D0-37F0F3D2617E}">` | `msipackage/package.wix.in:127` |
| **Status** | **PROVEN** | |

#### 2.5 COM ProxyStub CLSID

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **ProxyStub CLSID** | `{4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}` | |
| **MSI Registration** | `<RegistryKey Root="HKCR" Key="CLSID\{4EA0C6DD-E9FF-48E7-994E-13A31D10DC60}">` | `msipackage/package.wix.in:134` |
| **DLL** | `wslserviceproxystub.dll` | `msipackage/package.wix.in:138` |
| **Status** | **PROVEN** | |

#### 2.6 Registry Root (Machine-Wide Config)

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Registry Path** | `Software\Microsoft\Windows\CurrentVersion\Lxss` | |
| **IDL Definition** | `#define LXSS_REGISTRY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"` | `src/windows/service/inc/wslservice.idl:145` |
| **MSI Usage** | `<RegistryKey Root="HKLM" Key="SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss\MSI">` | `msipackage/package.wix.in:44` |
| **Status** | **PROVEN** | |

#### 2.7 HNS Network Identity (NAT Mode)

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Legacy NAT Network GUID** | `{b95d0c5e-57d4-412b-b571-18a81a16e005}` | |
| **NAT + Hyper-V Firewall GUID** | `{790e58b4-7939-4434-9358-89ae7ddbe87e}` | |
| **Code Definition** | `static constexpr GUID c_networkId = {0xb95d0c5e, 0x57d4, 0x412b, {0xb5, 0x71, 0x18, 0xa8, 0x1a, 0x16, 0xe0, 0x05}};` | `src/windows/common/WslCoreConfig.cpp:498` |
| **Code Definition** | `static constexpr GUID c_networkWithFirewallId = {0x790e58b4, 0x7939, 0x4434, {0x93, 0x58, 0x89, 0xae, 0x7d, 0xdb, 0xe8, 0x7e}};` | `src/windows/common/WslCoreConfig.cpp:501` |
| **Selection Logic** | `return FirewallConfig.Enabled() ? c_networkWithFirewallId : c_networkId;` | `src/windows/common/WslCoreConfig.cpp:503` |
| **Network Names** | `L"WSL"` and `L"WSL (Hyper-V firewall)"` | `src/windows/common/WslCoreConfig.cpp:508-509` |
| **Status** | **PROVEN** | |

#### 2.8 HCS Compute System Owner

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **VM Owner** | `L"WSL"` | |
| **Code Definition** | `inline constexpr auto c_vmOwner = L"WSL";` | `src/windows/common/wslutil.h:47` |
| **Usage (HCS Owner)** | `systemSettings.Owner = wsl::windows::common::wslutil::c_vmOwner;` | `src/windows/service/exe/WslCoreVm.cpp:1395` |
| **Usage (vmmem suffix)** | `vmSettings.ComputeTopology.Memory.HostingProcessNameSuffix = c_vmOwner;` | `src/windows/service/exe/WslCoreVm.cpp:1502` |
| **Status** | **PROVEN** | |

#### 2.9 MSI UpgradeCode

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **UpgradeCode** | `{6D5B792B-1EDC-4DE9-8EAD-201B820F8E82}` | |
| **WiX Definition** | `UpgradeCode="6D5B792B-1EDC-4DE9-8EAD-201B820F8E82"` | `msipackage/package.wix.in:2` |
| **Status** | **PROVEN** | |

### Tier 1: PROVEN Singletons (continued)

#### 2.10 HvSocket Ports

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Init Port** | `50000` | |
| **Plan9 Port** | `50001` | |
| **Plan9 DrvFs Port** | `50002` | |
| **Plan9 DrvFs Admin Port** | `50003` | |
| **VirtioFS Port** | `50004` | |
| **Crash Dump Port** | `50005` | |
| **Code Definition** | `#define LX_INIT_UTILITY_VM_INIT_PORT (50000)` ... | `src/shared/inc/lxinitshared.h:115-120` |
| **HvSocket Mapping** | Ports map to GUIDs via `HV_GUID_VSOCK_TEMPLATE` with `Data1 = Port` | `src/windows/common/hvsocket.cpp:29-30` |
| **Status** | **PROVEN** | |

**HvSocket Technical Detail:**
```cpp
// src/windows/common/hvsocket.cpp:24-31
void InitializeSocketAddress(_In_ const GUID& VmId, _In_ unsigned long Port, _Out_ PSOCKADDR_HV Address)
{
    RtlZeroMemory(Address, sizeof(*Address));
    Address->Family = AF_HYPERV;
    Address->VmId = VmId;
    Address->ServiceId = HV_GUID_VSOCK_TEMPLATE;  // Base GUID template
    Address->ServiceId.Data1 = Port;               // Port replaces Data1
}
```

The "ports" 50000-50005 become service GUIDs: `{0000C350-facb-11e6-bd58-64006a7986d3}` through `{0000C355-...}` (where C350 = 50000 in hex).

### Tier 2: PROVEN Singletons (Feature Parity)

#### 2.11 Explorer Shell Folder CLSID

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Shell Folder CLSID** | `{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}` | |
| **MSI Registration** | `<RegistryKey Root="HKLM" Key="SOFTWARE\Classes\CLSID\{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}">` | `msipackage/package.wix.in:53` |
| **Display Name** | `"Linux"` | `msipackage/package.wix.in:54` |
| **Status** | **PROVEN** | |

#### 2.12 UNC Path Aliases

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Primary UNC** | `\\wsl.localhost` | |
| **Legacy UNC** | `\\wsl$` | |
| **Explorer Alias (WSL)** | Source=`\\wsl.localhost` → Target=`::{B2B4A4D1-...}` | `msipackage/package.wix.in:94-96` |
| **Explorer Alias (WSLLegacy)** | Source=`\\wsl$` → Target=`::{B2B4A4D1-...}` | `msipackage/package.wix.in:99-101` |
| **Provider Name** | `"Plan 9 Network Provider"` | `msipackage/package.wix.in:80` |
| **Status** | **PROVEN** (Explorer aliases only) | |

### Tier 3: UNCONFIRMED / External Dependencies

#### 2.13 P9NP / P9rdr.sys Network Provider

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Service Name** | `P9NP` | |
| **Registry Path** | `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\P9NP` | `diagnostics/collect-wsl-logs.ps1:125` |
| **Driver** | `p9rdr.sys` (closed-source, Windows inbox) | External reference |
| **DLL** | `p9np.dll` (closed-source, Windows inbox) | External reference |
| **Status** | **UNCONFIRMED** - Not in repo, Windows inbox component | |

**Next Steps to Confirm:**
- Run `reg query HKLM\SYSTEM\CurrentControlSet\Services\P9NP` on a Windows machine with WSL
- Check `reg query HKLM\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order`

#### 2.14 Inbox LxssManager Service (WSL1)

| Attribute | Value | Evidence |
|-----------|-------|----------|
| **Service Name** | `lxssmanager` | |
| **IDL Reference** | `#define LXSS_INBOX_SERVICE_NAME L"lxssmanager"` | `src/windows/service/inc/wslservice.idl:20` |
| **Status** | **UNCONFIRMED** - Windows inbox component for WSL1 | |

---

## 3. COM Activation Path (Full Trace)

### Step 1: Client calls CoCreateInstance

**Client Code Pattern:**
```cpp
// From test/windows/UnitTests.cpp:652
const auto wslSupport = wil::CoCreateInstance<LxssUserSession, IWslSupport>(
    CLSCTX_LOCAL_SERVER | CLSCTX_ENABLE_CLOAKING | CLSCTX_ENABLE_AAA);
```

### Step 2: COM looks up CLSID in registry

**Registry Path:** `HKCR\CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}`
**Evidence:** `msipackage/package.wix.in:152-155`
```xml
<RegistryKey Root="HKCR" Key="CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}">
    <RegistryValue Name="AppId" Value="{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}" Type="string" />
    <RegistryValue Value="LxssUserSession" Type="string" />
</RegistryKey>
```

### Step 3: COM resolves AppID to LocalService

**Registry Path:** `HKCR\AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}`
**Evidence:** `msipackage/package.wix.in:143-149`
```xml
<RegistryKey Root="HKCR" Key="AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}">
    <RegistryValue Name="LocalService" Value="WSLService" Type="string" />
    <!-- Access/Launch permissions SDDL -->
</RegistryKey>
```

### Step 4: SCM starts WSLService (if not running)

**Service Binary:** `C:\Program Files\WSL\wslservice.exe`
**Evidence:** `msipackage/package.wix.in:217-218`
```xml
<File Id="wslservice.exe" Source="${PACKAGE_INPUT_DIR}/wslservice.exe" KeyPath="yes" />
<ServiceInstall Name="WSLService" DisplayName="WSL Service" Start="auto" Type="ownProcess" Account="LocalSystem" />
```

### Step 5: Service hosts COM class via WRL

**Service Entry:** `src/windows/service/exe/ServiceMain.cpp:50`
```cpp
static wchar_t* GetName() { return const_cast<LPWSTR>(L"WslService"); }
```

**COM Factory:** `src/windows/service/exe/LxssUserSessionFactory.cpp`
**Session Object:** `src/windows/service/exe/LxssUserSession.cpp`

---

## 4. HvSocket Contract

### Fixed Constants (Compile-Time)

| Port | Purpose | Hex | Derived Service GUID |
|------|---------|-----|---------------------|
| 50000 | Init | 0xC350 | `{0000C350-facb-11e6-bd58-64006a7986d3}` |
| 50001 | Plan9 | 0xC351 | `{0000C351-facb-11e6-bd58-64006a7986d3}` |
| 50002 | Plan9 DrvFs | 0xC352 | `{0000C352-facb-11e6-bd58-64006a7986d3}` |
| 50003 | Plan9 DrvFs Admin | 0xC353 | `{0000C353-facb-11e6-bd58-64006a7986d3}` |
| 50004 | VirtioFS | 0xC354 | `{0000C354-facb-11e6-bd58-64006a7986d3}` |
| 50005 | Crash Dump | 0xC355 | `{0000C355-facb-11e6-bd58-64006a7986d3}` |

### How Ports Become GUIDs

**Evidence:** `src/windows/common/hvsocket.cpp:24-31`

The `HV_GUID_VSOCK_TEMPLATE` (typically `{00000000-facb-11e6-bd58-64006a7986d3}`) has its `Data1` field replaced with the port number.

### Fork Strategy

To fork HvSocket ports:
1. Change constants in `src/shared/inc/lxinitshared.h:115-120`
2. Must change BOTH Windows service (listener) AND Linux init (connector)
3. Ports must be coordinated - shared header ensures consistency
4. **Risk Level:** Low - ports are local to the VM, not system-wide

---

## 5. \\wsl.localhost Limitation Analysis

### What the Repo Controls

1. **Explorer Shell Integration** - PROVEN controllable
   - Shell folder CLSID registration
   - IdListAliasTranslations mapping

2. **Plan9 Server (Linux side)** - PROVEN controllable
   - `src/linux/plan9/*.cpp` - 37 files
   - Listens on HvSocket port 50001

### What the Repo Does NOT Control

1. **P9rdr.sys** - Windows kernel driver (closed-source)
   - Handles `\\wsl.localhost` and `\\wsl$` UNC paths
   - Registered as network provider
   - **CANNOT be renamed** without replacing the driver

2. **P9np.dll** - Network provider DLL (closed-source)
   - Works with P9rdr.sys
   - **CANNOT be renamed**

### Evidence of Closed-Source Dependency

From `diagnostics/collect-wsl-logs.ps1:125`:
```powershell
reg.exe export HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\P9NP $folder/P9NP.txt
```

This exports the P9NP service configuration, indicating it's a Windows inbox service not part of this repo.

### Alternative Designs for SxS File Access

| Option | Complexity | Performance | Security | SxS Viability |
|--------|------------|-------------|----------|---------------|
| SMB share from guest | Medium | Medium | Medium | **High** - Independent namespace |
| Additional 9P via virtiofs | High | High | High | Medium - Needs driver work |
| Custom Windows redirector | Very High | High | Requires signing | Low - Driver development |
| Use canonical `\\wsl.localhost` | None | N/A | N/A | **Works** - Fork shows in same namespace |

---

## 6. VM Creation Evidence

### HCS Compute System Creation

**Function:** `WslCoreVm::GenerateConfigJson()`
**File:** `src/windows/service/exe/WslCoreVm.cpp:1392-1550`

```cpp
// Line 1394-1395
hcs::ComputeSystem systemSettings{};
systemSettings.Owner = wsl::windows::common::wslutil::c_vmOwner;  // "WSL"

// Line 1396-1398
systemSettings.ShouldTerminateOnLastHandleClosed = true;
systemSettings.SchemaVersion.Major = 2;
systemSettings.SchemaVersion.Minor = 3;
```

**VM Memory Process Name:**
```cpp
// Line 1500-1503
if (IsVmemmSuffixSupported())
{
    vmSettings.ComputeTopology.Memory.HostingProcessNameSuffix = c_vmOwner;  // Shows as "vmmem WSL"
}
```

### Kernel Selection

**Custom Kernel Path:** Configured via `.wslconfig` `[wsl2] kernel=<path>`
**Evidence:** `src/windows/common/WslCoreConfig.cpp` parses this setting

---

## 7. Commands for Runtime Verification

### Service Check
```powershell
sc qc WSLService
# Expected: SERVICE_NAME: WSLService, BINARY_PATH_NAME: "C:\Program Files\WSL\wslservice.exe"
```

### Registry Check
```powershell
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss"
reg query "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Lxss"
```

### HNS Network Check
```powershell
# Before starting WSL
hnsdiag list networks

# Start WSL
wsl -d Ubuntu -e /bin/true

# After starting WSL
hnsdiag list networks
# Expected: Network with ID {b95d0c5e-...} or {790e58b4-...} named "WSL" or "WSL (Hyper-V firewall)"
```

### HCS Compute System Check
```powershell
hcsdiag list
# Expected: Compute system with Owner="WSL"
```

### COM Registration Check
```powershell
reg query "HKCR\CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}"
reg query "HKCR\AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}"
```

### P9NP Service Check
```powershell
reg query "HKLM\SYSTEM\CurrentControlSet\Services\P9NP"
reg query "HKLM\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order"
```

---

## 8. Summary: Proven vs Unconfirmed

### PROVEN (Primary Code Evidence)

| # | Singleton | File:Line |
|---|-----------|-----------|
| 1 | Service Name `WSLService` | `ServiceMain.cpp:50`, `package.wix.in:218` |
| 2 | COM CLSID `{a9b7a1b9-...}` | `wslservice.idl:157`, `package.wix.in:152` |
| 3 | COM AppID `{370121D2-...}` | `package.wix.in:143` |
| 4 | COM Interface IID `{38541BDC-...}` | `wslservice.idl:165`, `package.wix.in:127` |
| 5 | Registry Root `Lxss` | `wslservice.idl:145`, `package.wix.in:44` |
| 6 | NAT Network GUID (legacy) `{b95d0c5e-...}` | `WslCoreConfig.cpp:498` |
| 7 | NAT Network GUID (firewall) `{790e58b4-...}` | `WslCoreConfig.cpp:501` |
| 8 | Network Names `"WSL"`, `"WSL (Hyper-V firewall)"` | `WslCoreConfig.cpp:508-509` |
| 9 | VM Owner `"WSL"` | `wslutil.h:47`, `WslCoreVm.cpp:1395` |
| 10 | MSI UpgradeCode `{6D5B792B-...}` | `package.wix.in:2` |
| 11 | HvSocket Ports 50000-50005 | `lxinitshared.h:115-120` |
| 12 | Shell Folder CLSID `{B2B4A4D1-...}` | `package.wix.in:53` |
| 13 | MSIX Identity `MicrosoftCorporationII.WindowsSubsystemForLinux` | `AppxManifest.in:15` |

### UNCONFIRMED (External/Inbox Components)

| # | Singleton | Notes |
|---|-----------|-------|
| 1 | P9NP/P9rdr.sys | Windows inbox, closed-source |
| 2 | `\\wsl.localhost` namespace | Tied to P9rdr.sys |
| 3 | `lxssmanager` service | WSL1 inbox service |
| 4 | Lxcore.sys | WSL1 kernel driver |
