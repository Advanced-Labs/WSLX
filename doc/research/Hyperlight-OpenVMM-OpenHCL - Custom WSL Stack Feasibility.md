# Hyperlight, OpenVMM, OpenHCL: Custom WSL Stack Feasibility
**Project:** WSLX Research
**Status:** Research document
**Date:** 2026-02-07

---

## Executive Summary

This document evaluates the feasibility of building a **custom WSL-like system** using Microsoft's open-source Rust virtualization projects (Hyperlight, OpenVMM, OpenHCL) to replace the proprietary HCS/HCN stack. It synthesizes analysis from another AI's "Hyper-WSL" proposal, corrects critical misconceptions, and identifies the viable architectural paths.

**Key finding:** The other AI's proposal is **directionally correct but misidentifies the vehicle**. Hyperlight cannot boot a Linux kernel — it is a function-level micro-VM sandbox with no device model. **OpenVMM is the correct tool** — it already boots Linux kernels, has VMBus and virtio support, and runs on Windows (WHP), Linux (KVM), and macOS (HVF). The real question is not "can Hyperlight boot WSL" but rather "can OpenVMM replace HCS as the WSL orchestration layer."

---

## 1. Reality Check: What Hyperlight Actually Is (and Isn't)

### What the other AI said

> "Hyperlight is not merely a Wasm sandbox; it is a minimalist vCPU orchestrator. By 'stitching' the open-source OpenVMM protocol implementations to the WSL2 Kernel, it is possible to create a high-performance, hardware-accelerated Linux sandbox..."

### What Hyperlight actually is

Hyperlight is a **function-level micro-VM library** that creates an isolated vCPU + linear memory slab. It is far more constrained than the other AI's report suggests:

| Capability | Hyperlight Reality |
|---|---|
| Device model | **None.** No virtio, no VMBus, no PCI, no MMIO, no DMA |
| Interrupt support | **None.** No SynIC, no APIC, no timer interrupts |
| Boot capability | **None.** No BIOS, no UEFI, no Linux Boot Protocol |
| vCPU count | **1 only** |
| Max memory | **1 GB** (single PML4E page table constraint) |
| Guest I/O | **outb port I/O only** (triggers VM exit, caught by host) |
| Guest type | Custom `no_std` Rust/C binaries or Wasm (via Hyperlight-Wasm) |
| OS support | **No guest OS of any kind** |

Hyperlight's entire value proposition is **sub-millisecond cold start** (~0.9ms). It achieves this by eliminating everything a VMM normally provides. It "creates a linear slice of memory and assigns it a virtual CPU" — nothing more.

### What would be needed to boot a Linux kernel in Hyperlight

To boot even a minimal Linux kernel, Hyperlight would need:

1. **BIOS/UEFI or Linux Boot Protocol shim** — Set up GDT, page tables, zero page, initrd loading, command line, jump to kernel entry point
2. **Interrupt controller emulation** — LAPIC + IOAPIC at minimum; SynIC for Hyper-V enlightened kernels
3. **Timer device** — HPET or LAPIC timer (Linux kernel panics without a timer)
4. **Storage device** — virtio-blk or SCSI for root filesystem
5. **Console device** — Serial UART or virtio-console for kernel messages
6. **VMBus or virtio transport** — For the WSL kernel's expected synthetic devices
7. **Multi-vCPU support** — WSL2 default is 2+ vCPUs
8. **Memory beyond 1 GB** — WSL2 default is ~50% of host RAM
9. **Dynamic memory management** — Balloon driver for WSL2's memory reclamation

This is not "extending" Hyperlight — it is **rebuilding OpenVMM from scratch** while using Hyperlight's WHP wrapper. The engineering effort would be enormous with no benefit over using OpenVMM directly.

### Where Hyperlight IS relevant to WSLX

Hyperlight is interesting for WSLX not as a kernel boot mechanism, but for **WARP Track B** scenarios:

- **Ultra-fast function sandboxes** running alongside the WSL VM
- **Micro-VMs for untrusted code execution** (think: sandboxed build steps, plugin evaluation)
- **Wasm execution with hardware isolation** (Hyperlight-Wasm)
- **Nanvix-based POSIX sandboxes** (announced January 2026) — a microkernel inside a Hyperlight VM providing 150+ POSIX syscalls

These complement rather than replace the WSL kernel VM.

---

## 2. OpenVMM: The Actual Vehicle

### Why OpenVMM is the right foundation

OpenVMM (formerly hvlite) is Microsoft's **production VMM** — over 1.5 million Azure VMs run via OpenHCL, which uses OpenVMM as its core engine. It already provides everything the other AI's proposal wants from Hyperlight:

| Requirement | OpenVMM Status |
|---|---|
| Boot Linux kernel | **Yes** — Linux Direct Boot supported |
| VMBus | **Yes** — 13 Rust crates (server, client, ring buffers, relay) |
| virtio-pmem | **Yes** — `virtio_pmem` crate |
| virtio-fs | **Yes** — `virtiofs` crate |
| virtio-9p | **Yes** — `virtio_p9` crate |
| virtio-net | **Yes** — `virtio_net` crate |
| Serial console | **Yes** — `virtio_serial` + UART emulation |
| NVMe | **Yes** — emulated NVMe |
| SynIC | **Yes** — per-backend (WHP, KVM, MSHV) |
| Multi-vCPU | **Yes** — arbitrary vCPU count |
| Dynamic memory | **Yes** — balloon support |
| GPU passthrough | **Partial** — synthetic video, DDA |
| Hyper-V enlightenments | **Yes** — full hv1 emulation (hypercalls, MSRs, SynIC) |
| UEFI boot | **Yes** — mu_msvm firmware |
| Save/restore | **Yes** — state serialization across all devices |
| WHP backend | **Yes** — `virt_whp` crate |
| KVM backend | **Yes** — `virt_kvm` crate |
| macOS backend | **Yes** — `virt_hvf` crate (Apple Silicon) |

### OpenVMM crate architecture relevant to a custom WSL

```
What we'd use from OpenVMM:
━━━━━━━━━━━━━━━━━━━━━━━━━━━

Hypervisor Abstraction Layer (pick your backend):
  vmm_core/virt/           ← HAL traits (Partition, Processor)
  vmm_core/virt_whp/       ← Windows host (WHP API)
  vmm_core/virt_kvm/       ← Linux host (KVM)
  vmm_core/virt_mshv/      ← Azure/MSHV host

VMBus Stack (13 crates):
  vm/devices/vmbus/
    vmbus_core/            ← Protocol definitions
    vmbus_ring/            ← Ring buffer implementation
    vmbus_channel/         ← Channel offers, GPADL management
    vmbus_server/          ← Server-side (offer channels to guest)
    vmbus_client/          ← Client-side (consume host channels)
    vmbus_async/           ← Async wrappers

Virtio Devices:
  vm/devices/virtio/
    virtio/                ← Core VirtioDevice trait, queue management
    virtio_pmem/           ← Persistent memory (Portal transport!)
    virtiofs/              ← Filesystem passthrough
    virtio_p9/             ← Plan 9 file sharing
    virtio_net/            ← Network device
    virtio_serial/         ← Serial console

Storage:
  vm/devices/storage/
    storvsp/               ← VMBus synthetic SCSI (what WSL kernel expects)

Core VM Infrastructure:
  vm/vmcore/
    guestmem/              ← Guest memory management
    memory_range/          ← Memory range types
    vm_resource/           ← Resource resolver
    vm_topology/           ← VM topology definitions

Hyper-V Protocol:
  vm/hv1/
    hvdef/                 ← Hyper-V definitions
    hv1_hypercall/         ← Hypercall interface
    hv1_emulator/          ← Enlightenment emulation (SynIC, timers, etc.)
```

### The hvlite connection in the WSL kernel

Searching the WSLX codebase confirms the WSL kernel is **already designed to work with OpenVMM**:

1. **`CONFIG_VIRTIO_PMEM=y`** in `/Kernel/arch/x86/configs/config-wsl` — virtio-pmem driver compiled into the WSL kernel
2. **Full virtio stack enabled**: `CONFIG_VIRTIO=y`, `CONFIG_VIRTIO_PCI=y`, `CONFIG_VIRTIO_MMIO=y`, `CONFIG_VIRTIO_NET=y`, `CONFIG_VIRTIO_FS=y`, `CONFIG_VIRTIO_CONSOLE=y`, `CONFIG_VIRTIO_BALLOON=y`
3. **Full Hyper-V enlightenments**: VMBus driver (`drivers/hv/vmbus_drv.c`), synthetic NIC (`drivers/net/hyperv/netvsc_drv.c`), synthetic SCSI (`drivers/scsi/storvsc_drv.c`), hvsocket (`net/vmw_vsock/hyperv_transport.c`)
4. **DAX support**: `CONFIG_DAX=y`, `CONFIG_NVDIMM_DAX=y`, `CONFIG_FS_DAX=y` — ready for virtio-pmem DAX mmap

The WSL kernel speaks **both** VMBus synthetic devices (the standard HCS path) and virtio devices (the OpenVMM path). This dual capability means the same kernel binary can boot under either HCS or a custom OpenVMM-based launcher.

"hvlite" appears in our research documents as the former codename for OpenVMM. The naming connection (`hvlite_virtio_pmem` patterns noted) reflects that OpenVMM's virtio-pmem device implementation is the host-side counterpart of the `virtio_pmem.c` driver in the WSL kernel — they were designed together within Microsoft.

---

## 3. Architecture: "WSLX Custom Stack" via OpenVMM

### What we'd replace and what we'd keep

```
Current WSL2/WSLX Stack:                Custom OpenVMM Stack:
━━━━━━━━━━━━━━━━━━━━━━━━               ━━━━━━━━━━━━━━━━━━━━━━

wsl.exe (CLI)                           wslx.exe (CLI) [keep/modify]
    │                                       │
wslservice.exe (C++)                    wslx-vmm (Rust, OpenVMM-based)
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ HCS              │ REPLACE            │ OpenVMM                  │
│ computecore.dll  │ ─────────►         │ virt_whp / virt_kvm      │
│ (closed JSON     │                    │ (open Rust crates,       │
│  schema, opaque) │                    │  full control)           │
└───┬──────────────┘                    └───┬──────────────────────┘
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ HCN              │ REPLACE            │ Custom networking        │
│ computenetwork   │ ─────────►         │ (virtio-net + host TAP   │
│ .dll (opaque)    │                    │  or WinNAT integration)  │
└───┬──────────────┘                    └───┬──────────────────────┘
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ VMBus VSPs       │ REPLACE            │ OpenVMM VMBus server     │
│ (in-kernel,      │ ─────────►         │ (Rust user-space,        │
│  closed source)  │                    │  vmbus_server crate)     │
└───┬──────────────┘                    └───┬──────────────────────┘
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ p9rdr.sys        │ REPLACE            │ OpenVMM virtio_p9 or     │
│ (closed 9P       │ ─────────►         │ virtiofs crate           │
│  redirector)     │                    │ (open source, both)      │
└───┬──────────────┘                    └───┬──────────────────────┘
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ Windows          │ SAME               │ Windows Hypervisor       │
│ Hypervisor       │ ═══════════        │ (same hvix64.exe)        │
│ (hvix64.exe)     │                    │ OR Linux KVM             │
└──────────────────┘                    └──────────────────────────┘
    │                                       │
┌───▼──────────────┐                    ┌───▼──────────────────────┐
│ WSL Linux Kernel │ SAME               │ WSL Linux Kernel         │
│ (6.6.x LTS)     │ ═══════════        │ (same kernel, both VMBus │
│                  │                    │  and virtio work)        │
└──────────────────┘                    └──────────────────────────┘
```

### What we gain

1. **Full control over the VM lifecycle** — No opaque JSON schemas, no `computecore.dll` black box
2. **Custom device composition** — Mix VMBus synthetic devices with virtio devices freely
3. **virtio-pmem for Portal** — Zero-copy shared memory via DAX, controlled from Rust host code
4. **Cross-platform** — Same Rust VMM code runs on Windows (WHP) and Linux (KVM)
5. **Multi-VM architectures** — Multiple partitions with shared resources (via WHP `WHvMapGpaRange` on Windows, `KVM_SET_USER_MEMORY_REGION` on Linux)
6. **Snapshot/restore** — OpenVMM has state serialization for all devices; not dependent on HCS
7. **Debugging and instrumentation** — Full visibility into VM state, device emulation, ring buffer contents

### What we lose (and must replace)

1. **Networking (HCN)** — Must implement our own. Options:
   - virtio-net + host TAP adapter + iptables/WinNAT for NAT
   - On Windows: potentially still use HCN for NAT rules while bypassing HCS for the VM
   - On Linux: standard bridge/NAT networking

2. **`\\wsl.localhost\` filesystem integration** — The `p9rdr.sys` driver is closed-source and only works with HCS-managed VMs. Must replace with:
   - Custom Windows filesystem driver (significant effort)
   - Network share (SMB) from within the VM (simpler but less integrated)
   - FUSE-based approach on Windows (via WinFsp)

3. **Windows integration components** — `wsl.exe` commands, Windows Terminal integration, `wslpath`, etc. The open-sourced WSL code helps here but ties into `wslservice.exe` which we're replacing.

4. **GPU-P (dxgkrnl passthrough)** — The WSL kernel's `dxgkrnl` module talks to the host via VMBus GUID `HV_GPUP_DXGK_*`. OpenVMM would need to relay this to the Windows GPU stack. This is complex but OpenVMM already handles synthetic video.

5. **Automatic updates** — WSL auto-updates via Microsoft Store. A custom stack requires its own update mechanism.

---

## 4. Cross-Platform Feasibility: Windows Host + Linux Host

### The core question

> "Could this new thing run on both a Windows host and a Linux host without rewriting tons of things?"

### Answer: Yes, with caveats

OpenVMM's hypervisor abstraction layer (HAL) already handles this. The same device emulation code runs on all platforms — only the lowest layer changes:

```
Shared code (platform-independent):
┌─────────────────────────────────────────────────────┐
│  wslx-vmm (our custom launcher)                     │
│  ┌───────────────────────────────────────────────┐  │
│  │  OpenVMM device crates                        │  │
│  │  - vmbus_server (VMBus)                       │  │
│  │  - virtio_pmem (Portal shared memory)         │  │
│  │  - virtiofs / virtio_p9 (filesystems)         │  │
│  │  - virtio_net (networking)                    │  │
│  │  - storvsp (synthetic SCSI)                   │  │
│  │  - hv1_emulator (Hyper-V enlightenments)      │  │
│  └───────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────┐  │
│  │  vmm_core/virt (HAL traits)                   │  │
│  └─────────────┬──────────────┬──────────────────┘  │
└────────────────┼──────────────┼─────────────────────┘
                 │              │
    ┌────────────▼───┐   ┌─────▼─────────────┐
    │  virt_whp       │   │  virt_kvm          │
    │  (Windows)      │   │  (Linux)           │
    │  WHP API        │   │  /dev/kvm ioctls   │
    └────────────────┘   └───────────────────┘
```

### Platform feature matrix

| Feature | Windows (WHP) | Linux (KVM) | Notes |
|---|---|---|---|
| Boot WSL kernel | Yes | Yes | Same kernel image, same boot protocol |
| VMBus | Yes | Yes | OpenVMM emulates VMBus on all platforms |
| Hyper-V enlightenments | Yes (native) | Yes (emulated by OpenVMM) | `hv1_emulator` crate provides full emulation |
| virtio-pmem (Portal) | Yes | Yes | Same device crate |
| virtio-fs / 9P | Yes | Yes | 9P needs `lxutil.dll` on Windows |
| virtio-net | Yes | Yes | Different backend adapters per platform |
| Networking (NAT) | WinNAT or manual | iptables/nftables | Platform-specific host networking |
| `\\wsl.localhost\` | Not without p9rdr.sys | N/A (use native mounts) | Windows-specific limitation |
| GPU passthrough | Complex (dxgkrnl) | Different (GPU-P not available) | Fundamentally platform-specific |
| hvsocket | Yes (AF_HYPERV) | Mapped to vsock (AF_VSOCK) | OpenVMM handles translation |
| Shared memory (multi-VM) | `WHvMapGpaRange` | `KVM_SET_USER_MEMORY_REGION` | Same concept, different API |
| Inter-VM shared memory | Yes (WHP) | Yes (ivshmem on KVM!) | **KVM is actually better** for this |

### Critical insight: KVM gives us more, not less

On a Linux host, KVM provides capabilities that WHP lacks:

1. **ivshmem** — Direct inter-VM shared memory via PCI BAR. No VMBus proxy needed, no RDMA hardware needed. Sub-microsecond latency, true zero-copy. This is exactly what we discussed in the previous research document as being missing from Hyper-V.

2. **vhost-user** — User-space device backends with zero-copy. The guest's virtio-net packets go directly to a host user-space process via shared memory, bypassing the kernel.

3. **VFIO/GPU passthrough** — More mature and flexible than Hyper-V's GPU-P for some scenarios.

4. **Nested virtualization** — Run KVM inside KVM with reasonable performance. WSLX on a Linux host inside another VM.

For the **distributed kernel** research direction (from the previous document), a Linux host with KVM + ivshmem is actually the **most capable platform**, not the least.

### What would need to be platform-specific

| Component | Windows-specific | Linux-specific |
|---|---|---|
| Host networking | WinNAT rules / Hyper-V vSwitch | iptables/nftables, bridge setup |
| Filesystem integration | WinFsp/custom driver for `\\wslx\` | Native mounts, NFS, or SMB |
| GPU passthrough | dxgkrnl relay (complex) | VFIO passthrough (different) |
| Service management | Windows Service / `wslservice.exe` | systemd unit |
| Auto-start | Windows Task Scheduler / service | systemd / socket activation |
| Process bridge | Win32 ↔ Linux process mapping | N/A (already on Linux) |

The core VMM code (80%+ of the codebase) would be shared. Platform-specific code would be limited to host integration (networking, filesystem mounts, system services).

---

## 5. The WSL Distro Container Model

### How WSL distros work today

WSL2 runs a **single shared VM** with all distros as filesystem/process namespaces:

```
WSL2 Utility VM (single VM for all distros):
┌────────────────────────────────────────────────────┐
│  Microsoft Linux Kernel 6.6.x                      │
│                                                    │
│  ┌─────────────┐  ┌─────────────┐  ┌───────────┐  │
│  │ Ubuntu      │  │ Debian      │  │ Fedora    │  │
│  │ (ext4 VHD)  │  │ (ext4 VHD)  │  │ (ext4 VHD)│  │
│  │ /           │  │ /           │  │ /         │  │
│  │ Separate    │  │ Separate    │  │ Separate  │  │
│  │ PID/mount   │  │ PID/mount   │  │ PID/mount │  │
│  │ namespaces  │  │ namespaces  │  │ namespaces│  │
│  └─────────────┘  └─────────────┘  └───────────┘  │
│                                                    │
│  Shared: network namespace, kernel, /tmp, etc.     │
│  init → manages distro mounts + process isolation  │
└────────────────────────────────────────────────────┘
```

Each distro is:
1. An **ext4 VHD** file on the Windows filesystem
2. Mounted as a block device inside the VM
3. Given its own PID namespace and mount namespace
4. Managed by the WSL init process

### Replicating this with OpenVMM

OpenVMM can replicate this architecture:

1. **VM creation**: OpenVMM boots the WSL kernel via Linux Direct Boot
2. **VHD mounting**: Each distro's ext4 VHD is attached as a virtio-blk device or SCSI LUN via storvsp
3. **Init process**: We'd reuse or adapt the open-sourced WSL init (`src/linux/init/`) which handles:
   - Distro mount/namespace setup
   - HV socket → init protocol
   - Plan9/VirtioFS mount for Windows drives
   - Process factory for `wsl.exe` → Linux process creation
4. **Container-like isolation**: Linux namespaces (PID, mount, UTS, IPC) — kernel-native, no dependency on HCS

The open-sourced WSL init code in `src/linux/init/` speaks a protocol over hvsocket ports (50000-50005, or 51000-51005 in WSLX). OpenVMM supports hvsocket via its VMBus stack, so the init protocol would work unmodified.

### Alternative: One VM per distro

An OpenVMM-based stack could also support a **one VM per distro** model:

```
OpenVMM Distro-per-VM:
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ VM: Ubuntu   │  │ VM: Debian   │  │ VM: Fedora   │
│ WSL Kernel   │  │ WSL Kernel   │  │ WSL Kernel   │
│ ext4 root    │  │ ext4 root    │  │ ext4 root    │
│ virtio-pmem  │◄─┼─ SHARED ────►│  │ virtio-pmem  │
│ (Portal)     │  │ (Portal)     │  │ (Portal)     │
└──────────────┘  └──────────────┘  └──────────────┘
       ▲                 ▲                 ▲
       └─────── OpenVMM (single process) ──┘
                    │
             WHP or KVM backend
```

Benefits of per-distro VMs:
- **True isolation** — each distro gets its own kernel, can load different modules
- **Independent snapshots** — snapshot one distro without affecting others
- **Per-distro resource limits** — dedicated memory and CPU
- **Shared memory via Portal** — virtio-pmem regions mapped into multiple VMs from same host file

This is impractical with HCS (each VM = full overhead) but feasible with OpenVMM's lightweight VM creation.

---

## 6. Feasibility by Deployment Scenario

### Scenario A: Windows host, replacing HCS (primary WSLX use case)

**Feasibility: HIGH**

| Component | Solution | Effort |
|---|---|---|
| Hypervisor | WHP via `virt_whp` | Trivial (OpenVMM does this) |
| VM lifecycle | OpenVMM partition management | Medium (replace `wslservice.exe`) |
| Boot | Linux Direct Boot | Trivial (OpenVMM supports this) |
| VMBus devices | `vmbus_server` + `storvsp` | Low (OpenVMM has it) |
| Filesystem sharing | `virtio_p9` or `virtiofs` | Low (OpenVMM has both) |
| Portal shared memory | `virtio_pmem` | Low (OpenVMM has it; kernel has driver) |
| Networking | `virtio_net` + WinNAT | Medium (need WinNAT integration) |
| hvsocket | VMBus hvsock relay | Low (OpenVMM has it) |
| `\\wslx\` mount | WinFsp + custom provider | Medium-High (or skip initially) |
| GPU passthrough | dxgkrnl relay | High (complex, VMBus GUID relay) |
| WSL init | Reuse open-sourced init code | Low (same protocol, same kernel) |

**Estimated timeline**: A working prototype (boot kernel, mount distro, run shell) could be built in weeks. Full feature parity with WSL2 would take months, primarily due to networking and Windows integration.

### Scenario B: Windows host, alongside full Hyper-V

**Feasibility: HIGH (slightly easier)**

If full Hyper-V is installed, we get additional infrastructure:
- Hyper-V virtual switch (no need to implement our own networking)
- VMMS for VM management (though we'd bypass this to use OpenVMM directly)
- Integration services (KVP, heartbeat, time sync)

However, the interesting question is whether we need full Hyper-V at all. The "Virtual Machine Platform" optional component (what WSL2 uses) is sufficient for WHP, and OpenVMM handles everything else.

### Scenario C: Linux host via KVM

**Feasibility: HIGH — and arguably the most interesting**

On a Linux host, we'd use OpenVMM with the KVM backend (`virt_kvm`). This is architecturally identical to Scenario A but with KVM instead of WHP.

**What works:**

1. **WSL kernel boots unchanged** — The WSL kernel has both Hyper-V enlightenments AND standard virtio/KVM support. OpenVMM's `hv1_emulator` can provide Hyper-V enlightenment emulation even on KVM, so the kernel's VMBus drivers work.

2. **All virtio devices work** — virtio-pmem, virtio-fs, virtio-net, etc. are hypervisor-agnostic

3. **VMBus works** — OpenVMM emulates VMBus as a software device. The guest sees VMBus channels regardless of the underlying hypervisor.

4. **Inter-VM shared memory is BETTER** — KVM's ivshmem gives true zero-copy inter-VM shared memory. On Hyper-V/WHP this requires workarounds (see previous research document).

5. **Filesystem sharing is simpler** — No need for Windows filesystem translation. Direct host path mounting via virtio-fs.

**What's different:**

| Aspect | Windows host (WHP) | Linux host (KVM) |
|---|---|---|
| Hypervisor backend | `virt_whp` | `virt_kvm` |
| Hyper-V enlightenments | Native (real Hyper-V) | Emulated by `hv1_emulator` |
| Networking | WinNAT + Hyper-V vSwitch | iptables/bridge (standard Linux) |
| Filesystem | 9P with `lxutil.dll` | virtiofs (native, faster) |
| GPU | dxgkrnl relay | VFIO passthrough |
| Inter-VM shared memory | `WHvMapGpaRange` trick | ivshmem (built-in!) |
| "What's the point?" | Run Linux on Windows | Custom VM orchestration, testing, distributed kernel research |

**Use cases for WSLX on Linux:**
- **Development/testing** — Test WSLX VMM code on Linux where debugging tools are better
- **Nested virtualization** — Run WSLX inside a Linux cloud VM
- **Distributed kernel research** — KVM + ivshmem for multi-VM shared memory experiments
- **CI/CD** — Build and test WSLX in Linux-based CI pipelines
- **Cross-compilation validation** — Ensure the WSL kernel + OpenVMM work identically on both platforms

### Scenario D: Linux host as nested virtualization (WSLX inside a VM)

**Feasibility: MEDIUM**

Running OpenVMM with KVM inside a VM that itself runs on KVM or Hyper-V:

```
Bare metal host (KVM or Hyper-V)
  └── Outer VM (Linux guest, nested virt enabled)
       └── /dev/kvm available
            └── OpenVMM (virt_kvm backend)
                 └── Inner VM: WSL kernel + distros
```

This works if the outer hypervisor exposes nested virtualization:
- **KVM-on-KVM**: Well-supported on modern CPUs with VMX/SVM nesting
- **KVM-on-Hyper-V**: Works via Windows' nested virtualization feature (requires Intel VT-x; AMD support limited)

Performance is degraded (~10-30% overhead from double SLAT/EPT), but acceptable for development and testing.

---

## 7. The Language Boundary: Rust ↔ C/C++

### Current state

| Component | Language | Can we use it? |
|---|---|---|
| OpenVMM | Rust (98.1%) | Yes — primary vehicle |
| OpenHCL | Rust | Yes — selective crate reuse |
| Hyperlight | Rust | Peripheral use (function sandboxes) |
| WSL open source (Windows side) | C++ 58.4%, C 36.9% | Yes — reuse or port |
| WSL Linux kernel | C | Yes — use as-is |
| WSL init/daemons | C/C++ | Yes — reuse as-is (runs in guest) |

### Integration strategy

The guest-side code (kernel, init, daemons) runs inside the VM and doesn't care what language the host VMM is written in. They communicate via VMBus rings, hvsocket, and virtio queues — all binary protocols.

The host-side boundary is:

```
Rust world (OpenVMM):                 C/C++ world (WSL components):
┌────────────────────────────┐        ┌────────────────────────────┐
│ wslx-vmm (Rust)            │        │ wsl.exe (C++) [optional]   │
│   │                        │        │   │                        │
│   ├── OpenVMM device crates│ ◄──────┤   ├── CLI parsing          │
│   ├── Portal allocator     │  FFI   │   ├── Process factory      │
│   ├── VM lifecycle mgmt    │  or    │   └── wslpath, wslvar      │
│   └── Config/state         │  IPC   │                            │
└────────────────────────────┘        └────────────────────────────┘
```

Two approaches:
1. **FFI boundary** — Expose a C API from the Rust VMM, call from C++ CLI
2. **IPC boundary** — Rust VMM runs as a service, C++ CLI communicates via named pipe/socket/gRPC
3. **Full Rust rewrite** — Rewrite the CLI components in Rust (cleanest long-term)

The IPC approach is most practical initially — the existing WSL CLI code can be adapted to talk to a new Rust-based VMM service rather than `wslservice.exe`.

---

## 8. OpenHCL: What It Means for This Architecture

### OpenHCL's role

OpenHCL is a **paravisor** — it runs inside the guest VM at VTL2 (a higher privilege level than the guest OS at VTL0), providing device emulation and security services. It uses OpenVMM as its core engine.

**For our purposes, OpenHCL is NOT directly useful.** It requires:
- Hyper-V Virtual Trust Levels (VTL2) — not available on standard Windows, only Azure/MSHV
- IGVM file format loading — specialized boot format
- `/dev/mshv_vtl` interface — Azure-specific

### What IS useful from OpenHCL

The OpenHCL codebase contains valuable implementations we can study or adapt:

1. **`vmbus_relay`** — How to bridge VMBus channels between different trust domains. Useful pattern for multi-VM architectures.
2. **Boot shim (`openhcl_boot`)** — How to bootstrap from raw hardware state to a running Linux kernel. Relevant to WARP Track B.
3. **State management** — How OpenHCL handles snapshot/restore across all device states.
4. **`underhill_init`** — A complete init system for a minimal Linux environment hosting an OpenVMM instance.

### Could OpenHCL run inside an OpenVMM-created partition?

**Not directly** — OpenHCL needs VTLs, which are a hypervisor feature, not something a VMM can provide. However, on an Azure/MSHV host, the architecture could be:

```
MSHV host → OpenVMM (with VTL support) → OpenHCL in VTL2 → Guest OS in VTL0
```

This is exactly what Azure already does. For WSLX's purposes, this is an Azure-specific deployment option, not the primary path.

---

## 9. Revised "Hyper-WSL" Architecture

Based on all research, here is the corrected and expanded architecture:

### Tier 1: OpenVMM-WSL (Primary Path)

Replace HCS/HCN with OpenVMM while keeping the WSL kernel and distro model:

```
┌────────────────────────────────────────────────────────────────────┐
│                     WSLX Custom Stack                              │
│                                                                    │
│  wslx-cli (Rust or C++, adapted from open-source wsl.exe)         │
│      │                                                             │
│  wslx-vmm (Rust, built on OpenVMM crates)                         │
│      │                                                             │
│      ├── VM Lifecycle ──────── OpenVMM partition management        │
│      │                         (virt_whp on Windows, virt_kvm      │
│      │                          on Linux)                          │
│      │                                                             │
│      ├── VMBus ─────────────── vmbus_server + storvsp + netvsp     │
│      │                         (synthetic devices for WSL kernel)  │
│      │                                                             │
│      ├── Portal ────────────── virtio_pmem (DAX shared memory)     │
│      │                         + custom arena allocator            │
│      │                         + ring structures                   │
│      │                                                             │
│      ├── Filesystem ────────── virtiofs (Linux host) or            │
│      │                         virtio_p9 (Windows host)            │
│      │                                                             │
│      ├── Networking ────────── virtio_net + platform-specific      │
│      │                         backend (WinNAT / iptables)         │
│      │                                                             │
│      ├── Console ───────────── virtio_serial + UART emulation      │
│      │                                                             │
│      └── Snapshots ─────────── OpenVMM state serialization         │
│                                + Portal-aware quiesce protocol     │
│                                                                    │
│  ┌─────────────── Inside the VM ──────────────────────────────┐    │
│  │ WSL Linux Kernel 6.6.x (unchanged)                         │    │
│  │   ├── VMBus drivers (storvsc, netvsc, hv_balloon, hvsock)  │    │
│  │   ├── virtio drivers (virtio_pmem, virtio_net, virtiofs)   │    │
│  │   └── Hyper-V enlightenments (SynIC, hypercalls)           │    │
│  │                                                             │    │
│  │ WSL init (from open-source WSL, C/C++)                      │    │
│  │   ├── Distro mount/namespace management                    │    │
│  │   ├── hvsocket-based control protocol                      │    │
│  │   └── Plan9/VirtioFS mount for host drives                 │    │
│  │                                                             │    │
│  │ Distros: Ubuntu, Debian, etc. (ext4 VHD, unchanged)        │    │
│  └─────────────────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────────────────┘
```

### Tier 2: Multi-VM Extension (Portal + Distributed)

Multiple OpenVMM-managed VMs with shared memory:

```
wslx-vmm (single process, manages multiple VMs)
    │
    ├── VM-1 (Ubuntu distro)
    │   └── virtio-pmem → portal_shared.bin (host file)
    │                          │
    ├── VM-2 (Debian distro)   │ Same file mapped
    │   └── virtio-pmem → portal_shared.bin ← into multiple VMs
    │                          │
    └── VM-3 (Custom kernel)   │
        └── virtio-pmem → portal_shared.bin

On KVM: can also use ivshmem for even more direct sharing
```

### Tier 3: Hyperlight Micro-VM Sidecar

Hyperlight for lightweight function sandboxes alongside the main WSL VM:

```
wslx-vmm (OpenVMM)                   wslx-functions (Hyperlight)
┌──────────────────────────┐          ┌──────────────────────────┐
│ WSL Utility VM           │          │ Micro-VM pool            │
│ (full Linux, distros)    │ ◄───────►│ (per-function, <1ms)     │
│                          │ shared   │                          │
│ Portal region ◄──────────┼─memory──►│ Guest functions can      │
│ (virtio-pmem)            │          │ read/write Portal        │
└──────────────────────────┘          │ (mapped into VM memory)  │
                                      └──────────────────────────┘
```

This uses Hyperlight where it excels — ephemeral function execution with hardware isolation — while the main workload runs in a proper OpenVMM-managed VM.

---

## 10. The Paravirtualization Driver Compatibility Story

### Why we believe seamless compatibility is likely

The WSLX kernel code search confirms that the WSL kernel's paravirtualization drivers are designed with OpenVMM in mind:

1. **Shared provenance**: The WSL kernel's Hyper-V drivers (`drivers/hv/`) and OpenVMM's VMBus crates were developed by the same organization (Microsoft). The wire protocols match because they're two halves of the same system.

2. **Dual-stack design**: The WSL kernel config enables BOTH:
   - Hyper-V synthetic devices (VMBus): `storvsc`, `netvsc`, `hv_balloon`, `hv_sock`
   - Virtio devices: `virtio_pmem`, `virtio_net`, `virtio_fs`, `virtio_blk`

   This means the kernel boots and probes both buses. Under HCS, it finds VMBus devices. Under OpenVMM, it can find either (or both).

3. **`hvlite` naming in OpenVMM**: OpenVMM's codebase still contains `hvlite` references (the former codename). The virtio-pmem device in OpenVMM (`virtio_pmem` crate) is the host-side counterpart of `Kernel/drivers/nvdimm/virtio_pmem.c`. They use the same `VIRTIO_PMEM_REQ_TYPE_FLUSH` protocol, the same shared memory region semantics, the same DAX mapping.

4. **VMBus protocol versioning**: Both the kernel's VMBus driver and OpenVMM's `vmbus_core` crate implement the same protocol versions with the same message formats. This is unsurprising — they must interoperate in Azure production.

5. **hvsocket compatibility**: The kernel's `hyperv_transport.c` implements AF_VSOCK over hvsocket. OpenVMM's `vmbus_server` crate provides hvsocket relay. The WSL init process uses specific port numbers (50000-50005) for control, and these are just configuration — the transport works the same.

### The closed-source gap (what's NOT in OpenVMM)

Some WSL-specific VMBus devices have host-side implementations that ship with Windows but aren't in OpenVMM:

| Device | Guest Driver | Host Side (Windows) | In OpenVMM? |
|---|---|---|---|
| Synthetic SCSI | `storvsc` | StorVSP (Windows kernel) | **Yes** — `storvsp` crate |
| Synthetic NIC | `netvsc` | NetVSP (Windows kernel) | **Partial** — needs network backend |
| Dynamic Memory | `hv_balloon` | DM VSP (Windows kernel) | **Yes** — OpenVMM manages this |
| GPU-P (dxgkrnl) | `dxgkrnl` | DXG VSP (Windows kernel) | **No** — complex, GPU-specific |
| 9P file server | `v9fs` (kernel) | `p9np.dll` + `p9rdr.sys` | **Yes** — `virtio_p9` (different transport) |

The dxgkrnl (GPU) gap is the most significant. Everything else has an OpenVMM equivalent.

---

## 11. Recommended Path Forward

### Phase 1: Proof of Concept (OpenVMM boots WSL kernel)

**Goal**: Boot the WSL kernel using OpenVMM on Windows (WHP) and Linux (KVM).

1. Build OpenVMM from source on both platforms
2. Configure it to boot the WSL kernel (`Kernel/arch/x86/boot/bzImage`) with Linux Direct Boot
3. Attach a distro ext4 VHD via storvsp or virtio-blk
4. Verify VMBus enumeration (kernel should find synthetic devices)
5. Get a serial console shell

**This likely works out of the box.** OpenVMM already supports Linux Direct Boot, VMBus, and storvsp.

### Phase 2: WSL Init Integration

**Goal**: Run the WSL init process and get a distro shell via hvsocket.

1. Build the WSL init from `src/linux/init/`
2. Pack it into an initrd or install into the distro root filesystem
3. Configure hvsocket ports in OpenVMM
4. Implement the host-side init protocol (HELLO, CONFIG, EXEC) in Rust
5. Get `wsl.exe`-equivalent functionality: launch a shell, run commands

### Phase 3: Portal MVP

**Goal**: Zero-copy shared memory between host and guest via virtio-pmem.

1. Configure OpenVMM with a virtio-pmem device backed by a host file
2. Guest maps `/dev/pmem0` with DAX
3. Host maps the same file
4. Implement Portal arena allocator and ring structures
5. Benchmark: raw memcpy throughput, ring operation latency

### Phase 4: Cross-Platform Validation

**Goal**: Same `wslx-vmm` binary works on Windows and Linux.

1. CI pipeline: build + test on both Windows (WHP) and Linux (KVM)
2. Verify identical behavior (boot, init, Portal, networking)
3. Document platform-specific configuration (networking backends)

### Phase 5: Multi-VM and Distributed Research

**Goal**: Multiple VMs with shared Portal regions.

1. Launch two OpenVMM-managed VMs from the same process
2. Map the same virtio-pmem backing file into both
3. On KVM: additionally test ivshmem for comparison
4. Implement inter-VM signaling (hvsocket doorbell or custom mechanism)
5. Prototype distributed shared memory semantics

---

## 12. Open Questions

1. **OpenVMM stability as standalone VMM**: OpenVMM docs explicitly say it's a "development platform, not a ready-to-deploy application." How much stabilization work is needed?

2. **VMBus version compatibility**: Does the WSL kernel's VMBus driver expect specific protocol versions that OpenVMM's server might not support (or vice versa)?

3. **WHP API requirements**: OpenVMM requires `WHvMapGpaRange2` which needs Windows 11+. What about Windows 10 support?

4. **dxgkrnl relay**: Can we relay the GPU-P VMBus channels from OpenVMM to the Windows GPU stack, or does this require Windows kernel-mode components?

5. **Performance**: How does OpenVMM (user-space VMM on WHP) compare to HCS (which uses the kernel-mode VM worker process `vmwp.exe`)? The OpenVMM docs note that performance "is not great" when emulating VTLs on WHP — but we wouldn't be using VTLs.

6. **Memory overhead**: OpenVMM runs in user-space. Does this add measurable memory overhead vs. the HCS approach where device emulation happens in the kernel-mode vmwp.exe?

7. **WSL init port allocation**: Can we configure the WSL init to use custom hvsocket ports, or are the port numbers (50000-50005) hardcoded in the kernel/init?

---

## 13. References

### Microsoft Open Source Projects
- [OpenVMM GitHub](https://github.com/microsoft/openvmm)
- [OpenVMM Guide](https://openvmm.dev/guide/)
- [OpenHCL Architecture](https://github.com/microsoft/openvmm/blob/main/Guide/src/reference/architecture/openhcl.md)
- [Hyperlight GitHub](https://github.com/hyperlight-dev/hyperlight)
- [Hyperlight-Wasm GitHub](https://github.com/hyperlight-dev/hyperlight-wasm)
- [WSL GitHub (open-sourced)](https://github.com/microsoft/WSL)
- [WSL2-Linux-Kernel GitHub](https://github.com/microsoft/WSL2-Linux-Kernel)
- [hcsshim (Go HCS bindings)](https://github.com/microsoft/hcsshim)

### Microsoft Documentation
- [Hyper-V TLFS](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs)
- [WHP API](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
- [HCS API](https://learn.microsoft.com/en-us/virtualization/api/hcs/overview)

### Hyperlight Deep Dive
- [Introducing Hyperlight (Nov 2024)](https://opensource.microsoft.com/blog/2024/11/07/introducing-hyperlight-virtual-machine-based-security-for-functions-at-scale)
- [Hyperlight 0.9ms execution (Feb 2025)](https://opensource.microsoft.com/blog/2025/02/11/hyperlight-creating-a-0-0009-second-micro-vm-execution-time)
- [Hyperlight Nanvix POSIX support (Jan 2026)](https://opensource.microsoft.com/blog/2026/1/28/hyperlight-nanvix-bringing-multi-language-support-for-extremely-fast-hardware-isolated-micro-vms)

### OpenVMM / OpenHCL
- [OpenHCL: The new open source paravisor](https://techcommunity.microsoft.com/blog/windowsosplatform/openhcl-the-new-open-source-paravisor/4273172)
- [OpenHCL: Evolving Azure's virtualization model](https://techcommunity.microsoft.com/blog/windowsosplatform/openhcl-evolving-azure%E2%80%99s-virtualization-model/4248345)
- [OpenVMM Rustdoc (vmbus_server)](https://openvmm.dev/rustdoc/linux/vmbus_server/index.html)
- [OpenVMM Rustdoc (virtio_pmem)](https://openvmm.dev/rustdoc/linux/virtio_pmem/index.html)

### WSLX Internal Research
- `doc/research/WHP vs HCS - VMBus - Inter-VM Shared Memory.md`
- `doc/research/Hyper-V Virtualization Primitives for WSLX.md`
- `doc/research/Gemini + Portal/seminal - Gemini + Portal.md`
- `doc/research/WARP/seminal - WSLX WARP.md`

---

*Document created 2026-02-07. Subject to revision as research progresses.*
