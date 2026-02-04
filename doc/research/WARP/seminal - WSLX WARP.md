# WSLX WARP  
**Project:** WSLX WARP (WSLX *Arbitrary Runtime Platform*)  
**Status:** Concept paper (seminal)  
**Date:** 2026-01-29 (America/Montreal)  

---

## Abstract

WSLX WARP reframes a WSL2-style “utility VM” as a **generalized, host-managed micro‑VM runtime** capable of booting **nonstandard kernels** (including kernels that are *not Linux*) while retaining the minimum invariants needed for: (1) the **host VM lifecycle**, (2) **host↔guest control/IO**, and (3) **paravirtual devices** (storage/network/IPC).  

The central thesis is that once the host-side manager (WSLXService) and the guest-side init/control agent are under our control, “Linux” becomes *one compatibility personality* among multiple possible guest OS personalities. WARP supports two primary tracks:

1. **Linux‑personality track**: radically change kernel internals while preserving the Linux userspace ABI (syscalls + core semantics), enabling existing WSL distributions to run.
2. **New‑OS track**: define a new ABI and “distribution” format for a non‑Linux OS running inside WSLX.

---

## 1. Baseline: What WSL2 actually is (relevant parts only)

### 1.1 Shared kernel, per‑distro userspace
In WSL2, the **kernel is provided by WSL** and **shared across installed distributions**. Distros are primarily **root filesystem + metadata**, not “full VM images with their own kernels.”  
- Microsoft docs: WSL configuration & custom kernel via `.wslconfig` (global) and `wsl.conf` (per-distro).  
  - https://learn.microsoft.com/en-us/windows/wsl/wsl-config  
- Kernel build instructions (x86_64 example) in WSL2 Linux kernel repo:  
  - https://github.com/microsoft/WSL2-Linux-Kernel  

### 1.2 Boot/control pipeline (WSL2 reference model)
WSL2 uses a Linux direct-boot style flow where:
- A kernel is booted in a lightweight Hyper‑V VM
- A first userspace component (`mini_init`) performs early setup and receives config messages from the host service
- A distribution init (`init`) performs distro initialization and starts the user session

Primary public technical references:
- WSL boot process: https://wsl.dev/technical-documentation/boot-process/  
- `mini_init`: https://wsl.dev/technical-documentation/mini_init/  
- Distribution `init`: https://wsl.dev/technical-documentation/init/  
- WSL overview of components/protocols: https://wsl.dev/technical-documentation/  

### 1.3 “Distribution packaging” (modern WSL)
A modern WSL distribution can be distributed as a **`.wsl`** artifact consisting of:
- a root filesystem (tar)
- a manifest entry (metadata)  
Doc: https://learn.microsoft.com/en-us/windows/wsl/build-custom-distro  

(Store-era packaging also exists via a launcher app; reference implementation:)
- https://github.com/microsoft/WSL-DistroLauncher  

### 1.4 WSL is now open source
WSL host-side code is available:
- Repo: https://github.com/microsoft/WSL  
- Overview: https://learn.microsoft.com/en-us/windows/wsl/opensource  
- Announcement: https://blogs.windows.com/windowsdeveloper/2025/05/19/the-windows-subsystem-for-linux-is-now-open-source/  

> WSLX assumes we already have an identity-isolated SxS fork of these components.

---

## 2. WARP motivation (why this exists)

WSLX WARP targets a capability gap: **booting and operating “guest kernels” that are not constrained to Linux’s architecture**, while still leveraging Windows’ mature VM hosting (Hyper‑V utility VM) and WSL’s integration surfaces.

Typical motivations:
- Research OS kernels that want *WSL‑class* Windows integration without being Linux.
- Experiments requiring deep kernel modifications where “staying Linux” is an unnecessary constraint.
- A platform for “kernel personalities” (Linux ABI / New ABI) with a shared host manager.
- Controlled experiments on snapshot/restore and VM-level lifecycle, without adopting a full hypervisor stack from scratch.

---

## 3. Core thesis: WSLX can become a **guest runtime**, not a Linux-only subsystem

### 3.1 The key observation
If we control:
- **Host**: WSLXService (VM creation, lifecycle, configuration, IO relay)
- **Guest**: early init in initramfs and the “distribution init” layer

…then the “must be Linux” assumption dissolves.  

In stock WSL2, those layers are Linux-oriented (`mini_init`, `init`, Linux syscalls, Linux filesystem conventions). In WARP, these become *replaceable contracts*.

### 3.2 WARP’s minimum invariant set (conceptual)
A non-Linux kernel can work under WSLX if the overall system still provides:

1. **Bootability**  
   A guest kernel must be bootable through the VM creation pathway WSLX uses (direct-kernel boot and initrd, or an alternate boot path if WSLXService implements it).

2. **Host↔guest control plane**  
   A bi-directional protocol channel for lifecycle + process launch + IO relay (stdin/stdout/stderr, exit codes).

3. **Storage**  
   A way to provision and mount a per-“distro” image as the guest’s root filesystem (or equivalent).

4. **Optional but typical**  
   Networking, time sync, memory reporting, filesystem sharing, GPU/GUI integration—each can be reintroduced incrementally.

These are *platform invariants*, not “Linux invariants.”

---

## 4. WARP architecture (concept-level)

### 4.1 Components and names (WARP terminology)

- **WSLXService**: host-side manager (fork of WSL service)
- **KernelX**: guest kernel (Linux-personality or new-OS)
- **AgentX**: guest early userspace agent (replaces/extends `mini_init`/`init` roles)
- **ImageX**: the “distribution image” for KernelX (root filesystem + metadata, plus optional modules/resources)

### 4.2 Reference flow (sequence)

1. **Host** creates a utility VM (via the existing WSL2 hosting pathway).
2. **Host** supplies `{KernelX, initrdX, cmdlineX}`.
3. **KernelX** boots.
4. **AgentX** starts from initrdX.
5. **AgentX** establishes control-plane connectivity to WSLXService.
6. **AgentX** requests/receives configuration (identities, mounts, hostname, policies, integration toggles).
7. **AgentX** attaches/mounts ImageX as the guest root.
8. **AgentX** launches the user session / processes per WSLXService requests.

> This mirrors the WSL2 structure, but makes the guest stack replaceable.

---

## 5. Two compatibility tracks

### Track A — **Linux‑personality KernelX** (Linux ABI preserved)
Goal: Run existing WSL distros and Linux apps “as-is,” but allow drastic kernel changes internally.

**Constraints**:
- Must preserve Linux userspace ABI: syscall surface + core semantics expected by distros.
- Must preserve enough Linux VFS/process model that standard userland functions.

**Benefits**:
- Drop-in compatibility with Ubuntu/Debian/etc.
- Can still explore deep kernel refactors (scheduler, VM subsystem, storage drivers, etc.) as long as the ABI contract holds.

**Interpretation**:
- “Not Linux internally” is plausible, but “Linux-compatible” remains a true statement at the ABI boundary.

### Track B — **New‑OS KernelX** (new ABI)
Goal: KernelX is a different OS kernel with its own ABI and userland.

**Constraints**:
- Must meet WARP platform invariants (boot, control plane, storage, etc.).
- Must define a new userland + tooling story.

**Benefits**:
- Maximum architectural freedom.
- WSLX becomes a Windows-integrated *micro‑VM OS runtime*.

**Interpretation**:
- This is not “a new WSL distro”; it is **a new OS platform** hosted by WSLX.

---

## 6. ImageX (“distro”) model under WARP

### 6.1 ImageX under Track A (Linux-personality)
ImageX can reuse existing WSL distribution patterns:
- rootfs tar import (`wsl --import`) equivalents
- `.wsl` packaging (rootfs tar + manifest)  
  - https://learn.microsoft.com/en-us/windows/wsl/build-custom-distro  

Kernel remains global (or WSLX-global), while each distro is a separate rootfs.

### 6.2 ImageX under Track B (New-OS)
ImageX becomes an OS-specific package:
- root filesystem (or equivalent object store)
- manifest describing:
  - required KernelX version / features
  - init entrypoints and service roles
  - integration toggles (fs-share, net, GUI, GPU)
  - security policy and signing requirements

WARP can still *reuse* the `.wsl` “two-part packaging” idea (filesystem + manifest) even if the contents are not Linux.

---

## 7. Integration surfaces (what WARP can optionally provide)

Each surface can be a modular “integration contract” between WSLXService and AgentX:

1. **Filesystem sharing**  
   WSL2 historically used Plan9/9P-like roles for host↔guest file sharing; WARP can keep, replace, or redefine this layer depending on performance/security goals.  
   (WSL kernel release notes and WSL dev docs discuss these integration elements in the current system.)

2. **Networking**  
   NAT vs bridged, localhost forwarding semantics, name resolution, and host↔guest sockets.  
   WARP should define an explicit network contract so a non-Linux kernel can still integrate.

3. **Graphics / WSLg**  
   Optional. For non-Linux kernels, a new protocol may be needed unless Linux-compatible userspace is retained.

4. **GPU compute / acceleration**  
   Optional. For WSL2, GPU acceleration is tied to specific guest drivers and host orchestration; WARP can treat this as an optional integration module.

5. **Process launching model**  
   WSL’s UX depends on “launch a process in the distro and stream stdio.”  
   Under Track B, “process” might mean something else; WARP should define the external launch abstraction.

---

## 8. Security and trust model (WARP-specific concerns)

WARP amplifies the need for explicit trust boundaries:

- **Kernel trust**: KernelX is code execution at the highest privilege inside the guest.
- **Image trust**: ImageX supplies userland or runtime content.
- **Host boundary**: WSLXService is a privileged host component.

Recommended principles:
- Signed KernelX + signed ImageX manifests.
- Versioned contracts between host and AgentX.
- Minimal attack surface on the control plane (authenticated channel, least privilege).
- Isolation defaults: if an integration surface is not required, it should be off.

---

## 9. Tooling & developer workflow (minimal)

### 9.1 KernelX lifecycle
- Build KernelX (and modules if applicable).
- Configure WSLX to point to KernelX (WSL has a `.wslconfig` kernel option; WSLX can replicate/extend this mechanism).  
  - https://learn.microsoft.com/en-us/windows/wsl/wsl-config  
- Boot and validate the control plane.

### 9.2 AgentX lifecycle
- Build AgentX and initrdX.
- Ensure AgentX can:
  - mount ImageX
  - speak WSLX protocol
  - launch processes/services

### 9.3 ImageX lifecycle
- Produce filesystem content + manifest.
- Import/register into WSLX (reusing `.wsl` packaging ideas or a WSLX-native format).

---

## 10. R&D agenda (phased)

### Phase 0 — “WARP skeleton”
- Boot KernelX + initrdX in WSLX.
- Establish a minimal control plane (ping/config exchange).
- Mount a basic ImageX and start a trivial process with IO relay.

### Phase 1 — “Linux-personality viability”
- Run a minimal Linux userspace on KernelX (Track A).
- Validate that deep kernel changes remain compatible at the syscall boundary.
- Characterize which Linux semantics are “hard” vs “optional” for target workloads.

### Phase 2 — “New-OS track prototype”
- Define a minimal new ABI + userland for KernelX (Track B).
- Implement AgentX launch model and basic tooling.
- Add one integration surface (filesystem share or networking) end-to-end.

### Phase 3 — “Modular integration”
- Make integration surfaces pluggable and policy-governed.
- Add incremental features (GUI/GPU only if required).

---

## 11. Open questions (research-grade)

1. **Boot method constraints**: How far can WSLX deviate from Linux-style direct boot while staying within HCS/utility-VM assumptions?  
2. **Control plane transport**: Which host↔guest IPC transport is simplest to keep stable across kernels (vsock/hvsocket role equivalents)?  
3. **Filesystem sharing**: Which protocol provides the best performance/security tradeoffs for WARP (keep WSL-style approach vs replace)?  
4. **Image format**: Should ImageX standardize on “rootfs + manifest” (WSL `.wsl` model) for both tracks?  
5. **Compatibility promises**: For Track A, which Linux subsystems are required for the targeted distros/workloads (containers, systemd, etc.)?  
6. **Update strategy**: How to update KernelX and ImageX safely without breaking running instances?

---

## 12. Non-goals (explicit)

- Recreating a general-purpose hypervisor UI/UX (Hyper‑V Manager equivalence).
- Supporting arbitrary third-party OS kernels without a WARP control plane implementation.
- “Bare metal Linux host ↔ guest Windows VM” scenarios (out of scope).

---

## References (primary)
- WSL boot process & internal components: https://wsl.dev/technical-documentation/boot-process/  
- `mini_init`: https://wsl.dev/technical-documentation/mini_init/  
- `init`: https://wsl.dev/technical-documentation/init/  
- WSL overview: https://wsl.dev/technical-documentation/  
- WSL configuration (`.wslconfig`, custom kernel): https://learn.microsoft.com/en-us/windows/wsl/wsl-config  
- Build custom WSL distro (`.wsl`): https://learn.microsoft.com/en-us/windows/wsl/build-custom-distro  
- WSL2 Linux kernel source & build instructions: https://github.com/microsoft/WSL2-Linux-Kernel  
- WSL open source overview: https://learn.microsoft.com/en-us/windows/wsl/opensource  
- WSL source repo: https://github.com/microsoft/WSL  
- Announcement: https://blogs.windows.com/windowsdeveloper/2025/05/19/the-windows-subsystem-for-linux-is-now-open-source/  
- Distro launcher reference: https://github.com/microsoft/WSL-DistroLauncher  

---
