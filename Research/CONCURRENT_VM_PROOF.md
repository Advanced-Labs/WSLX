# Concurrent VM Runtime Proof Procedure

**Date:** 2026-01-28
**Objective:** Prove that two WSL VMs with different Owners can run concurrently without conflicts

---

## 1. Background: HCS Owner Isolation

### Code Evidence

```cpp
// src/windows/common/wslutil.h:47
inline constexpr auto c_vmOwner = L"WSL";
```

The `c_vmOwner` string is used when creating HCS compute systems. For the fork, this becomes `L"WSLX"`.

### Hypothesis

HCS (Host Compute Service) uses the Owner field for:
1. VM grouping/identification
2. vmmem process naming suffix
3. Resource accounting

**Expected behavior:** Two VMs with different Owners should coexist without conflict.

**Risk:** Hidden shared global state in HCS/HNS that causes collision.

---

## 2. Test Environment Requirements

### Prerequisites

- Windows 10/11 with Hyper-V enabled
- Canonical WSL installed and working
- Fork WSLX built and MSI installed (with `SKIPMSIX=1`)
- At least one Linux distro registered in each (can be the same distro)
- Administrator PowerShell

### System Requirements

| Resource | Minimum |
|----------|---------|
| RAM | 8GB (4GB per VM) |
| CPU | 4 cores |
| Disk | 10GB free |

---

## 3. Test Procedure

### Step 1: Baseline State

```powershell
# Ensure clean state
wsl --shutdown
# If fork client exists:
# wslx --shutdown

Start-Sleep -Seconds 3

# Capture baseline
Write-Host "=== Baseline State ===" -ForegroundColor Cyan

Write-Host "`nHCS Compute Systems:" -ForegroundColor Yellow
hcsdiag list
# Expected: Empty or only non-WSL systems

Write-Host "`nHNS Networks:" -ForegroundColor Yellow
hnsdiag list networks | Select-String -Pattern "WSL|WSLX|b95d0c5e|790e58b4"
# Expected: No WSL networks

Write-Host "`nvmmem Processes:" -ForegroundColor Yellow
Get-Process vmmem* -ErrorAction SilentlyContinue | Format-Table Name, Id, WorkingSet64
# Expected: None
```

### Step 2: Start Canonical WSL VM

```powershell
Write-Host "`n=== Starting Canonical WSL ===" -ForegroundColor Cyan

# Start WSL with a simple command that keeps VM alive
$canonicalJob = Start-Job -ScriptBlock {
    wsl -d Ubuntu -e bash -c "echo 'Canonical VM running'; sleep 120"
}

# Wait for VM to boot
Start-Sleep -Seconds 5

Write-Host "`nHCS after canonical boot:" -ForegroundColor Yellow
$hcsCanonical = hcsdiag list
$hcsCanonical | Select-String -Pattern "WSL"
# Expected: One compute system with Owner=WSL

Write-Host "`nHNS after canonical boot:" -ForegroundColor Yellow
$hnsCanonical = hnsdiag list networks
$hnsCanonical | Select-String -Pattern "WSL"
# Expected: WSL or "WSL (Hyper-V firewall)" network
```

### Step 3: Start Fork WSLX VM (Concurrent)

```powershell
Write-Host "`n=== Starting Fork WSLX (Concurrent) ===" -ForegroundColor Cyan

# Start fork VM
# NOTE: Replace 'wslx' with actual fork client name
# If using COM directly, use appropriate activation method
$forkJob = Start-Job -ScriptBlock {
    wslx -d Ubuntu -e bash -c "echo 'Fork VM running'; sleep 120"
}

# Wait for second VM to boot
Start-Sleep -Seconds 5

Write-Host "`nHCS with both VMs:" -ForegroundColor Yellow
$hcsBoth = hcsdiag list
$hcsBoth
# Expected: TWO compute systems - Owner=WSL and Owner=WSLX

Write-Host "`nHNS with both networks:" -ForegroundColor Yellow
$hnsBoth = hnsdiag list networks
$hnsBoth | Select-String -Pattern "WSL|WSLX"
# Expected: Both "WSL" and "WSLX" networks (or firewall variants)

Write-Host "`nvmmem Processes:" -ForegroundColor Yellow
Get-Process vmmem* | Format-Table Name, Id, WorkingSet64
# Expected: Two vmmem processes (may have suffix if supported)
```

### Step 4: Verify Independence

```powershell
Write-Host "`n=== Independence Verification ===" -ForegroundColor Cyan

# Test 1: Shutdown canonical, verify fork survives
Write-Host "`nShutting down canonical WSL..." -ForegroundColor Yellow
wsl --shutdown
Start-Sleep -Seconds 3

Write-Host "HCS after canonical shutdown:" -ForegroundColor Yellow
hcsdiag list | Select-String -Pattern "WSL|WSLX"
# Expected: Only WSLX compute system remains

Write-Host "HNS after canonical shutdown:" -ForegroundColor Yellow
hnsdiag list networks | Select-String -Pattern "WSL|WSLX"
# Expected: WSLX network remains, WSL network may be removed

# Test 2: Verify fork is still running
Write-Host "`nFork VM status:" -ForegroundColor Yellow
wslx --list --running
# Expected: Shows running distro

# Test 3: Restart canonical while fork running
Write-Host "`nRestarting canonical while fork runs..." -ForegroundColor Yellow
wsl -d Ubuntu -e /bin/true
Start-Sleep -Seconds 3

Write-Host "HCS with both again:" -ForegroundColor Yellow
hcsdiag list
# Expected: Both WSL and WSLX compute systems
```

### Step 5: Cleanup

```powershell
Write-Host "`n=== Cleanup ===" -ForegroundColor Cyan

wsl --shutdown
wslx --shutdown
Start-Sleep -Seconds 3

# Remove background jobs
Stop-Job $canonicalJob -ErrorAction SilentlyContinue
Stop-Job $forkJob -ErrorAction SilentlyContinue
Remove-Job $canonicalJob, $forkJob -ErrorAction SilentlyContinue

Write-Host "Final state:" -ForegroundColor Yellow
hcsdiag list
hnsdiag list networks | Select-String -Pattern "WSL|WSLX"
# Expected: No WSL/WSLX systems or networks
```

---

## 4. Expected Results

### Success Criteria

| Check | Expected | Pass/Fail |
|-------|----------|-----------|
| Both VMs boot successfully | Yes | [ ] |
| HCS shows two compute systems | Owner=WSL, Owner=WSLX | [ ] |
| HNS shows two networks | WSL, WSLX (or firewall variants) | [ ] |
| Canonical shutdown doesn't kill fork | Fork survives | [ ] |
| Fork shutdown doesn't kill canonical | Canonical survives | [ ] |
| Both can restart independently | Yes | [ ] |

### HCS Output Format

```
Id: <guid>
Name: <name>
Owner: WSL                    <-- This field distinguishes VMs
State: Running
...

Id: <guid>
Name: <name>
Owner: WSLX                   <-- Fork uses different Owner
State: Running
```

### HNS Output Format

```
Name : WSL (Hyper-V firewall)
ID   : 790E58B4-7939-4434-9358-89AE7DDBE87E
...

Name : WSLX (Hyper-V firewall)
ID   : <fork-guid>             <-- Fork uses different GUID
...
```

---

## 5. Failure Scenarios

### Scenario A: Second VM Fails to Start

**Symptom:** `wslx` command hangs or errors

**Possible causes:**
1. HNS network GUID collision (forgot to change)
2. HvSocket port collision
3. Service not started

**Diagnosis:**
```powershell
# Check service status
sc query WSLXService

# Check event logs
Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='Hyper-V*'} -MaxEvents 20

# Check HNS errors
hnsdiag list networks -ErrorAction SilentlyContinue
```

### Scenario B: First VM Killed When Second Starts

**Symptom:** Canonical WSL terminates when fork starts

**Possible causes:**
1. Same VM Owner (forgot to change `c_vmOwner`)
2. HCS hidden singleton

**Diagnosis:**
```powershell
# Verify Owner strings are different
# Check wslutil.h in both builds
strings "C:\Program Files\WSL\wslservice.exe" | Select-String "WSL"
strings "C:\Program Files\WSLX\wslxservice.exe" | Select-String "WSLX"
```

### Scenario C: Network Connectivity Issues

**Symptom:** One or both VMs have no network

**Possible causes:**
1. HNS network name collision
2. NAT conflict

**Diagnosis:**
```powershell
# Check network adapters in each VM
wsl -e ip addr
wslx -e ip addr

# Check host NAT
Get-NetNat
```

---

## 6. Deliverable Template

```markdown
## Concurrent VM Proof Results

**Date:** YYYY-MM-DD
**Tester:** [name]
**Windows Version:** [winver]
**Canonical WSL Version:** [wsl --version]
**Fork WSLX Version:** [wslx --version]

### HCS Output (Both Running)

```
[paste hcsdiag list output]
```

### HNS Output (Both Running)

```
[paste hnsdiag list networks output]
```

### vmmem Processes

```
[paste Get-Process vmmem* output]
```

### Independence Test Results

| Test | Result |
|------|--------|
| Canonical shutdown, fork survives | PASS/FAIL |
| Fork shutdown, canonical survives | PASS/FAIL |
| Concurrent restart | PASS/FAIL |

### Conclusion

[ ] PROVEN by runtime: HCS Owner isolation works correctly
[ ] FAILED: [describe failure mode]
[ ] PARTIAL: [describe limitations]

### Evidence Files

- hcs_both_running.txt
- hns_both_networks.txt
- event_log_excerpt.txt (if failures)
```

---

## 7. Implications for SxS

### If Test Passes

- HCS Owner field provides sufficient isolation
- Update evidence label to `PROVEN by runtime`
- Proceed with SxS implementation

### If Test Fails

- Document failure mode
- Investigate HCS/HNS hidden singletons
- May need additional isolation mechanisms

---

*This procedure must be executed on a Windows machine with both canonical WSL and fork WSLX installed.*
