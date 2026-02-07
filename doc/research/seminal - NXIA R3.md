# NXIA R3 — Seminal Introduction
**Draft v1.0 — WIP Research (Evolving Concepts)**
**Date:** 2026-02-07

> **This document contains evolving research concepts, not specifications.**
> Names, boundaries, and architectural details are subject to change as
> understanding deepens. Nothing here constitutes a commitment or a frozen design.

---

## 0. What This Document Is

This is the founding description of **NXIA R3**: a platform architecture where
virtualization is a feature — not infrastructure overhead — and where installing
the platform gives you not just libraries and tools, but a **managed virtual OS
kernel substrate** that the platform owns, controls, and exposes to everything
built on top of it.

NXIA R3 synthesizes research from multiple directions:

- **Portal**: zero-copy shared memory as a first-class primitive
  (virtio-pmem/DAX, ivshmem-class mechanisms)
- **Gemini**: bidirectional cross-OS execution fabric (host ↔ System VM)
- **WARP**: the System VM as an arbitrary runtime platform capable of hosting
  Linux-personality kernels, modified kernels, or entirely new OS personalities
- **Snapshots**: process-level and VM-level state capture/restore
- **Hypervisor feasibility**: OpenVMM as the portable VMM substrate across
  Windows (WHP), Linux (KVM), and potentially macOS (HVF)

Prior research exists in a WSLX context (a differentiated, side-by-side WSL fork
that is independently distributable and runs in parallel to canonical WSL). NXIA
R3 generalizes beyond WSLX: we are building our own multiplatform hypervisor
system. WSLX remains relevant as a proving ground and as evidence that the WSL
Linux kernel, its distro model, and its integration surfaces are viable building
blocks — but NXIA R3 is not coupled to WSL.

---

## 1. The Core Thesis

### 1.1 Virtualization as a feature

Most platforms treat VMs as deployment targets or isolation boxes. NXIA R3 treats
the VM as **part of the runtime itself**. When you install NXIA, you get:

- Native host components (lightweight, per-OS)
- A portable VMM (OpenVMM-class, Rust, cross-platform)
- A **System VM**: a Linux-kernel-based virtual machine that *is* the platform's
  semantic substrate

This is analogous to installing a language runtime — except the runtime includes
a managed kernel. The modest virtualization cost is the price of admission for:
consistent cross-OS semantics, kernel-grade isolation, controlled device
composition, and shared-memory primitives that don't exist in user-space.

### 1.2 The meta-runtime

NXIA R3 is not itself an application platform. It is a **meta-runtime**: a
foundation that other runtimes, languages, platforms, and execution models build
on top of. The meta-runtime provides:

- A kernel you control (scheduling, memory, namespaces, eBPF, LSM hooks)
- OS-grade services (persistence, search, policy enforcement, orchestration)
- A shared-memory fabric (Portal) for zero-copy communication
- A cross-OS execution bridge (Gemini) for host integration
- VM lifecycle primitives (snapshots, multi-VM, isolation profiles)

Higher-level runtimes — whether existing (JVM, CLR, Node, Python) or new
(custom language runtimes, AI agent runtimes, capability-oriented environments)
— target the meta-runtime rather than each host OS individually. The meta-runtime
absorbs platform variance.

### 1.3 Universal distributivity

Distribution is a primitive, not an application-level concern. NXIA R3 aims for
a world where:

- Code and data move between nodes (VMs, machines, clusters) as a platform
  operation, not an application rewrite
- Isolation boundaries (VM, container, process) are policy-driven and
  reconfigurable
- The shared-memory fabric extends naturally from intra-VM to inter-VM to
  inter-machine (with degrading performance but preserved semantics)

This is ambitious and unproven. It is a research direction, not a claim.

---

## 2. Architecture: The Three Strata

NXIA R3 decomposes into three cooperating layers. All three are under platform
control. The key architectural invariant is that **the boundaries between strata
are explicit, versioned, and crossable only through defined conduits**.

### Stratum 1: Host OS Components

Per-host-OS native code. Intentionally thin. Responsible for:

- **User experience integration**: Shell integration, IDE tooling, native UI
  surfaces, system tray, notifications, file associations
- **Security and policy integration**: Host identity bridging (Windows user
  tokens, Linux uid/gid, macOS keychains), platform policy enforcement at the
  host boundary
- **High-performance conduits**: The host-side endpoints of Portal shared memory
  regions (memory-mapped files backing virtio-pmem devices), host-side Gemini
  transport endpoints (hvsocket/vsock), and host networking integration
  (WinNAT, iptables/nftables, TAP adapters)
- **Lifecycle and orchestration**: Starting/stopping the VMM, managing VM
  profiles, handling suspend/resume/hibernate, auto-start on login

What Stratum 1 is NOT: it is not where platform logic lives. It is a facade,
a bridge, and a lifecycle manager. If the host OS changes or breaks something,
Stratum 1 absorbs the impact. The rest of the platform is unaffected.

### Stratum 2: The VMM Layer

A portable, cross-platform Virtual Machine Monitor. OpenVMM-class: written in
Rust, with pluggable hypervisor backends:

| Host OS | Backend | Status |
|---|---|---|
| Windows 11+ | WHP (`virt_whp`) | Viable (OpenVMM production-tested) |
| Linux | KVM (`virt_kvm`) | Viable (OpenVMM production-tested) |
| macOS | HVF (`virt_hvf`) | Experimental (OpenVMM has a backend) |

Stratum 2 is responsible for:

- **VM lifecycle**: Create, boot, pause, resume, snapshot, destroy
- **Device composition**: Which virtual devices each VM gets — VMBus synthetic
  devices, virtio devices (pmem, fs, net, blk, console), or custom devices
- **Memory topology**: Guest physical address space layout, shared-memory region
  mapping (the host-side of Portal), multi-VM shared regions
- **Hyper-V enlightenment emulation**: SynIC, synthetic timers, hypercall
  interface — so that a Hyper-V-enlightened Linux kernel runs unchanged on KVM
  (OpenVMM's `hv1_emulator` crate handles this)
- **VM profile management**: Different VM configurations for different purposes
  (see Section 4)

Stratum 2 is where NXIA R3 differentiates from "just running Linux in a VM."
Control over the VMM means control over:
- Which memory regions are shared (Portal)
- Which devices exist (WARP device sets)
- How multiple VMs relate to each other (distributed kernel research)
- What the snapshot/restore semantics are

### Stratum 3: The System VM

A Linux-kernel-based virtual machine that serves as the platform's semantic
substrate. This is where most platform complexity lives, implemented once,
running identically on every host OS.

The System VM provides:

- **A real Linux kernel** (currently based on 6.6.x LTS, with dual-stack
  support for both VMBus synthetic devices and virtio devices — confirmed by
  kernel config analysis showing CONFIG_VIRTIO_PMEM=y, CONFIG_VIRTIO_FS=y,
  CONFIG_DAX=y alongside full Hyper-V driver support)
- **Platform services**: Persistence engines, search/indexing, policy
  enforcement, orchestration, audit logging, capability management
- **Guest-side Portal endpoints**: `/dev/pmem0` mapped with DAX for zero-copy
  shared memory with the host (and potentially with other VMs)
- **Guest-side Gemini agent**: The execution fabric endpoint that bridges
  calls between host-native code and System VM services
- **AgentX**: The control-plane agent that speaks the NXIA lifecycle protocol
  (boot, configure, mount, exec, quiesce, resume, shutdown)
- **Namespace/container isolation**: Linux namespaces, cgroups, seccomp, LSM
  hooks — providing intra-VM isolation for workloads without additional VMs

The System VM kernel is not necessarily unmodified upstream Linux. NXIA R3
embraces kernel modification:
- Custom schedulers, memory policies, or storage drivers
- eBPF programs for platform-level observability and policy
- LSM modules for capability-based security enforcement
- As long as the Linux userspace ABI is preserved, existing distros and
  applications run unmodified (WARP Track A)

---

## 3. Portal: Shared Memory as a First-Class Primitive

### 3.1 Why shared memory matters

Every cross-boundary communication mechanism (RPC, IPC, network) involves
copying data. For a platform that treats the VM boundary as internal (not
external), copy overhead becomes a tax on every operation. Portal eliminates
this tax for the common case.

### 3.2 Implementation path

Portal is built on **virtio-pmem with DAX** (Direct Access):

```
Host (Stratum 1):
  portal_region.bin → memory-mapped file → OpenVMM virtio-pmem device
                                                  │
                                                  ▼ (PCI/MMIO)
System VM (Stratum 3):
  /dev/pmem0 → DAX mount or raw mmap → Portal arena allocator
```

Both sides see the same physical memory. No copies. The host writes; the guest
reads immediately. The guest writes; the host reads immediately. This is not
emulated shared memory — it is literal shared pages via the hypervisor's address
mapping.

### 3.3 Portal primitives (conceptual)

- **PortalRegion**: A shared memory segment mapped into both host and guest
- **PortalArena**: An allocator within a region (bump, slab, or concurrent)
- **PortalSpan**: `(region_id, offset, length, flags, generation)` — the
  universal "pointer" that crosses all boundaries
- **Rings**: SubmitRing, CompleteRing, EventRing, UpcallRing — producer/consumer
  queues for structured communication
- **Doorbells**: Lightweight signaling (hvsocket notification or HvCallSignalEvent)
  to wake the consumer when data is available

Portal enforces: no raw pointers cross any boundary. All cross-boundary
references are PortalSpans with bounds checking and generation numbers to prevent
use-after-free.

### 3.4 Multi-VM Portal

For architectures with multiple VMs, the same host backing file can be mapped as
a virtio-pmem device into multiple partitions. Both VMs see the same memory.

On Linux hosts with KVM, an even more direct path exists: **ivshmem** (Inter-VM
Shared Memory), a virtual PCI device that maps a shared memory BAR into multiple
VMs with sub-microsecond latency and built-in interrupt-based signaling.

Hyper-V (Windows host) has no built-in ivshmem equivalent. The virtio-pmem
approach via OpenVMM is the practical workaround, with the option of extending
OpenVMM to add ivshmem-like semantics. Alternatively, `WHvMapGpaRange` can map
the same host buffer into multiple WHP partitions — true shared memory, but
requiring a custom VMM path.

### 3.5 Performance envelope (expected)

| Operation | Mechanism | Latency |
|---|---|---|
| Small RPC (< 4KB) | HV socket control plane | 10-50 us |
| Bulk data transfer | Portal DAX (memcpy speed) | GB/s |
| Ring doorbell | HV socket or HvCallSignalEvent | < 10 us |
| Inter-VM shared memory (KVM) | ivshmem | < 1 us |
| Inter-VM shared memory (WHP) | virtio-pmem shared file | < 1 us (memory bus) |

---

## 4. VM Profiles

NXIA R3 does not assume a single VM. The VMM layer supports multiple
concurrently running VMs with different characteristics:

### System VM (default)

The shared substrate. Always running while the platform is active. Hosts
platform services, persistence engines, and the primary workload environment.
Multiple "distros" or workload namespaces can coexist within it (the WSL model:
shared kernel, per-namespace root filesystems).

### Isolated VMs

Per-workload or per-project VMs providing stronger isolation than namespaces:

- Separate kernel instance (can load different modules, run different policies)
- Independent snapshot/restore lifecycle
- Dedicated resource allocation (memory, CPU)
- Connected to the System VM via Portal shared-memory regions

Use cases: untrusted code execution, multi-tenant isolation, security-sensitive
workloads.

### Micro-VMs (Hyperlight-class)

Sub-millisecond cold-start function sandboxes for ephemeral execution:

- **No device model, no OS, single vCPU, < 1 GB memory**
- Custom `no_std` Rust/C binaries or WebAssembly (via Hyperlight-Wasm)
- Can share Portal memory regions with the System VM
- Hardware-isolated (real VM, not just a sandbox)

Use cases: Sandboxed build steps, plugin evaluation, untrusted function
execution, AI tool-call sandboxing.

Hyperlight (Microsoft's open-source Rust micro-VMM library, CNCF Sandbox
project) provides this capability. It uses the same hypervisor backends as
OpenVMM (WHP, KVM, MSHV) but with radically reduced overhead.

### Performance-Profile VMs

VMs tuned for specific workload characteristics:

- **Latency-optimized**: Pinned vCPUs, no overcommit, minimal device set
- **Throughput-optimized**: Multiple vCPUs, large memory, deep I/O queues
- **Footprint-optimized**: Minimal memory, shared kernel image, copy-on-write
  filesystems

These are configuration profiles, not separate architectures. The same VMM code
manages all of them.

---

## 5. Gemini: Cross-OS Execution Fabric

### 5.1 The problem

The System VM runs Linux. The host runs Windows (or macOS, or Linux). Users
expect native-feeling applications. Gemini bridges this gap.

### 5.2 Two directions

**L→W (Linux calls Windows):** A process in the System VM invokes a Windows API
(Win32, COM, .NET, etc.). Gemini routes the call to a paired executor process
on the Windows host, which performs the real API call and returns results.

**W→L (Windows calls Linux):** A Windows process invokes a Linux API (POSIX,
syscalls, Linux services). Gemini routes the call to a paired executor process
in the System VM.

Both directions share the same machinery: sessions, RPC protocol, Portal
data plane, capability tokens.

### 5.3 Key design principles

- **Capability tokens, not raw handles**: Remote HANDLE/fd/object pointers are
  never exposed directly. Cross-boundary references use opaque tokens with
  rights bitmasks and lifetime management.
- **Policy-gated**: All cross-boundary calls pass through a policy module.
  Catastrophic operations are denied by default. Logging and auditing are
  mandatory even in development mode.
- **Portal-backed marshalling**: Large arguments (buffers, strings, structs)
  are placed in Portal regions and referenced by PortalSpan. Only the span
  descriptor crosses the RPC boundary — the data stays in shared memory.
- **Paired process model**: One host-side executor per guest-side caller
  (default). Lifetime is coupled: if one dies, the other is cleaned up.
  Multiplexing is a later optimization.

### 5.4 .NET as first-class runtime integration target

Gemini's Runtime Layer makes foreign APIs feel native in .NET:

- **Linux .NET → Windows APIs**: `DllImport("kernel32.dll")` on Linux routes
  through Gemini to the Windows host. A managed resolver remaps Win32 module
  names to Gemini shim libraries.
- **Windows .NET → Linux APIs**: `DllImport("libc.so.6")` on Windows routes
  through Gemini to the System VM.
- **Portal-backed marshalling helpers** for large buffer P/Invoke patterns.

This is a research direction, not a delivered feature.

---

## 6. WARP: Arbitrary Runtime Platform

### 6.1 Beyond Linux

Once the VMM layer is under our control, "must be Linux" dissolves. WARP defines
the minimum invariants a guest kernel must satisfy to participate in the NXIA
platform:

1. **Bootable** via the VMM's boot pathway (Linux direct boot, UEFI, or custom)
2. **Control plane**: Bidirectional protocol channel for lifecycle, process
   launch, I/O relay (AgentX protocol)
3. **Storage**: Ability to mount a per-workload image as root filesystem
4. **Optional integrations**: Networking, filesystem sharing, GPU, GUI — each
   as a modular contract

### 6.2 Two tracks

**Track A — Linux-personality**: Preserve the Linux userspace ABI (syscalls,
VFS, process model) while making deep kernel modifications internally. Existing
distros (Ubuntu, Debian, etc.) run unmodified. The kernel is "not stock Linux"
but is "Linux-compatible" at the ABI boundary.

**Track B — New-OS**: A non-Linux kernel with its own ABI and userland. Maximum
architectural freedom. Must implement WARP's minimum invariants (AgentX, control
plane, storage). This makes NXIA a Windows-integrated (and Linux-integrated)
micro-VM OS runtime, not just a Linux subsystem.

### 6.3 OpenHCL as a boot shim (Track B enabler)

For Track B kernels that don't implement Hyper-V enlightenments natively,
OpenHCL (Microsoft's open-source Rust paravisor) can serve as a boot shim: it
sets up GDT, page tables, and Linux Boot Protocol requirements before jumping
to the kernel. OpenHCL was designed as a VTL2 paravisor for Azure confidential
computing, but its boot machinery is reusable as a standalone shim.

### 6.4 The distro model

Regardless of track, NXIA uses a "kernel + image" distribution model inspired
by (but not limited to) WSL's approach:

- **KernelX**: The guest kernel (shared across workloads, or per-workload)
- **ImageX**: The root filesystem + manifest (distro in Track A, OS image in
  Track B)
- **AgentX**: The init/control agent inside the guest

ImageX packaging reuses the filesystem-plus-manifest pattern (similar to WSL's
`.wsl` format) regardless of whether the contents are Linux or a new OS.

---

## 7. Snapshots: Process-Level and VM-Level

### 7.1 Process-level snapshots

CRIU-class checkpoint/restore of individual processes or process trees within
the System VM. Captures: CPU registers, address space, open FDs, pipes, epoll
state, and (best-effort) network sockets.

Known challenges in a VM environment:
- seccomp filter inheritance from init
- ptrace permission restrictions
- hvsocket/vsock endpoint restoration requires host-side cooperation
- GPU compute state is generally non-checkpointable

Approach: cooperative checkpoint with profiles (minimal, network, full) and
host-side quiesce coordination.

### 7.2 VM-level snapshots

Full hypervisor checkpoint: guest RAM + vCPU state + virtual device state.
Restore means "resume the entire machine exactly where it was."

The VMM layer (OpenVMM) has state serialization for all devices. The key
question is whether quiesced save/restore works for the specific VM configuration
NXIA uses (utility-VM-style with Portal regions, hvsocket channels, and
potentially GPU state).

Portal state survives snapshot/restore naturally — it's backed by a host file.
The host-side mapping persists across VM restart. The guest re-maps on resume.

### 7.3 Quiesce protocol

Before snapshot:
1. AgentX receives QUIESCE command from Stratum 1
2. Drain in-flight Portal operations
3. Flush virtio-pmem regions (`VIRTIO_PMEM_REQ_TYPE_FLUSH`)
4. Send QUIESCE_COMPLETE
5. VM snapshot occurs

After restore:
1. AgentX receives RESUME command
2. Re-establish hvsocket channels
3. Resume Portal producers/consumers
4. Normal operation continues

---

## 8. Security Model

### 8.1 Trust boundaries

- **Stratum 1 (host)** is the primary authority for host-OS policy
- **Stratum 2 (VMM)** enforces memory isolation, device access, and VM boundaries
- **Stratum 3 (System VM)** is the primary authority for platform policy within
  the guest (LSM, seccomp, namespaces, capability manifests)
- Guest calls to the host are "remote" even on the same machine — subject to
  policy and capability checks

### 8.2 Capability-based access

The platform moves toward capability-based security:
- Language-level intents ("this data is local-only", "this function may expose
  to remote", "this object must be encrypted at rest") compile into
  capability manifests
- Manifests are enforced by kernel mechanisms in the System VM (LSM modules,
  eBPF programs, namespace/cgroup policies)
- The host side enforces its own policy at the Gemini boundary (denylist
  catastrophic operations, rate limiting, audit logging)
- Capability tokens (not raw handles) cross all boundaries

### 8.3 Signed artifacts

- KernelX and ImageX should be signed
- AgentX control-plane protocol is versioned and authenticated
- Portal regions are capability-scoped (not ambient-access)
- Snapshot artifacts are encrypted at rest

---

## 9. Cross-Platform Story

### 9.1 What's shared vs. platform-specific

| Component | Shared (Rust, ~80%) | Platform-specific (~20%) |
|---|---|---|
| VMM core (OpenVMM crates) | All device emulation, VMBus, virtio, hv1 emulator | Hypervisor backend selection (WHP/KVM/HVF) |
| Portal | Arena allocator, ring structures, span validation | Host-side file mapping API (CreateFileMapping vs mmap) |
| Gemini | RPC protocol, capability tokens, session management | Transport adapter (hvsocket vs vsock) |
| AgentX | Control protocol, quiesce/resume, process factory | Nothing (runs inside the VM) |
| System VM kernel | Everything (identical kernel image) | Nothing (same binary on all hosts) |
| Host integration | — | Networking (WinNAT vs iptables), filesystem mounts, GPU, shell integration |

### 9.2 Linux host advantages

On a Linux host, KVM provides capabilities that WHP (Windows) lacks:

- **ivshmem**: Direct inter-VM shared memory via PCI BAR — no VMBus proxy, no
  RDMA hardware required. Sub-microsecond latency, true zero-copy. This is
  the most performant multi-VM shared memory option available on any hypervisor.
- **vhost-user**: User-space device backends with zero-copy. Guest virtio-net
  packets go directly to a host user-space process.
- **Nested virtualization**: Run NXIA inside a cloud VM.
- **Mature VFIO GPU passthrough**: More flexible than Hyper-V GPU-P for some
  scenarios.

For distributed kernel research (multi-VM shared memory experiments), **a Linux
host with KVM is the most capable platform**, not the least.

### 9.3 Windows host advantages

- **Windows is where the users are** (developer workstations)
- **WSL ecosystem validation**: The WSL Linux kernel is proven to work under the
  Windows hypervisor with full device support. WSLX demonstrates that a
  differentiated fork can run side-by-side with canonical WSL.
- **Native Windows UI integration**: Gemini L→W calls can invoke Win32/COM/WPF
  for native-feeling applications
- **dxgkrnl GPU passthrough**: WSL2's GPU integration, while complex to
  replicate outside HCS, provides a model for GPU compute in the System VM

---

## 10. Relationship to Existing Research

### WSLX

WSLX is a fully differentiated WSL fork, independently distributable, running
in parallel to canonical WSL on Windows. It provides:
- Evidence that the WSL kernel and distro model work as building blocks
- A proving ground for Portal, Gemini, and WARP concepts
- Confirmation that the WSL kernel's dual-stack drivers (VMBus + virtio) enable
  booting under both HCS and custom VMMs

NXIA R3 generalizes beyond WSLX. Where WSLX is Windows-specific and WSL-derived,
NXIA R3 is cross-platform and VMM-native. WSLX may continue as one deployment
mode (the Windows-native, HCS-compatible path), while the OpenVMM-based path
becomes the primary architecture.

### VAYRON

VAYRON is the experimental runtime integration path — especially .NET/C# runtime
and toolchain experiments. In NXIA R3 terms, VAYRON is a higher-level runtime
that targets the meta-runtime. It demonstrates "runtime over meta-runtime"
composition: VAYRON speaks Portal, adopts NXIA semantics, and delivers
applications that use both Windows and Linux APIs via Gemini.

---

## 11. What "NXIA-Class" Means (Feasibility Definition)

> An **NXIA-class architecture** is one where policy, identity, memory
> semantics, persistence/search, and orchestration are OS-grade services with
> stable ABIs, and where language runtimes compile intent into those services
> rather than reimplementing them per application.

This is not about magical performance. It is about:

- **Semantic gravity moving downward**: Platform concerns (security, persistence,
  distribution, search) become kernel/service-grade, not application-grade.
- **Deduplication of concerns**: Every application doesn't re-invent storage,
  auth, IPC, or policy enforcement.
- **Uniform identity and capability handling**: One model, enforced at the
  right boundary, spanning host and guest.
- **Enforcement at the kernel boundary**: The System VM kernel is the
  enforcement root, not the host OS or the application.
- **Portable system behavior**: The System VM provides identical semantics
  on Windows, Linux, and (eventually) macOS hosts.

---

## 12. Known Challenges and Risks

### Adoption-critical

1. **Developer experience**: Filesystem performance (cross-boundary access via
   9P/virtiofs is slower than native), IDE integration, debugger attachment
   across VM boundary, startup latency
2. **GPU and graphics**: Host GPU integration is the hardest missing piece for
   native-feeling applications. dxgkrnl relay outside HCS is complex; VFIO
   on Linux is different. This may be the long pole.
3. **Networking and identity**: Replacing HCN means implementing our own NAT,
   DNS, and port forwarding. Host identity bridging (Windows tokens ↔ Linux
   uid) requires careful design.

### Architectural

4. **Shared-memory correctness**: Portal regions must be capability-scoped,
   observable, and resilient under failure. Generation numbers and bounds
   checking are necessary but not sufficient — the concurrency model matters.
5. **Snapshot complexity**: Network socket state, GPU state, and host-integration
   channel state may not survive checkpoint/restore. Must design for graceful
   degradation.
6. **VMM maturity**: OpenVMM docs note it is "a development platform, not a
   ready-to-deploy application." Stabilization work is required.

### Strategic

7. **"Windows hostility resistance"**: If the host OS breaks a convenience or
   integration surface, the platform should survive because the System VM holds
   the important state and logic. Stratum 1 should be thin enough that it can be
   repaired or rewritten without affecting the rest. This must be validated, not
   assumed.
8. **Virtualization overhead**: The thesis accepts a "modest" penalty. This must
   be quantified. If Portal + DAX provides memcpy-speed bulk transfer and
   < 50 us RPC, the overhead is manageable. If filesystem access via virtiofs
   adds 5x latency to every file operation, developer experience suffers.

---

## 13. Research Directions (Phased)

### Phase A: Boot the System VM

Boot a Linux kernel on an OpenVMM-class launcher on both Windows (WHP) and
Linux (KVM). Establish VM lifecycle, serial console, basic block device for
root filesystem. Validate that the kernel's dual-stack drivers (VMBus + virtio)
enumerate correctly under OpenVMM.

### Phase B: Control Plane and Process Factory

Implement the AgentX protocol: HELLO, CONFIG, MOUNT, EXEC, STDIO, SIGNAL,
SHUTDOWN. Get a working shell. Run a distro. This is the "custom WSL without
HCS" milestone.

### Phase C: Portal MVP

Configure virtio-pmem in OpenVMM. Map a host file as `/dev/pmem0` with DAX in
the guest. Implement PortalArena, PortalSpan, and ring structures. Benchmark:
raw throughput (memcpy), ring operation latency, doorbell latency.

### Phase D: Gemini Control Plane

Establish hvsocket/vsock sessions between host and System VM. Implement
CALL/RETURN with copy-marshalling. Implement paired executor spawn on both sides.
Demonstrate L→W (file I/O from Linux calling Win32) and W→L (file I/O from
Windows calling POSIX).

### Phase E: Portal Data Plane Integration

Connect Gemini to Portal: CALL/RETURN descriptors reference PortalSpans instead
of copying arguments. Measure the improvement. This is where the architecture
starts to pay off.

### Phase F: Cross-Platform Validation

CI pipeline building and testing on both Windows and Linux. Same VMM binary,
same kernel, same AgentX, same Portal. Document platform-specific delta.

### Phase G: Multi-VM and Distributed Research

Launch multiple VMs from the same VMM process. Shared Portal regions via
virtio-pmem (both platforms) and ivshmem (KVM). Prototype inter-VM signaling.
Explore distributed shared memory semantics. Evaluate whether this is a viable
path toward multi-node runtimes.

### Phase H: Micro-VM Integration

Integrate Hyperlight-class micro-VMs as a sidecar capability. Shared Portal
memory between System VM and micro-VMs. Demonstrate: System VM dispatches a
function to a Hyperlight micro-VM, passes arguments via Portal, gets results
back in < 2ms total.

### Phase I: Snapshot MVP

Process-level: CRIU-based checkpoint/restore within the System VM (no-network
profile). VM-level: probe OpenVMM's state serialization for full VM save/restore.
Implement quiesce protocol. Test Portal state persistence across snapshot.

---

## 14. Summary

NXIA R3 is a meta-runtime architecture built on three strata:

1. **Thin host components** for OS integration and lifecycle
2. **A portable VMM** (OpenVMM-class, Rust, WHP/KVM/HVF backends) for
   controlled virtualization
3. **A Linux-kernel-based System VM** as the stable semantic substrate

With **Portal** (shared-memory-first IPC), **Gemini** (cross-OS execution
fabric), **WARP** (arbitrary guest kernel support), and **Snapshots**
(state capture/restore) as the core enabling primitives.

The thesis: this model can become a foundation for next-generation isolation,
universal distributivity, runtime composability, and cross-OS portability with
consistent semantics — while accepting a modest virtualization cost in exchange
for major capability gains.

Everything in this document is a research direction. Nothing is frozen.
The only way to know if it works is to build Phase A and measure.

---

## References

### NXIA R3 Research Lineage
- `doc/research/Gemini + Portal/intro WSLX NXIA.md` — Original NXIA concept
- `doc/research/Gemini + Portal/seminal - Gemini + Portal.md` — Portal and
  Gemini detailed design
- `doc/research/WARP/seminal - WSLX WARP.md` — WARP arbitrary runtime platform
- `doc/research/Process and VM Snapshots.md/seminal - Process and VM Snapshots.md`
  — Snapshot R&D agenda
- `doc/research/Hyper-V Virtualization Primitives for WSLX.md` — virtio-pmem,
  hypercalls, OpenVMM integration
- `doc/research/WHP vs HCS - VMBus - Inter-VM Shared Memory.md` — Virtualization
  API landscape, VMBus internals, inter-VM shared memory
- `doc/research/Hyperlight-OpenVMM-OpenHCL - Custom WSL Stack Feasibility.md` —
  Custom WSL stack feasibility via OpenVMM

### External References
- [OpenVMM](https://github.com/microsoft/openvmm) — Microsoft's open-source
  Rust VMM
- [OpenHCL](https://openvmm.dev/guide/) — Open-source paravisor
- [Hyperlight](https://github.com/hyperlight-dev/hyperlight) — Rust micro-VMM
  library (CNCF Sandbox)
- [WSL (open source)](https://github.com/microsoft/WSL) — WSL host-side code
- [WSL2-Linux-Kernel](https://github.com/microsoft/WSL2-Linux-Kernel) — WSL
  kernel source
- [Hyper-V TLFS](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs)
  — Hypervisor Top Level Functional Specification
- [WHP API](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
  — Windows Hypervisor Platform
- [HCS API](https://learn.microsoft.com/en-us/virtualization/api/hcs/overview)
  — Host Compute Service
- [ivshmem](https://www.qemu.org/docs/master/system/devices/ivshmem.html) —
  QEMU Inter-VM Shared Memory device

---

*Draft v1.0 — 2026-02-07. This is a living research document. All concepts are
evolving and subject to revision.*
