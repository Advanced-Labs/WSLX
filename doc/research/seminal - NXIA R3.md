# NXIA R3 — Seminal Introduction
**Draft v1.0 — WIP Research (Evolving Concepts)**
**Date:** 2026-02-07

> **This document contains evolving research concepts, not specifications.**
> Names, boundaries, architectural details, and specific technology choices are
> subject to change as understanding deepens and prototypes yield data. Nothing
> here constitutes a commitment or a frozen design. Where names from prior
> research (Portal, Gemini, etc.) appear, they refer to the abstract capability
> being explored, not to any specific interface or protocol version.

---

## 0. What This Document Is

This is the founding description of **NXIA R3**: a multiplatform hypervisor
system whose architecture and designs are based on a customized/augmented Linux
kernel, built to support and implement the technologies needed for the
realization of NXIA's concepts — and which ships as part of the NXIA runtime
distribution.

The document's subject is: **the potential architecture of a Multi-OS Hypervisor
System** — one that runs on Windows 11+, targets macOS, and targets Linux in
two distinct modes — using a Linux-based System VM as its semantic substrate,
a portable VMM as its hypervisor layer, and shared-memory-first conduits as its
core communication primitive.

### Target platforms

| Host OS | Backend | Priority | Notes |
|---|---|---|---|
| **Windows 11+** | WHP | Primary | Where most users are; developer workstations |
| **macOS** | HVF (or equivalent) | Target | Apple Silicon; backend maturity TBD |
| **Linux (mode 1)** | KVM | Target | Regular distros, bare-metal, no custom host kernel required (modules/drivers OK) |
| **Linux (mode 2)** | KVM or native | To evaluate | NXIA's own Linux-based distro; bare-metal kernel could be our custom kernel or stock — requires feasibility, viability, and maintenance evaluation |

### Relationship to prior research

NXIA R3 synthesizes evolving research from multiple directions. Key areas that
inform this architecture include:

- **Shared memory** (Portal-class): zero-copy memory sharing between host and
  guest, and between VMs, via virtio-pmem/DAX and ivshmem-class mechanisms
- **Cross-OS execution** (Gemini-class): bidirectional execution bridging
  between host OS and System VM
- **Snapshots**: process-level and VM-level state capture/restore
- **Hypervisor feasibility**: OpenVMM as a candidate portable VMM substrate

Prior research exists in a WSLX context (a differentiated, side-by-side WSL
fork that is independently distributable and runs in parallel to canonical WSL
on Windows). NXIA R3 generalizes beyond WSLX: we are building our own
multiplatform hypervisor system. WSLX remains relevant as a proving ground and
as evidence that the WSL Linux kernel, its distro model, and its integration
surfaces are viable building blocks — but NXIA R3 is not coupled to WSL.

---

## 1. The Core Thesis

### 1.1 A platform that includes a virtual OS kernel

Most platforms ship libraries, tools, and runtimes. NXIA R3 ships those *and*
a managed virtual OS kernel. When you install NXIA, you get:

- Native host components (lightweight, per-OS)
- A portable VMM (OpenVMM-class, Rust, cross-platform)
- A **System VM**: a Linux-kernel-based virtual machine that *is* the
  platform's semantic substrate

This is not a metaphor. The platform literally provides a VM in both the
software sense (a runtime environment) and the OS/hypervisor sense (a virtual
machine managed by a hypervisor, with its own kernel, memory space, and device
surface). The System VM is as much a part of the NXIA distribution as `dotnet`
is part of a .NET installation — except it's an entire managed kernel
environment.

The modest virtualization cost is the price of admission for: consistent
cross-OS semantics, kernel-grade isolation, controlled device composition, and
shared-memory primitives that don't exist in user-space.

### 1.2 Virtualization as a feature

NXIA R3 treats virtualization as a feature, not infrastructure overhead.
The VMM layer is not a deployment convenience — it is what gives the platform
capabilities that no user-space runtime can provide:

- Direct control over guest physical memory mapping (zero-copy shared regions)
- Kernel-grade isolation boundaries (stronger than containers)
- Device composition (custom virtual devices, per-VM device sets)
- Multi-VM topologies with shared resources
- Snapshot/restore of entire execution states
- A consistent kernel across all host OSes

### 1.3 The meta-runtime

NXIA R3 is not itself an application platform. It is a **meta-runtime**: a
foundation that other runtimes, languages, platforms, and execution models
build on top of. The meta-runtime provides:

- A kernel you control (scheduling, memory, namespaces, eBPF, LSM hooks)
- OS-grade services (persistence, search, policy enforcement, orchestration)
- A shared-memory fabric for zero-copy communication
- A cross-OS execution bridge for host integration
- VM lifecycle primitives (snapshots, multi-VM, isolation profiles)

Higher-level runtimes — whether existing ones ported to the platform or entirely
new ones — target the meta-runtime rather than each host OS individually. The
meta-runtime absorbs platform variance.

The first planned port of an existing runtime is **.NET**, under the codename
**dotnext**. dotnext is a fork of the .NET runtime extended to support the
features and virtues of the NXIA platform — including shared-memory-backed
marshalling, cross-OS execution bridging, and platform-aware security semantics.
Other runtimes (JVM, Node, Python, custom language runtimes, AI agent runtimes)
could follow the same pattern.

### 1.4 Universal distributivity

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
- **High-performance conduits**: The host-side endpoints of shared memory
  regions (memory-mapped files backing virtio-pmem devices), host-side cross-OS
  transport endpoints (hvsocket/vsock), and host networking integration
  (WinNAT, iptables/nftables, TAP adapters)
- **Lifecycle and orchestration**: Starting/stopping the VMM, managing VM
  profiles, handling suspend/resume/hibernate, auto-start on login

What Stratum 1 is NOT: it is not where platform logic lives. It is a facade,
a bridge, and a lifecycle manager. If the host OS changes or breaks something,
Stratum 1 absorbs the impact. The rest of the platform is unaffected. This
"hostility resistance" property — the ability to survive host-OS changes because
the important state and logic live in the System VM — is a design goal. It must
be validated, not assumed.

### Stratum 2: The VMM Layer

A portable, cross-platform Virtual Machine Monitor. OpenVMM-class: written in
Rust, with pluggable hypervisor backends.

| Host OS | Backend | Feasibility |
|---|---|---|
| Windows 11+ | WHP | Supported by OpenVMM; viable substrate for prototyping |
| Linux | KVM | Supported by OpenVMM; viable substrate for prototyping |
| macOS | HVF | Supported by OpenVMM on Apple Silicon; maturity TBD |

> **Note on maturity:** OpenVMM is actively developed and used as a reference
> VMM platform within Microsoft (via OpenHCL in Azure). It is not marketed as
> a standalone production VMM. Maturity varies by backend and device surface.
> Stabilization work is expected.

Stratum 2 is responsible for:

- **VM lifecycle**: Create, boot, pause, resume, snapshot, destroy
- **Device composition**: Which virtual devices each VM gets — VMBus synthetic
  devices, virtio devices (pmem, fs, net, blk, console), or custom devices
- **Memory topology**: Guest physical address space layout, shared-memory region
  mapping, multi-VM shared regions
- **Hyper-V enlightenment emulation**: OpenVMM appears to include Hyper-V
  interface emulation (SynIC, synthetic timers, hypercall surfaces via the
  `hv1_emulator` crate) intended to allow Hyper-V-aware kernels to operate
  across backends. Exact scope and performance parity must be verified —
  especially which enlightenments are required by our chosen kernel
  configuration and whether behavior on the KVM path matches the WHP path.
- **VM profile management**: Different VM configurations for different purposes
  (see Section 4)

Stratum 2 is where NXIA R3 differentiates from "just running Linux in a VM."
Control over the VMM means control over:
- Which memory regions are shared
- Which devices exist
- How multiple VMs relate to each other
- What the snapshot/restore semantics are

### Stratum 3: The System VM

A Linux-kernel-based virtual machine that serves as the platform's semantic
substrate. This is where most platform complexity lives, implemented once,
running identically on every host OS.

**Kernel candidates:**

- **WSL Linux Kernel** (currently 6.6.x LTS) as-is or forked — known to have
  dual-stack driver support (VMBus synthetic + virtio) and DAX/virtio-pmem
  readiness. Kernel config indicates CONFIG_VIRTIO_PMEM=y, CONFIG_VIRTIO_FS=y,
  CONFIG_DAX=y alongside full Hyper-V driver support. To be verified against
  our specific requirements.
- **Upstream LTS kernel** configured with equivalent drivers — a viable
  alternative that avoids WSL-specific coupling.
- **Custom fork** of either base, modified for NXIA's needs.

The choice is not finalized. What matters is the capability set, not the
provenance.

The System VM provides:

- **A real Linux kernel** with both VMBus and virtio device support, enabling
  the same kernel to boot under different VMM configurations
- **Platform services**: Persistence engines, search/indexing, policy
  enforcement, orchestration, audit logging, capability management
- **Shared memory endpoints**: DAX-mapped regions for zero-copy shared memory
  with the host (and potentially with other VMs)
- **Cross-OS execution agent**: The execution fabric endpoint that bridges
  calls between host-native code and System VM services
- **Control agent**: The control-plane agent that speaks the NXIA lifecycle
  protocol (boot, configure, mount, exec, quiesce, resume, shutdown)
- **Namespace/container isolation**: Linux namespaces, cgroups, seccomp, LSM
  hooks — providing intra-VM isolation for workloads without additional VMs

The System VM kernel is not necessarily unmodified upstream Linux. NXIA R3
embraces kernel modification:
- Custom schedulers, memory policies, or storage drivers
- eBPF programs for platform-level observability and policy
- LSM modules for capability-based security enforcement
- As long as the Linux userspace ABI is preserved, existing distros and
  applications run unmodified

---

## 3. Shared Memory as a First-Class Primitive

> The concepts in this section are evolving. Names like "Portal" refer to the
> abstract capability (zero-copy shared memory across the VM boundary), not to
> a finalized interface specification.

### 3.1 Why shared memory matters

Every cross-boundary communication mechanism (RPC, IPC, network) involves
copying data. For a platform that treats the VM boundary as internal (not
external), copy overhead becomes a tax on every operation. A shared-memory
primitive eliminates this tax for the common case.

### 3.2 Implementation direction

The leading candidate mechanism is **virtio-pmem with DAX** (Direct Access):

```
Host (Stratum 1):
  backing_file → memory-mapped file → OpenVMM virtio-pmem device
                                                │
                                                ▼ (PCI/MMIO)
System VM (Stratum 3):
  /dev/pmem0 → DAX mount or raw mmap → arena allocator
```

Both sides see the same physical memory. No copies. The host writes; the guest
reads immediately. The guest writes; the host reads immediately. This is not
emulated shared memory — it is literal shared pages via the hypervisor's address
mapping.

Key requirements for the shared memory system (abstract):
- **Shared regions** mapped into both host and guest (and potentially multiple
  guests)
- **Arena allocation** within regions for structured, concurrent use
- **Bounded references** that cross boundaries safely (no raw pointers; offset +
  length + bounds checking + generation tracking)
- **Ring structures** for producer/consumer communication
- **Lightweight signaling** (doorbells) to wake consumers when data is available
  — preferring the lowest-latency mechanism available on each backend
  (e.g. HvCallSignalEvent on WHP; paravirt interrupts or vsock-based
  notification depending on backend)

These requirements will be refined as prototyping begins.

### 3.3 Multi-VM shared memory

For architectures with multiple VMs, the same host backing file can be mapped as
a virtio-pmem device into multiple partitions. Both VMs see the same memory.

On Linux hosts with KVM, an even more direct path exists: **ivshmem** (Inter-VM
Shared Memory), a virtual PCI device that maps a shared memory BAR into multiple
VMs with sub-microsecond latency and built-in interrupt-based signaling.

Hyper-V (Windows host) has no built-in ivshmem equivalent. The virtio-pmem
approach via OpenVMM is the practical workaround, with the option of extending
OpenVMM to add ivshmem-like semantics. Alternatively, `WHvMapGpaRange` can map
the same host buffer into multiple WHP partitions — true shared memory, but
requiring custom VMM-level integration.

### 3.4 Hypothesized performance envelope (requires measurement)

All numbers are provisional targets/estimates. They must be benchmarked on each
backend and configuration before being treated as commitments.

| Operation | Mechanism | Target Latency |
|---|---|---|
| Small RPC (< 4KB) | Control-plane channel (hvsocket/vsock) | 10-50 us |
| Bulk data transfer | DAX shared memory (memcpy speed) | GB/s throughput |
| Ring doorbell | Backend-appropriate signaling | < 10 us |
| Inter-VM shared memory (KVM) | ivshmem | < 1 us |
| Inter-VM shared memory (WHP) | virtio-pmem shared file | < 1 us (memory bus) |

---

## 4. VM Profiles

NXIA R3 does not assume a single VM. The VMM layer can support multiple
concurrently running VMs with different characteristics:

### System VM (default)

The shared substrate. Always running while the platform is active. Hosts
platform services, persistence engines, and the primary workload environment.
Multiple "distros" or workload namespaces can coexist within it (shared kernel,
per-namespace root filesystems — a pattern proven by WSL2's existing
architecture).

### Isolated VMs

Per-workload or per-project VMs providing stronger isolation than namespaces:

- Separate kernel instance (can load different modules, run different policies)
- Independent snapshot/restore lifecycle
- Dedicated resource allocation (memory, CPU)
- Connected to the System VM via shared memory regions

Use cases: untrusted code execution, multi-tenant isolation, security-sensitive
workloads.

### Micro-VMs (exploration item)

Sub-millisecond cold-start function sandboxes for ephemeral execution. This is
an area to evaluate, not an assumed capability.

Hyperlight (Microsoft's open-source Rust micro-VMM library, CNCF Sandbox
project) is a candidate for this role. It provides hardware-isolated micro-VMs
with no device model, no OS, single vCPU, and < 1 GB memory. Hyperlight and
OpenVMM overlap on host virtualization backends (WHP on Windows, KVM on Linux)
but are architecturally distinct.

**Open question**: Can Hyperlight-class micro-VMs participate in shared memory
regions (e.g., host-backed mappings accessible to both the System VM and
micro-VMs)? This requires evaluation under each backend and is not assumed.

Use cases if viable: sandboxed build steps, plugin evaluation, untrusted
function execution, AI tool-call sandboxing.

### Performance-Profile VMs

VMs tuned for specific workload characteristics:

- **Latency-optimized**: Pinned vCPUs, no overcommit, minimal device set
- **Throughput-optimized**: Multiple vCPUs, large memory, deep I/O queues
- **Footprint-optimized**: Minimal memory, shared kernel image, copy-on-write
  filesystems

These are configuration profiles, not separate architectures. The same VMM code
manages all of them.

---

## 5. Cross-OS Execution

> The concepts in this section are evolving. "Gemini" refers to the abstract
> capability of bidirectional cross-OS execution bridging. The specific protocol,
> transport, and marshalling design are not specified here.

### 5.1 The problem

The System VM runs Linux. The host may run Windows, macOS, or Linux. Users
expect applications that appear native on the host while leveraging the System
VM substrate. A cross-OS execution fabric bridges this gap.

### 5.2 Two directions

**L→H (Linux calls Host):** A process in the System VM invokes a host-native API
(Win32, COM, Cocoa, etc.). The fabric routes the call to a paired executor
process on the host, which performs the real API call and returns results.

**H→L (Host calls Linux):** A host process invokes a Linux API or platform
service. The fabric routes the call into the System VM.

### 5.3 Design principles (abstract)

- **Capability tokens, not raw handles**: Cross-boundary references use opaque
  tokens with rights bitmasks and lifetime management.
- **Policy-gated**: All cross-boundary calls pass through a policy module.
  Catastrophic operations denied by default.
- **Shared-memory-backed marshalling**: Large arguments live in shared memory
  regions. Only lightweight descriptors cross the RPC boundary.
- **Paired lifetime management**: If one side of a cross-boundary session dies,
  the other is cleaned up.

### 5.4 dotnext: .NET as first runtime integration target

**dotnext** is a fork of the .NET runtime, extended to support NXIA platform
features. In the context of cross-OS execution:

- **Linux .NET → Windows APIs**: P/Invoke calls targeting Windows DLLs route
  through the execution fabric to the Windows host.
- **Windows .NET → Linux APIs**: P/Invoke calls targeting Linux shared objects
  route through the execution fabric to the System VM.
- **Shared-memory-backed marshalling** for large buffer patterns.

dotnext demonstrates "runtime over meta-runtime" composition: a higher-level
runtime that targets the NXIA platform rather than individual host OSes.

---

## 6. Kernel Customization

### 6.1 What NXIA needs from the kernel

The System VM kernel is not just "any Linux kernel." NXIA R3 requires (or
benefits from) specific kernel capabilities:

**Required:**
- Dual-stack device support: VMBus synthetic devices AND virtio devices
  (so the same kernel boots under different VMM configurations)
- virtio-pmem + DAX support (for shared memory)
- Namespace/cgroup infrastructure (for intra-VM isolation)
- hvsocket/vsock transport (for control plane and cross-OS execution)

**Desired:**
- Custom LSM modules for capability-based security
- eBPF infrastructure for platform-level observability and policy enforcement
- Custom scheduler classes for platform-aware workload management
- Checkpoint/restore support (CONFIG_CHECKPOINT_RESTORE) for process-level
  snapshots

**To evaluate:**
- Whether kernel modifications are needed beyond configuration and modules,
  or whether the above can be achieved with an upstream LTS kernel + modules
- Whether a custom fork yields enough benefit over the maintenance cost
- Whether the WSL kernel's existing configuration already satisfies most
  requirements (early evidence suggests yes, but must be verified)

### 6.2 Relationship to the WSL kernel

The WSL Linux kernel (Microsoft's fork, currently 6.6.x LTS) is a strong
candidate starting point:

- Already configured with dual-stack drivers (VMBus + virtio)
- Already has virtio-pmem, DAX, virtiofs support compiled in
- Optimized for Hyper-V enlightenments (fast boot, synthetic devices)
- Actively maintained by Microsoft

However, NXIA R3 is not bound to the WSL kernel. An upstream LTS kernel with
equivalent configuration is a viable alternative. The choice depends on:
- Maintenance burden (tracking WSL fork vs. upstream + patches)
- WSL-specific modifications that may or may not benefit NXIA
- Long-term independence from Microsoft's WSL release cadence

### 6.3 Previous WARP research (historical context)

Earlier research under the name "WARP" (WSLX Arbitrary Runtime Platform)
explored repurposing the WSL/HCS pipeline to boot non-WSL kernels — including
hypothetical non-Linux OS personalities. In NXIA R3's direction, where we
control the VMM layer via OpenVMM, WARP's original problem ("hack HCS into
running other things") dissolves — the ability to boot and configure arbitrary
kernels is inherent to owning the VMM.

The useful kernel-customization concepts from WARP are folded into this
section. The "Track B" concept (entirely new non-Linux OS personality) remains
an interesting long-term research direction but is not part of the near-term
architecture.

---

## 7. Snapshots: Process-Level and VM-Level

### 7.1 Process-level snapshots

CRIU-class checkpoint/restore of individual processes or process trees within
the System VM. Captures: CPU registers, address space, open FDs, pipes, epoll
state, and (best-effort) network sockets.

Known challenges:
- seccomp filter inheritance from init
- ptrace permission restrictions
- hvsocket/vsock endpoint restoration requires host-side cooperation
- GPU compute state is generally non-checkpointable

Approach: cooperative checkpoint with profiles (minimal, network, full) and
host-side quiesce coordination.

### 7.2 VM-level snapshots

Full hypervisor checkpoint: guest RAM + vCPU state + virtual device state.
Restore means "resume the entire machine exactly where it was."

OpenVMM includes state serialization for devices. The key question is whether
quiesced save/restore works for the specific VM configuration NXIA uses. This
must be tested experimentally.

Shared memory state backed by host files survives snapshot/restore naturally —
the host-side mapping persists across VM restart.

### 7.3 Quiesce protocol (conceptual)

Before snapshot:
1. Control agent receives QUIESCE command from Stratum 1
2. Drain in-flight shared memory operations
3. Flush virtio-pmem regions for durability
4. Signal QUIESCE_COMPLETE
5. VM snapshot occurs

After restore:
1. Control agent receives RESUME command
2. Re-establish host communication channels
3. Resume shared memory producers/consumers
4. Normal operation continues

---

## 8. Security Model

### 8.1 Trust boundaries

- **Stratum 1 (host)** is the primary authority for host-OS policy
- **Stratum 2 (VMM)** enforces memory isolation, device access, and VM
  boundaries
- **Stratum 3 (System VM)** is the primary authority for platform policy
  within the guest (LSM, seccomp, namespaces, capability manifests)
- Guest calls to the host are "remote" even on the same machine — subject to
  policy and capability checks

### 8.2 Capability-based access

The platform moves toward capability-based security:
- Language-level intents ("this data is local-only", "this function may expose
  to remote", "this object must be encrypted at rest") compile into
  capability manifests
- Manifests are enforced by kernel mechanisms in the System VM (LSM modules,
  eBPF programs, namespace/cgroup policies)
- The host side enforces its own policy at the cross-OS execution boundary
- Capability tokens (not raw handles) cross all boundaries

### 8.3 Signed artifacts

- Kernel images and filesystem images should be signed
- Control-plane protocols are versioned and authenticated
- Shared memory regions are capability-scoped (not ambient-access)
- Snapshot artifacts are encrypted at rest

---

## 9. Cross-Platform Story

### 9.1 What's shared vs. platform-specific

The goal is ~80% shared Rust code across all host platforms, with
platform-specific code limited to host integration.

| Component | Shared | Platform-specific |
|---|---|---|
| VMM core (OpenVMM-class crates) | Device emulation, VMBus, virtio, enlightenment emulator | Backend selection (WHP/KVM/HVF) |
| Shared memory | Arena allocator, ring structures, span validation | Host-side file mapping API |
| Cross-OS execution | Protocol, capability tokens, session management | Transport adapter (hvsocket vs vsock) |
| Control agent | Protocol, quiesce/resume, process factory | Nothing (runs inside the VM) |
| System VM kernel | Everything (identical kernel image) | Nothing (same binary on all hosts) |
| Host integration | — | Networking, filesystem mounts, GPU, shell integration |

### 9.2 Linux host advantages

On a Linux host, KVM provides capabilities that WHP (Windows) lacks:

- **ivshmem**: Direct inter-VM shared memory via PCI BAR — the most performant
  multi-VM shared memory option available on any mainstream hypervisor
- **vhost-user**: User-space device backends with zero-copy
- **Nested virtualization**: Run NXIA inside a cloud VM
- **Mature VFIO GPU passthrough**: More flexible than Hyper-V GPU-P

For distributed and multi-VM research, **a Linux host with KVM is the most
capable platform**.

### 9.3 Windows host advantages

- **Where the users are** (developer workstations, enterprise)
- **WSL ecosystem validation**: Proven kernel + hypervisor + device stack. WSLX
  demonstrates a differentiated fork running side-by-side with canonical WSL.
- **Native Windows UI integration**: Cross-OS execution can invoke
  Win32/COM/WPF for native-feeling applications
- **dxgkrnl GPU passthrough**: A model (complex to replicate outside HCS) for
  GPU compute in the System VM

### 9.4 Linux deployment modes

NXIA R3 targets Linux as a host in two distinct modes:

**Mode 1: Regular distro, stock kernel**

NXIA installs on an existing Linux distribution (Ubuntu, Fedora, etc.) running
its own bare-metal kernel. No host kernel modification required beyond loading
standard modules (KVM, etc.). The System VM runs inside KVM on the host.

This mode is straightforward: the host is a standard Linux machine, and NXIA
is "just an application" (with a VMM) running on it.

**Mode 2: NXIA's own Linux-based distro**

An NXIA-branded Linux distribution where the bare-metal host kernel could
potentially be our customized kernel — the same one (or a variant) that runs
inside the System VM on other platforms.

Open questions requiring evaluation:
- **Technical feasibility**: Can our System VM kernel also serve as a bare-metal
  host kernel? What configuration differences are needed?
- **Technical viability**: Does running our kernel bare-metal provide meaningful
  advantages (deeper host integration, bypass the VM layer for some workloads,
  native shared-memory without virtio-pmem)?
- **Maintenance complexity**: Does maintaining a bare-metal variant of the kernel
  create an unsustainable maintenance burden vs. using an upstream LTS kernel
  for the host and our customized kernel only inside VMs?

This is a research question, not a decided direction.

---

## 10. Relationship to Existing Research and Projects

### WSLX

WSLX is a fully differentiated WSL fork, independently distributable, running
in parallel to canonical WSL on Windows. It provides:
- Evidence that the WSL kernel and distro model work as building blocks
- A proving ground for shared memory and cross-OS execution concepts
- Confirmation that the WSL kernel's dual-stack drivers enable booting under
  both HCS and OpenVMM-class VMMs

NXIA R3 generalizes beyond WSLX. Where WSLX is Windows-specific and
WSL-derived, NXIA R3 is cross-platform and VMM-native.

### dotnext

A fork of the .NET runtime, extended to support NXIA platform features. The
first planned port of an existing runtime to the meta-runtime. Demonstrates
"runtime over meta-runtime" composition.

### VAYRON

The broader experimental runtime integration path. In NXIA R3 terms, VAYRON
represents the layer where higher-level runtimes, toolchains, and application
frameworks target the meta-runtime. dotnext is the first concrete instance.

### OpenVMM / OpenHCL

OpenVMM is the candidate VMM substrate. OpenHCL is a related paravisor project.

**OpenHCL is a candidate layer to evaluate, not an assumed component.** It may
provide useful properties (boot shimming, isolation tiers, measurement hooks,
security attestation), but its applicability and required adaptations must be
validated experimentally. Open questions:
- What value does OpenHCL add in this architecture beyond boot shimming?
- What adaptation/completion work would be needed?
- What experiments would validate its utility (boot flow, device model
  interactions, performance overhead, snapshot/restore interplay)?

### Hyperlight

Hyperlight is a candidate for micro-VM function sandboxes (see Section 4).
It overlaps with OpenVMM on host virtualization backends but is architecturally
distinct (no device model, no OS support, sub-millisecond cold start). Its
participation in NXIA's shared memory fabric requires evaluation.

---

## 11. What "NXIA-Class" Means

> An **NXIA-class architecture** is one where policy, identity, memory
> semantics, persistence/search, and orchestration are OS-grade services with
> stable ABIs, and where language runtimes compile intent into those services
> rather than reimplementing them per application.

This means:

- **Semantic gravity moving downward**: Platform concerns (security, persistence,
  distribution, search) become kernel/service-grade, not application-grade.
- **Deduplication of concerns**: Every application doesn't re-invent storage,
  auth, IPC, or policy enforcement.
- **Uniform identity and capability handling**: One model, enforced at the
  right boundary, spanning host and guest.
- **Enforcement at the kernel boundary**: The System VM kernel is the
  enforcement root, not the host OS or the application.
- **Portable system behavior**: The System VM provides identical semantics
  on every supported host OS.

---

## 12. Known Challenges and Risks

### Adoption-critical

1. **Developer experience**: Filesystem performance (cross-boundary access via
   virtiofs/9P is slower than native), IDE integration, debugger attachment
   across VM boundary, startup latency.
2. **GPU and graphics**: Host GPU integration is the hardest missing piece for
   native-feeling applications. This may be the long pole.
3. **Networking and identity**: Building our own networking stack (NAT, DNS,
   port forwarding) and host identity bridging requires careful design.

### Architectural

4. **Shared-memory correctness**: Shared regions must be capability-scoped,
   observable, and resilient under failure. The concurrency model matters as
   much as the memory layout.
5. **Snapshot complexity**: Network socket state, GPU state, and host-integration
   channel state may not survive checkpoint/restore. Must design for graceful
   degradation.
6. **VMM maturity**: OpenVMM is described as a development platform, not a
   ready-to-deploy application. Maturity varies by backend and device surface.
   Stabilization work is expected.

### Strategic

7. **Host resilience**: The platform should survive host-OS changes because the
   System VM holds the important state and logic. Stratum 1 should be thin
   enough that it can be repaired or rewritten without affecting the rest. This
   must be validated, not assumed.
8. **Virtualization overhead**: The thesis accepts a "modest" penalty. This must
   be quantified. If shared memory provides memcpy-speed bulk transfer and
   low-microsecond signaling, the overhead is manageable. If cross-boundary
   filesystem access adds unacceptable latency, developer experience suffers.

---

## 13. Research Directions (Phased)

### Phase A: Boot the System VM

Boot a Linux kernel on an OpenVMM-class launcher on both Windows (WHP) and
Linux (KVM). Establish VM lifecycle, serial console, basic block device for
root filesystem. Validate that the kernel boots and enumerates devices correctly
under OpenVMM.

**Verification tasks:**
- Confirm hv1 emulator capabilities on KVM path
- Confirm which enlightenments are required by the chosen kernel
- Confirm behavior parity between WHP and KVM backends

### Phase B: Control Plane and Process Factory

Implement a control agent protocol: lifecycle commands, process factory,
I/O relay. Get a working shell. Run a distro.

### Phase C: Shared Memory MVP

Configure virtio-pmem in OpenVMM. Map a host file with DAX in the guest.
Implement arena allocation and ring structures. Benchmark: raw throughput,
ring operation latency, signaling latency.

### Phase D: Cross-OS Execution Control Plane

Establish host↔guest sessions. Implement CALL/RETURN with copy-marshalling.
Demonstrate bidirectional execution (Linux calling Host, Host calling Linux).

### Phase E: Shared Memory Data Plane Integration

Connect cross-OS execution to the shared memory fabric: call descriptors
reference shared memory regions instead of copying arguments. Measure the
improvement.

### Phase F: Cross-Platform Validation

CI pipeline building and testing on both Windows and Linux. Same VMM binary,
same kernel, same control agent, same shared memory. Document platform delta.

### Phase G: Multi-VM and Distributed Research

Launch multiple VMs from the same VMM process. Shared memory regions across
VMs. Prototype inter-VM signaling. Evaluate ivshmem on KVM. Explore
distributed shared memory semantics.

### Phase H: Micro-VM Evaluation

Evaluate Hyperlight-class micro-VMs as a sidecar capability. Test whether
shared memory regions can be mapped into both the System VM and micro-VMs.
Benchmark end-to-end function dispatch latency.

### Phase I: Snapshot MVP

Process-level: CRIU-based checkpoint/restore within the System VM (minimal
profile). VM-level: probe OpenVMM's state serialization. Test shared memory
state persistence across snapshot/restore cycles.

---

## 14. Summary

NXIA R3 is a meta-runtime architecture built on three strata:

1. **Thin host components** for OS integration and lifecycle
2. **A portable VMM** (OpenVMM-class, Rust, WHP/KVM/HVF backends) for
   controlled virtualization
3. **A Linux-kernel-based System VM** as the stable semantic substrate

With shared-memory-first communication, cross-OS execution bridging,
VM-level snapshots, and multi-VM topologies as core enabling primitives.

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
  Gemini early design exploration
- `doc/research/WARP/seminal - WSLX WARP.md` — WARP arbitrary runtime platform
  (historical; folded into Section 6.3)
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
