# UNC Namespace Support for WSLX (`\\wslx.localhost`)

**Status:** Backlog
**Priority:** Low (workarounds exist)
**Complexity:** High

## Problem Statement

WSLX distributions do not appear in the `\\wsl.localhost` UNC namespace or Windows Explorer's "Linux" folder. This is because the Windows components that implement UNC access are closed-source and hardcode the canonical WSL identities.

Users cannot access WSLX distribution filesystems via familiar paths like:
- `\\wsl.localhost\Ubuntu`
- `\\wsl$\Ubuntu`
- Windows Explorer sidebar "Linux" entry

## Technical Root Cause

The `\\wsl.localhost` UNC path is implemented by two closed-source Microsoft inbox components:

```
User accesses \\wsl.localhost\DistroName
    ↓
P9rdr.sys (kernel-mode filesystem redirector)
    ↓
P9np.dll (user-mode network provider DLL)
    ↓
COM activation of hardcoded CLSID: {a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}
    ↓
WSLService enumerates HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss
```

### Hardcoded Values in Closed-Source Components

| Component | Location | Hardcoded Values |
|-----------|----------|------------------|
| `P9rdr.sys` | `%SystemRoot%\System32\drivers\` | UNC prefixes `\\wsl.localhost`, `\\wsl$` |
| `P9np.dll` | `%SystemRoot%\System32\` | CLSID `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` |

WSLX uses different identities to enable side-by-side operation:

| Identity | Canonical WSL | WSLX |
|----------|---------------|------|
| COM CLSID | `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` | `{21ad80bd-b800-4027-b84a-1e0d074ae507}` |
| Registry Path | `HKCU\...\Lxss` | `HKCU\...\WslX` |

Since `P9np.dll` only activates the canonical CLSID and only reads from the canonical registry path, WSLX distributions are never enumerated.

## What Would Be Required for `\\wslx.localhost`

### Required Components

| Component | Purpose | Development Effort |
|-----------|---------|-------------------|
| **P9rdrX.sys** | Kernel-mode filesystem redirector registering `\\wslx.localhost` and `\\wslx$` with the Windows I/O Manager | Very High |
| **P9npX.dll** | User-mode network provider implementing MUP interface, binding to WSLX CLSID | Medium |
| **MUP Registration** | Registry entries in `HKLM\...\NetworkProvider\Order` | Low |

### P9rdrX.sys (Kernel Driver)

A custom kernel-mode filesystem redirector driver would need to:

1. Register `\\wslx.localhost` and `\\wslx$` UNC prefixes with Windows I/O Manager
2. Implement IRP handlers for filesystem operations (create, read, write, query, etc.)
3. Communicate with the Plan 9 server running inside WSLX VMs
4. Handle connection lifecycle and caching

**Challenges:**
- Requires kernel-mode driver development expertise
- Must be signed with EV code signing certificate
- Requires Microsoft attestation signing or WHQL certification for production use
- Kernel bugs can cause system instability (BSOD)

### P9npX.dll (Network Provider)

A custom user-mode network provider DLL would need to:

1. Export standard network provider entry points:
   - `NPGetCaps` - Declare capabilities
   - `NPOpenEnum` / `NPEnumResource` / `NPCloseEnum` - Enumerate distributions
   - `NPGetResourceInformation` - Resolve UNC paths
2. Activate WSLX COM CLSID: `{21ad80bd-b800-4027-b84a-1e0d074ae507}`
3. Enumerate distributions from `HKCU\Software\Microsoft\Windows\CurrentVersion\WslX`

**Challenges:**
- Network provider API is sparsely documented
- Must handle all edge cases (offline, permissions, etc.)
- Testing requires careful integration with Windows shell

### Existing WSLX Components (Already Compatible)

These components already support fork operation and would work with a custom UNC provider:

- COM service with separate CLSID/AppID/IID
- Distribution enumeration from `WslX` registry key
- Plan 9 server in VM (protocol unchanged)
- HvSocket communication (ports 51000-51005)

## Alternative Solutions

### Option 1: Custom Kernel Driver + Network Provider

**Approach:** Develop P9rdrX.sys and P9npX.dll from scratch.

| Pros | Cons |
|------|------|
| Native Windows integration | Significant development effort |
| Familiar UNC path experience | Kernel driver signing requirements |
| Works with all Windows applications | Maintenance burden for Windows updates |

**Estimated Effort:** 3-6 months for experienced Windows driver developer

### Option 2: Usermode Filesystem (Dokan/WinFsp)

**Approach:** Use an existing usermode filesystem framework to expose WSLX distributions as drive letters or UNC paths.

| Pros | Cons |
|------|------|
| No kernel driver development | Requires third-party dependency |
| Well-documented frameworks | Performance overhead vs kernel driver |
| Easier to develop and debug | May not support `\\wslx.localhost` syntax |

**Frameworks:**
- [WinFsp](https://github.com/winfsp/winfsp) - Windows File System Proxy
- [Dokan](https://github.com/dokan-dev/dokany) - User-mode filesystem for Windows

**Implementation sketch:**
```
WinFsp/Dokan filesystem driver
    ↓
WSLX usermode service (new component)
    ↓
COM activation of WSLX CLSID
    ↓
Plan 9 protocol to VM
```

**Estimated Effort:** 1-2 months

### Option 3: SMB/Samba Share from Distribution

**Approach:** Run Samba server inside each WSLX distribution, exposing the root filesystem.

| Pros | Cons |
|------|------|
| No Windows development required | Requires per-distro configuration |
| Standard Windows networking | Different UNC path format |
| Works immediately | Security considerations |

**Implementation:**
```bash
# Inside WSLX distribution
sudo apt install samba
sudo smbpasswd -a $USER

# /etc/samba/smb.conf
[wslx-root]
    path = /
    read only = no
    browsable = yes
```

Access from Windows: `\\localhost\wslx-root` or `\\<distro-ip>\wslx-root`

**Automation possibility:** WSLX could auto-configure Samba on first boot.

**Estimated Effort:** 1-2 weeks (including automation)

### Option 4: VirtioFS / DrvFs Passthrough

**Approach:** Mount Windows directories into WSLX distributions rather than accessing Linux files from Windows.

| Pros | Cons |
|------|------|
| Already supported | Reverse of desired workflow |
| No additional development | Doesn't solve Windows-side access |
| Best performance | |

This is the current recommended workaround but doesn't address the Windows-to-Linux access pattern.

### Option 5: Petition Microsoft

**Approach:** Request Microsoft add configurability to P9np.dll to support alternative CLSIDs via registry.

| Pros | Cons |
|------|------|
| No WSLX development needed | Depends on Microsoft priorities |
| Official support | Unknown timeline |
| Benefits other WSL forks | May be rejected |

**Suggested feature request:**
- Allow P9np.dll to read CLSID from registry instead of hardcoding
- Support multiple UNC prefixes with configurable CLSID bindings
- File via Windows Feedback Hub or WSL GitHub issues

## Recommendation

For the MVP phase, continue with current workarounds (VirtioFS, SMB, CLI access).

For future improvement, **Option 2 (WinFsp/Dokan)** offers the best balance of:
- Reasonable development effort
- No kernel driver signing complexity
- Proven technology stack
- Potential for `\\wslx.localhost` or `\\wslx$` syntax via WinFsp's UNC support

If native performance is critical, **Option 1 (custom driver)** becomes necessary but should only be pursued with dedicated Windows kernel development resources.

## References

- [Research/UNC_RUNTIME_PROOF.md](../../../Research/UNC_RUNTIME_PROOF.md) - Verification procedures for CLSID hardcoding
- [Research/SXS_DEEP_DIVE.md](../../../Research/SXS_DEEP_DIVE.md) - Evidence of P9np.dll limitations
- [doc/docs/technical-documentation/plan9.md](../../docs/technical-documentation/plan9.md) - Plan 9 protocol documentation
- [WinFsp GitHub](https://github.com/winfsp/winfsp) - Usermode filesystem framework
- [Windows Network Provider API](https://docs.microsoft.com/en-us/windows/win32/secauthn/network-provider-api)
