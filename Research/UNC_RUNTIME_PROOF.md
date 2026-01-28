# UNC Enumeration Runtime Proof Procedure

**Date:** 2026-01-28
**Objective:** Definitively prove which CLSID is activated when accessing `\\wsl.localhost`

---

## 1. Background: What We Know from Code

### P9NP TriggerStart Registration

The WSL service registers UNC prefixes with the P9NP network provider:

```cpp
// src/windows/service/exe/ServiceMain.cpp:122,145
constexpr auto* keyName = L"SYSTEM\\CurrentControlSet\\Services\\P9NP\\NetworkProvider";
constexpr wchar_t newValue[] = L"wsl.localhost\0wsl$\0";
THROW_IF_WIN32_ERROR(RegSetValueEx(key.get(), valueName, 0, REG_MULTI_SZ, (BYTE*)newValue, sizeof(newValue)));
```

### Explorer Shell Folder Links to UNC

```xml
<!-- msipackage/package.wix.in:81 -->
<RegistryValue Name="ResName" Value="\\wsl.localhost" Type="string"/>
```

### Glue MSIX Contains CLSID Reference

```xml
<!-- msixgluepackage/AppxManifest.in:59 -->
<Clsid>{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}</Clsid>
```

### Hypothesis

P9np.dll (closed-source network provider) hardcodes or reads the canonical CLSID `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` when activating COM for UNC enumeration.

**If true:** Fork distros (registered under `WslX` registry) will NOT appear in `\\wsl.localhost`

---

## 2. Runtime Verification Procedure

### Prerequisites

- Windows 10/11 with WSL installed
- Process Monitor (ProcMon) from Sysinternals
- Administrator access

### Step 1: Configure ProcMon Filters

```
Filter: Path contains "HKCR\CLSID\{a9b7a1b9" → Include
Filter: Path contains "HKCR\CLSID\{" → Include (for any CLSID activation)
Filter: Path contains "Lxss" → Include (registry enumeration)
Filter: Path contains "P9" → Include (network provider activity)
Filter: Process Name is "explorer.exe" → Include
Filter: Process Name is "svchost.exe" → Include
Filter: Process Name is "System" → Include
```

### Step 2: Clear WSL State

```powershell
# Ensure no WSL VMs are running
wsl --shutdown
Start-Sleep -Seconds 2

# Clear ProcMon capture
# In ProcMon: Edit → Clear Display
```

### Step 3: Trigger UNC Enumeration

```powershell
# Method 1: Command-line directory listing
cmd /c "dir \\wsl.localhost\"

# Method 2: Explorer navigation (alternative)
# In Explorer, type: \\wsl.localhost in address bar
```

### Step 4: Capture and Analyze

**Expected ProcMon Output (canonical behavior):**

| Time | Process | Operation | Path | Result |
|------|---------|-----------|------|--------|
| ... | svchost.exe | RegQueryValue | HKCR\CLSID\{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}\... | SUCCESS |
| ... | svchost.exe | RegQueryValue | HKCR\AppID\{370121D2-AA7E-4608-A86D-0BBAB9DA1A60}\LocalService | SUCCESS |
| ... | wslservice.exe | RegEnumKey | HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss | SUCCESS |

**Key observations to record:**

1. **Which CLSID is queried?**
   - Expected: `{a9b7a1b9-0671-405c-95f1-e0612cb4ce7e}` (canonical)
   - If fork CLSID appears: UNC is NOT hardcoded (good for SxS)

2. **Which process queries the CLSID?**
   - Expected: svchost.exe (COM activation) or System
   - This identifies the COM activation source

3. **Which registry path is enumerated for distros?**
   - Expected: `HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss`
   - This confirms registry-driven enumeration

### Step 5: Export Evidence

```powershell
# In ProcMon: File → Save → Events displayed using current filter
# Save as: UNC_CLSID_trace.CSV

# Extract key lines:
Import-Csv .\UNC_CLSID_trace.CSV | Where-Object {
    $_.Path -match "CLSID.*a9b7a1b9|AppID.*370121D2|Lxss"
} | Select-Object Time, 'Process Name', Operation, Path, Result |
  Export-Csv .\UNC_CLSID_summary.csv -NoTypeInformation
```

---

## 3. Alternative Verification: Binary Analysis

### Check P9np.dll for Hardcoded CLSID

```powershell
# Method 1: String search in binary
$dll = "$env:SystemRoot\System32\P9np.dll"
$bytes = [System.IO.File]::ReadAllBytes($dll)
$text = [System.Text.Encoding]::Unicode.GetString($bytes)

# Search for canonical CLSID (with and without braces)
if ($text -match "a9b7a1b9-0671-405c-95f1-e0612cb4ce7e") {
    Write-Host "FOUND: Canonical CLSID hardcoded in P9np.dll" -ForegroundColor Red
} else {
    Write-Host "NOT FOUND: CLSID may be read from registry" -ForegroundColor Green
}

# Search for Lxss registry path
if ($text -match "Lxss") {
    Write-Host "FOUND: 'Lxss' string in P9np.dll" -ForegroundColor Yellow
}
```

### Check P9rdr.sys for Hardcoded Values

```powershell
$sys = "$env:SystemRoot\System32\drivers\P9rdr.sys"
$bytes = [System.IO.File]::ReadAllBytes($sys)
$text = [System.Text.Encoding]::Unicode.GetString($bytes)

if ($text -match "wsl\.localhost|wsl\$") {
    Write-Host "FOUND: UNC prefix hardcoded in P9rdr.sys" -ForegroundColor Yellow
}
```

---

## 4. Interpretation Guide

### Scenario A: CLSID Hardcoded in P9np.dll

**Evidence:**
- Binary search finds CLSID string in P9np.dll
- ProcMon shows no CLSID registry lookup before COM activation

**Conclusion:** `PROVEN by runtime` - Fork distros will NOT appear in `\\wsl.localhost`

**Workaround:** Fork must use alternative file access (SMB, VirtioFS)

### Scenario B: CLSID Read from Registry

**Evidence:**
- Binary search does NOT find CLSID in P9np.dll
- ProcMon shows registry lookup for CLSID (e.g., under P9NP service key)

**Conclusion:** Fork MIGHT be able to register alternative UNC prefix

**Action:** Investigate registry location for CLSID mapping

### Scenario C: UNC Prefix Hardcoded in P9rdr.sys

**Evidence:**
- Binary search finds `wsl.localhost` in P9rdr.sys

**Conclusion:** `PROVEN by runtime` - Cannot change UNC prefix without driver modification

---

## 5. Expected Results (Based on Architecture Analysis)

Given the architecture:
1. P9rdr.sys is a kernel driver (closed-source)
2. P9np.dll is a network provider DLL (closed-source)
3. COM activation uses well-known CLSID

**Most likely outcome:** Scenario A + C

- `\\wsl.localhost` prefix is hardcoded in P9rdr.sys
- CLSID `{a9b7a1b9-...}` is hardcoded in P9np.dll or uses registry under `P9NP` service
- Fork cannot intercept UNC enumeration without driver/DLL modification

---

## 6. Deliverable Template

After running the verification, fill in:

```markdown
## UNC Enumeration Runtime Proof Results

**Date:** YYYY-MM-DD
**Tester:** [name]
**Windows Version:** [winver output]
**WSL Version:** [wsl --version output]

### ProcMon Capture Summary

| Process | CLSID Queried | Registry Path Accessed |
|---------|---------------|------------------------|
| [fill] | [fill] | [fill] |

### Binary Analysis Results

- P9np.dll contains CLSID string: YES/NO
- P9np.dll contains "Lxss" string: YES/NO
- P9rdr.sys contains "wsl.localhost": YES/NO

### Conclusion

[ ] PROVEN by runtime: Fork distros will NOT appear in \\wsl.localhost
[ ] PROVEN by runtime: Fork CAN register alternative UNC namespace
[ ] INCONCLUSIVE: Additional investigation needed

### Evidence Files

- UNC_CLSID_trace.PML (full ProcMon capture)
- UNC_CLSID_summary.csv (filtered key events)
```

---

## 7. Impact on SxS Architecture

### If UNC is Hardcoded (Expected)

Fork must:
1. NOT rely on `\\wsl.localhost` for file access
2. Document this limitation clearly
3. Provide alternative: SMB share from guest, VirtioFS

### File Access Alternatives for Fork

| Method | Configuration | UNC Path |
|--------|---------------|----------|
| Guest SMB share | `net share wslx=C:\path /grant:Everyone,FULL` | `\\localhost\wslx` |
| VirtioFS | Already supported in WSL | N/A (direct mount) |
| Host share in guest | `/mnt/c/` works | N/A |

---

*This procedure must be executed on a Windows machine with WSL installed to produce runtime evidence.*
