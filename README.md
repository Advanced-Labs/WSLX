# WSLX - Windows Subsystem for Linux (Fork)

<p align="center">
  <img src="./Images/Square44x44Logo.targetsize-256.png" alt="WSLX logo"/>
</p>

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%2011-0078D6.svg)]()

**WSLX** is a fork of Microsoft's open-source [WSL](https://github.com/microsoft/WSL) repository, modified to run **side-by-side (SxS)** with the canonical WSL installation on the same Windows machine.

## Why WSLX?

WSLX enables scenarios that aren't possible with a single WSL installation:

- **Development & Testing** - Develop and test WSL modifications without risking your production WSL environment
- **Experimental Features** - Run bleeding-edge or experimental WSL features in complete isolation
- **Research** - Study WSL internals and behavior without affecting daily workflows
- **CI/CD Testing** - Test WSL-dependent applications against different WSL configurations simultaneously

## Important Limitation

> **Warning**
> **`\\wsl.localhost` UNC namespace is NOT supported for WSLX distributions.**
>
> The Windows shell integration components (`P9rdr.sys`, `P9np.dll`) are closed-source and hardcode the canonical WSL CLSID. WSLX distributions will **not** appear in `\\wsl.localhost` or Windows Explorer's "Linux" folder.
>
> **Workarounds:**
> - Use **VirtioFS** mounts for file access
> - Use **SMB/Samba** shares within the distribution
> - Access files via `wslx.exe` commands directly

## Technical Architecture

WSLX achieves SxS operation by changing all identity surfaces that would otherwise collide with canonical WSL:

### Service Identity

| Component | Canonical | WSLX |
|-----------|-----------|------|
| Service Name | `WSLService` | `WSLXService` |
| CLI Binary | `wsl.exe` | `wslx.exe` |
| Service Binary | `wslservice.exe` | `wslxservice.exe` |
| Install Directory | `C:\Program Files\WSL` | `C:\Program Files\WSLX` |

### COM Identity (Unique GUIDs)

| Purpose | WSLX GUID |
|---------|-----------|
| CLSID (UserSession) | `{21ad80bd-b800-4027-b84a-1e0d074ae507}` |
| AppID | `{4424eff5-6510-481d-b912-606d81c43c52}` |
| IID (IUserSession) | `{8b283ac3-d362-46f2-b68f-3ba6a1607cc8}` |
| ProxyStub CLSID | `{27a1899d-d923-4ddd-91b7-454b90109e50}` |

### Registry & Networking

| Component | Canonical | WSLX |
|-----------|-----------|------|
| Distro Registry | `HKCU\...\Lxss` | `HKCU\...\WslX` |
| HNS Network Name | `WSL` | `WSLX` |
| VM Owner | `WSL` | `WSLX` |
| HvSocket Ports | `50000-50005` | `51000-51005` |

All identity constants are centralized in [`src/shared/inc/fork_identity.h`](./src/shared/inc/fork_identity.h).

## Installation

### Prerequisites

- Windows 11 (Build 22000+)
- Virtual Machine Platform enabled
- Administrator privileges for installation

### Install WSLX (SxS Safe)

```powershell
# Install WSLX alongside canonical WSL
msiexec /i wslx.msi SKIPMSIX=1 /qn

# Verify both services are running
sc query WSLService    # Canonical WSL
sc query WSLXService   # WSLX Fork
```

The `SKIPMSIX=1` flag prevents WSLX from interfering with the canonical WSL MSIX package.

### Basic Usage

```powershell
# Install a distribution
wslx --install Ubuntu

# List distributions
wslx --list --verbose

# Run a command
wslx -d Ubuntu -- echo "Hello from WSLX"

# Enter a distribution
wslx -d Ubuntu
```

## Building from Source

### Requirements

- Visual Studio 2022 with C++ workload
- Windows SDK 26100+
- CMake 3.25+
- Nuget

### Build Steps

```powershell
# Clone the repository
git clone https://github.com/Advanced-Labs/WSLX.git
cd WSLX

# Configure (copy and customize if needed)
cp UserConfig.cmake.sample UserConfig.cmake

# Build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Build MSI
cmake --build build --target package
```

See [doc/docs/dev-loop.md](./doc/docs/dev-loop.md) for detailed development instructions.

## Key Files Modified

The following files contain WSLX-specific modifications from upstream WSL:

| File | Purpose |
|------|---------|
| `src/shared/inc/fork_identity.h` | Central identity header with all fork constants |
| `src/windows/wslservice/exe/ServiceMain.cpp` | Service name registration |
| `src/windows/wslservice/inc/wslservice.idl` | COM GUIDs and registry paths |
| `src/windows/common/WslCoreConfig.cpp` | HNS network IDs and names |
| `src/windows/common/wslutil.h` | VM owner string |
| `src/shared/inc/lxinitshared.h` | HvSocket port numbers |
| `msipackage/package.wix.in` | MSI identities, MSIX cleanup disabled |

## Documentation

### WSLX Research Documents

The [`Research/`](./Research/) directory contains detailed technical analysis:

| Document | Description |
|----------|-------------|
| [SXS_MVP_PLAN.md](./Research/SXS_MVP_PLAN.md) | Implementation plan and checklist |
| [SXS_DEEP_DIVE.md](./Research/SXS_DEEP_DIVE.md) | Technical deep dive with evidence labels |
| [CONCURRENT_VM_PROOF.md](./Research/CONCURRENT_VM_PROOF.md) | Procedure to verify concurrent VM operation |
| [UNC_RUNTIME_PROOF.md](./Research/UNC_RUNTIME_PROOF.md) | Procedure to verify UNC enumeration behavior |
| [EVIDENCE_PACK.md](./Research/EVIDENCE_PACK.md) | Audit-grade evidence for all singleton claims |

### Upstream WSL Documentation

| Document | Description |
|----------|-------------|
| [dev-loop.md](./doc/docs/dev-loop.md) | Build and development workflow |
| [debugging.md](./doc/docs/debugging.md) | Debugging guide |
| [technical-documentation/](./doc/docs/technical-documentation/) | Architecture deep dives |

## Known Limitations

| Limitation | Reason | Workaround |
|------------|--------|------------|
| `\\wsl.localhost` not supported | P9rdr.sys/P9np.dll are closed-source | Use VirtioFS or SMB shares |
| WSL1 mode not supported | Requires Lxcore.sys (closed-source kernel driver) | Use WSL2 mode only |
| No `bash.exe`/`wsl.exe` aliases | Execution aliases would conflict with canonical WSL | Invoke `wslx.exe` directly |
| Windows Explorer integration | Shell folder CLSID is hardcoded in Explorer | Access via CLI only |

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](./CONTRIBUTING.md) before submitting changes.

For WSLX-specific changes, ensure all identity constants remain in `fork_identity.h` and do not introduce new collisions with canonical WSL.

## License

MIT License - Same as upstream WSL. See [LICENSE](./LICENSE).

## Upstream

Forked from: [microsoft/WSL](https://github.com/microsoft/WSL)

---

**Note:** WSLX is an independent fork and is not affiliated with or endorsed by Microsoft. For production WSL usage, please use the official [Microsoft WSL](https://github.com/microsoft/WSL).
